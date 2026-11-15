#ifndef GAUGEBOY_OBD_LINK_H
#define GAUGEBOY_OBD_LINK_H

/*
 * Talking to the car: the ELM327 start-up, the protocol search, and the
 * supported-PID scan every later request relies on.
 *
 *   ATZ, ATE0, ATL0, ATSP0   adapter reset and setup (headers stay off:
 *                            the layout tested on the Ridgeline)
 *   0100                     first request; the adapter searches all
 *                            protocols (about 4.5 s on ISO 9141-2). With
 *                            only the ignition on, many cars don't answer
 *   ATDPN                    which protocol it found
 *   0120, 0140, ... 01E0     more supported-PID ranges, while the last
 *                            PID of each range says the next one exists
 *
 * Platform-free (uses the elm327 driver only); blocking, so call it from
 * the worker thread.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elm327/elm327.h"

typedef enum {
    OBD_LINK_OK = 0,
    OBD_LINK_NO_ADAPTER,     /* adapter start-up failed (not answering / odd replies) */
    OBD_LINK_NO_VEHICLE,     /* adapter fine, car didn't answer 0100 (engine off?) */
    OBD_LINK_LOST,           /* connection dropped or timed out mid-way */
    OBD_LINK_RESULT_COUNT
} obd_link_result_t;

#define OBD_PID_RANGES 7     /* 0100, 0120, ... 01C0 (PIDs 0x01-0xE0) */

typedef struct {
    obd_link_result_t result;
    elm_status_t elm_status;       /* status of the step that failed */
    const char *failed_command;    /* e.g. "0100"; NULL on success */
    char adapter_id[ELM_LINE_MAX]; /* "ELM327 V2.3" (when reset was done) */
    int protocol;                  /* ELM327 protocol number 1-12, 0 if unknown */
    bool protocol_auto;
    int ecus;                      /* ECUs that answered 0100 */
    int ranges_read;               /* supported-PID ranges read (1 = only 0100) */
    uint8_t supported[OBD_PID_RANGES][4]; /* A-D of each 01 00/20/40... reply, all ECUs OR'ed */
} obd_link_t;

/* Runs the sequence above (the adapter reset part only if reset is
 * true). Returns out->result. */
obd_link_result_t obd_link_open(elm_t *elm, bool reset, obd_link_t *out);

/* Called after each command with its reply and how long it took (for
 * the test app's log). */
typedef void (*obd_link_trace_fn)(void *ctx, const char *cmd, const elm_response_t *resp,
                                  unsigned ms);

/* obd_link_open with a trace of every command; trace may be NULL. */
obd_link_result_t obd_link_open_traced(elm_t *elm, bool reset, obd_link_t *out,
                                       obd_link_trace_fn trace, void *ctx);

/* True if the car said it supports Mode 01 PID pid (0x01-0xE0). */
bool obd_link_pid_supported(const obd_link_t *link, uint8_t pid);

/* Supported PIDs as hex text ("01 03 04 05 ..."), for logs and the
 * compatibility list. Returns out. */
char *obd_link_pid_list(const obd_link_t *link, char *out, size_t out_len);

const char *obd_link_result_str(obd_link_result_t result);

#endif
