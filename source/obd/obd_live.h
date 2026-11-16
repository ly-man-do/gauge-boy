#ifndef GAUGEBOY_OBD_LIVE_H
#define GAUGEBOY_OBD_LIVE_H

/*
 * Live data poller for the LIVE and TRIM screens.
 *
 * Polls the chosen Mode 01 PIDs the car supports, one request per step,
 * and keeps the latest value of each with when it was read and how often
 * it refreshes. Each PID has a desired refresh time (obd_pid_info's
 * refresh_ms: RPM as often as possible, coolant every 15 s ...); the next
 * request always goes to the most overdue PID, ties in list order
 * (gauge-boy-3n6.10). The adapter's battery voltage (ATRV) is read
 * every few seconds in between. A PID that keeps answering NO DATA is
 * dropped. For manifold vacuum it remembers the MAP read with the engine
 * off when the car has no barometric pressure PID (brief section 7): a MAP
 * read between two RPM-0 reads, so a MAP taken just as the engine starts
 * isn't mistaken for the outside pressure.
 *
 * Platform-free and blocking (one request per obd_live_step): run it on
 * the worker thread, never the render loop. Times come from the adapter
 * driver's clock (elm->io.now_ms). obd_live_t is small enough for a
 * static.
 */

#include <stdbool.h>
#include <stdint.h>

#include "elm327/elm327.h"
#include "obd_link.h"

#define OBD_LIVE_MAX        20      /* PIDs at once */
#define OBD_LIVE_MAX_MISSES 3       /* NO DATA in a row before a PID is dropped */
#define OBD_LIVE_VOLTS_MS   5000u   /* ATRV this often */
#define OBD_LIVE_TIMEOUT_MS 2000u   /* per request */
#define OBD_LIVE_MULTI_MAX  6       /* PIDs in one CAN request (SAE J1979) */
#define OBD_LIVE_LEFT_OUT_LOG 3     /* combined replies kept for the log */
#define OBD_LIVE_LEFT_OUT_RAW 160   /* escaped reply text kept for each */

typedef struct {
    uint8_t pid;
    bool have;               /* a value has been read */
    float value;             /* metric, as obd_pid_decode */
    uint64_t at_ms;          /* when it was read */
    float hz;                /* refresh rate, smoothed; 0 until two reads */
    uint32_t reads;
    uint8_t misses;          /* NO DATA (or no usable reply) in a row */
    bool dropped;            /* stopped polling after OBD_LIVE_MAX_MISSES */
    uint32_t period_ms;      /* desired refresh time, 0 = as often as possible */
    uint64_t due_ms;         /* when it should be read next (0 = now) */
    bool single_next;        /* left out of a combined reply: ask it alone next */
} obd_live_pid_t;

/* A combined request whose reply left PIDs out (for the log). */
typedef struct {
    char cmd[2 + 2 * OBD_LIVE_MULTI_MAX + 2];   /* with the response count */
    uint8_t missing[OBD_LIVE_MULTI_MAX];
    int missing_count;
    char reply[OBD_LIVE_LEFT_OUT_RAW];   /* raw reply, escaped ("\r") */
} obd_live_left_out_t;

typedef struct {
    obd_live_pid_t pid[OBD_LIVE_MAX];
    int count;
    int protocol;            /* from the link, for reply assembly */

    bool have_volts;
    float volts;             /* adapter (battery) voltage, from ATRV */
    uint64_t volts_at_ms;
    bool volts_failed;       /* ATRV refused: stop asking */

    bool have_engine_off_map;
    float engine_off_map_kpa;   /* MAP with RPM 0: baro when PID 33 is missing */
    bool have_pending_map;      /* MAP read after an RPM 0, kept if the next RPM is 0 too */
    float pending_map_kpa;

    bool use_count;          /* ask for one reply ("010C1"): set after init */
    bool count_refused;      /* the adapter answered "?" to it: plain requests */
    bool use_multi;          /* CAN: several PIDs per request: set after init */
    bool multi_refused;      /* the car didn't answer them: one PID at a time */
    bool multi_worked;       /* a combined request got 2+ PIDs back: never refuse after */
    uint32_t multi_requests; /* requests that carried several PIDs */
    bool use_engine_header;  /* CAN: ask the engine ECU only (ATSH 7E0): set after init */
    bool header_set;         /* the engine ECU's address is set in the adapter */
    bool header_checked;     /* and a reply came back with it */
    bool header_refused;     /* refused or unanswered: every ECU again */
    bool multi_count_off;    /* combined requests with a response count left PIDs out */
    uint32_t left_out;       /* PIDs left out of combined replies (asked again alone) */
    obd_live_left_out_t left_out_log[OBD_LIVE_LEFT_OUT_LOG];   /* the first few */
    int left_out_logged;

    uint32_t requests;       /* commands sent */
    uint32_t failures;       /* NO DATA, unusable or failed replies */
    uint64_t started_ms;
} obd_live_t;

typedef enum {
    OBD_LIVE_READ = 0,       /* a value (or the voltage) was read */
    OBD_LIVE_MISSED,         /* NO DATA or an unusable reply; polling goes on */
    OBD_LIVE_IDLE,           /* nothing left to poll */
    OBD_LIVE_LOST,           /* the adapter stopped answering: reconnect */
} obd_live_step_t;

/*
 * Start polling: keeps the wanted PIDs (in order, without repeats) that
 * the car supports and obd_pid decodes. Returns how many will be polled.
 */
int obd_live_init(obd_live_t *lv, const obd_link_t *link, const uint8_t *wanted, int n);

/* One request: ATRV when it is due, else the next PID. trace may be NULL.
 * With use_count set, PID requests carry the response count "1" so the
 * adapter answers as soon as one ECU replies instead of waiting for more;
 * an adapter that refuses it gets plain requests from then on.
 * With use_multi set and a CAN car, a request carries up to 6 PIDs: the
 * most overdue one plus others that are due or read as often as possible
 * ("010C0D05"); a car that answers with none or only the first of them
 * gets one PID per request from then on. A PID left out of a combined
 * reply isn't a miss: it is asked alone next, where NO DATA does count
 * (left_out, and the first few such replies in left_out_log).
 * With use_engine_header set and an OBD CAN protocol (6-9), the first step
 * sends "ATSH 7E0" (29-bit: "ATSH DA10F1") so only the engine ECU answers:
 * other ECUs' multi-frame replies can't interleave with its own (headers
 * are off). Combined requests then carry the response count too. If the
 * adapter refuses it, or the first request with it gets no answer, every
 * ECU is asked again (header_refused). Call obd_live_restore_header()
 * when polling stops. */
obd_live_step_t obd_live_step(obd_live_t *lv, elm_t *elm, obd_link_trace_fn trace, void *ctx);

/* Puts back the all-ECU (functional) request address if the engine ECU's
 * was set, so DIAG hears every ECU again. Returns the adapter's status
 * (ELM_OK if there was nothing to do). */
elm_status_t obd_live_restore_header(obd_live_t *lv, elm_t *elm);

/* Change a PID's desired refresh time (0 = as often as possible). */
void obd_live_set_period(obd_live_t *lv, uint8_t pid, uint32_t period_ms);

/* The PID's entry, or NULL if it isn't polled. */
const obd_live_pid_t *obd_live_get(const obd_live_t *lv, uint8_t pid);

/* Latest value; false if the PID isn't polled or has no value yet. */
bool obd_live_value(const obd_live_t *lv, uint8_t pid, float *value);

/* Manifold vacuum in kPa: baro (PID 33) or the engine-off MAP, minus the
 * latest MAP (PID 0B, or 87 on cars without it). Negative = boost. False
 * until both are known. */
bool obd_live_vacuum(const obd_live_t *lv, float *vacuum_kpa);

/* Requests per second since obd_live_init (all PIDs together). */
float obd_live_rate(const obd_live_t *lv, uint64_t now_ms);

#endif
