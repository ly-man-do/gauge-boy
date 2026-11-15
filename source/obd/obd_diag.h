#ifndef GAUGEBOY_OBD_DIAG_H
#define GAUGEBOY_OBD_DIAG_H

/*
 * One DIAG scan: everything the DIAG screen shows, read in one go.
 *
 *   obd_link_open   adapter reset, protocol search, supported PIDs
 *   0101            check engine light, code count, readiness monitors
 *   03 / 07 / 0A    stored, pending and permanent codes
 *   0902            VIN -> maker, model year, make for manufacturer codes
 *
 * Platform-free and blocking (several seconds; the protocol search alone
 * takes ~4.5 s on ISO 9141-2): call it from the worker thread. obd_diag_t
 * is large; keep it static.
 */

#include <stdbool.h>

#include "obd_codes.h"
#include "obd_link.h"
#include "obd_readiness.h"
#include "obd_vin.h"

#define OBD_DIAG_TIMEOUT_MS 5000u   /* code and VIN requests (multi-frame) */

typedef enum {
    OBD_DIAG_STEP_CONNECT = 0,
    OBD_DIAG_STEP_STATUS,
    OBD_DIAG_STEP_STORED,
    OBD_DIAG_STEP_PENDING,
    OBD_DIAG_STEP_PERMANENT,
    OBD_DIAG_STEP_VIN,
    OBD_DIAG_STEP_DONE,
} obd_diag_step_t;

typedef struct {
    obd_link_result_t result;       /* OBD_LINK_OK if the car answered */
    obd_diag_step_t stopped_at;     /* step that failed, or DONE */
    obd_link_t link;

    bool have_readiness;
    obd_readiness_t readiness;

    bool have_stored, have_pending, have_permanent;
    bool permanent_unsupported;     /* refused or NO DATA: hide the tab */
    obd_code_list_t stored, pending, permanent;

    bool have_vin;
    char vin[OBD_VIN_LEN + 1];
    int model_year;                 /* 0 if unknown */
    const char *maker;              /* NULL if the WMI isn't listed */
    int make;                       /* for obd_dtc_text_for(), -1 if unknown */
} obd_diag_t;

/* Progress callback: called before each step (for "Reading codes..."). */
typedef void (*obd_diag_progress_fn)(void *ctx, obd_diag_step_t step);

/*
 * Runs the scan. reset: send ATZ and the setup first (do it on the first
 * scan after connecting). current_year picks the VIN year cycle (0 =
 * North American position-7 rule). trace / progress may be NULL.
 * Returns out->result; a car that stops answering part-way keeps what
 * was read so far.
 */
obd_link_result_t obd_diag_scan(elm_t *elm, bool reset, int current_year, obd_diag_t *out,
                                obd_link_trace_fn trace, obd_diag_progress_fn progress,
                                void *ctx);

const char *obd_diag_step_str(obd_diag_step_t step);

#endif
