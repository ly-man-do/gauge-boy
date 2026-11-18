#ifndef GAUGEBOY_OBD_FREEZE_H
#define GAUGEBOY_OBD_FREEZE_H

/*
 * Freeze frame (Mode 02): the snapshot the engine computer kept when a
 * code set. Frame 0 is read: "02 02 00" gives the code, "02 00 00" (and
 * 20, 40 ...) the PIDs it holds, then "02 pid 00" each one GaugeBoy
 * decodes. Replies are "42 pid frame data...". Platform-free.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elm327/elm327.h"
#include "obd_link.h"
#include "obd_reply.h"

#define OBD_FREEZE_MAX 16   /* values kept */

typedef struct {
    bool have;              /* a freeze frame with a code was read */
    char code[6];           /* the code that set it, e.g. "P0171" */
    int count;
    uint8_t pid[OBD_FREEZE_MAX];
    float value[OBD_FREEZE_MAX];   /* metric, as obd_pid_decode */
} obd_freeze_t;

/* The data bytes of "42 pid frame ..." in a reply (the first ECU that
 * has it). False if no message has it. */
bool obd_freeze_find(const obd_reply_t *reply, uint8_t pid, uint8_t frame, const uint8_t **data,
                     size_t *len);

/* Reads frame 0 into out (cleared first). NO DATA, or code 0000, means no
 * freeze frame (out->have false, ELM_OK). Returns the adapter's status:
 * ELM_ERR_IO / ELM_ERR_TIMEOUT mean the link was lost. trace may be NULL. */
elm_status_t obd_freeze_read(elm_t *elm, int protocol, obd_freeze_t *out,
                             obd_link_trace_fn trace, void *ctx);

/* A one-line summary, "RPM 750 rpm, Load 25 %, Coolant 190 F, ...", in
 * the given units. Returns out. */
char *obd_freeze_text(const obd_freeze_t *f, int units, char *out, size_t len);

#endif
