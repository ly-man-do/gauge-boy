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
} obd_live_pid_t;

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
 * an adapter that refuses it gets plain requests from then on. */
obd_live_step_t obd_live_step(obd_live_t *lv, elm_t *elm, obd_link_trace_fn trace, void *ctx);

/* Change a PID's desired refresh time (0 = as often as possible). */
void obd_live_set_period(obd_live_t *lv, uint8_t pid, uint32_t period_ms);

/* The PID's entry, or NULL if it isn't polled. */
const obd_live_pid_t *obd_live_get(const obd_live_t *lv, uint8_t pid);

/* Latest value; false if the PID isn't polled or has no value yet. */
bool obd_live_value(const obd_live_t *lv, uint8_t pid, float *value);

/* Manifold vacuum in kPa: baro (PID 33) or the engine-off MAP, minus the
 * latest MAP. False until both are known. */
bool obd_live_vacuum(const obd_live_t *lv, float *vacuum_kpa);

/* Requests per second since obd_live_init (all PIDs together). */
float obd_live_rate(const obd_live_t *lv, uint64_t now_ms);

#endif
