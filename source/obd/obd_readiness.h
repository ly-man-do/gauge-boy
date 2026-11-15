#ifndef GAUGEBOY_OBD_READINESS_H
#define GAUGEBOY_OBD_READINESS_H

/*
 * Mode 01 PID 01 ("41 01 A B C D"): check engine light, number of stored
 * codes and readiness monitors.
 *
 *   A  bit 7 = MIL (check engine light) on; bits 0-6 = stored code count
 *   B  bit 3 = compression ignition (diesel); bits 0-2 = continuous
 *      monitors supported (misfire, fuel system, components); bits 4-6 =
 *      those monitors not complete
 *   C  non-continuous monitors supported; D  those not complete. The
 *      meaning of each bit depends on spark vs compression ignition.
 *
 * Several ECUs may answer (engine, transmission). The combined status
 * has the light on if any ECU says so, the code counts added up, and a
 * monitor supported if any ECU supports it and incomplete if any
 * supporting ECU says it isn't complete.
 *
 * Platform-free.
 */

#include <stdbool.h>
#include <stdint.h>

#include "obd_reply.h"

typedef enum {
    OBD_MONITOR_NOT_SUPPORTED = 0,
    OBD_MONITOR_COMPLETE,
    OBD_MONITOR_INCOMPLETE,
} obd_monitor_state_t;

#define OBD_MONITOR_COUNT 11   /* 3 continuous + 8 non-continuous */

typedef struct {
    const char *name;          /* e.g. "Catalyst"; NULL for a reserved bit */
    bool continuous;
    obd_monitor_state_t state;
} obd_monitor_t;

typedef struct {
    bool valid;                /* at least one ECU sent a usable 41 01 reply */
    bool mil_on;               /* check engine light */
    int dtc_count;             /* stored codes, as reported (all ECUs) */
    bool compression;          /* diesel monitors (else spark ignition) */
    int ecus;                  /* ECUs that answered */
    obd_monitor_t monitor[OBD_MONITOR_COUNT];
} obd_readiness_t;

/* Decodes the four data bytes A-D of one ECU's reply. */
void obd_readiness_from_bytes(const uint8_t abcd[4], obd_readiness_t *out);

/* Decodes an assembled reply to "01 01" and combines all ECUs. Returns
 * out->valid. */
bool obd_decode_readiness(const obd_reply_t *reply, obd_readiness_t *out);

/* Supported monitors that are complete / not complete. */
int obd_readiness_ready_count(const obd_readiness_t *st);
int obd_readiness_not_ready_count(const obd_readiness_t *st);

const char *obd_monitor_state_str(obd_monitor_state_t state);

#endif
