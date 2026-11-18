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
#include "obd_freeze.h"
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
    obd_freeze_t freeze;            /* frame 0, read when codes are stored */

    bool have_vin;
    char vin[OBD_VIN_LEN + 1];
    int model_year;                 /* 0 if unknown */
    const char *maker;              /* NULL if the WMI isn't listed */
    const char *model_make;         /* from NHTSA vPIC with model, e.g. "Jeep" */
    const char *model;              /* e.g. "Cherokee"; NULL if not decoded */
    int make;                       /* for obd_dtc_text_for(), -1 if unknown */
    bool make_chosen;               /* maker / make came from SETUP, not the VIN */
    bool model_chosen;              /* model came from SETUP (no VIN model) */
    bool year_chosen;               /* model_year came from SETUP (no VIN) */
    bool vin_only;                  /* found after a connect (link and VIN), not scanned */
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

/*
 * Reads the VIN ("0902") on d->link's protocol and decodes it into d:
 * have_vin, vin, model_year, maker / make and the vPIC model. Without a
 * VIN they are cleared (a make chosen in SETUP stays). Re-apply the SETUP
 * make and vehicle afterwards. Returns the adapter's status.
 */
elm_status_t obd_diag_read_vin(elm_t *elm, int current_year, obd_diag_t *d,
                               obd_link_trace_fn trace, void *ctx);

/*
 * Applies the make chosen in SETUP (a name from obd_dtc_make_name(), or
 * NULL / "" for auto). A make read from the VIN always wins; otherwise the
 * chosen make sets maker and make and marks make_chosen. Call it after each
 * scan and again whenever the choice changes (it undoes an earlier choice).
 */
void obd_diag_apply_make(obd_diag_t *d, const char *make_name);

/*
 * Applies the model and year typed in SETUP (model NULL / "" and year 0
 * for none). The year is used only when no VIN was read; the model when
 * the VIN gave none (model_make is then the maker, which may be NULL).
 * model must stay valid while d uses it. Call after obd_diag_apply_make,
 * after each scan and whenever the entries change (undoes earlier ones).
 */
void obd_diag_apply_vehicle(obd_diag_t *d, const char *model, int year);

/* ---------- clearing codes (Mode 04) ---------- */

typedef enum {
    OBD_CLEAR_DONE = 0,       /* an ECU confirmed (44) */
    OBD_CLEAR_MOVING,         /* vehicle speed above 0: not sent */
    OBD_CLEAR_REFUSED,        /* the car refused (7F 04 nn), e.g. engine running */
    OBD_CLEAR_NO_REPLY,       /* NO DATA or no confirmation */
    OBD_CLEAR_LOST,           /* connection lost */
    OBD_CLEAR_RESULT_COUNT
} obd_clear_result_t;

/*
 * Clears stored and pending codes and resets the readiness monitors
 * (service 04). Only call after the user has confirmed (safety rule).
 * First reads the vehicle speed (01 0D) when the car supports it and
 * refuses unless it is 0. refusal_code gets the negative response code
 * when refused (e.g. 0x22 "conditions not correct"), else 0. Clearing
 * then needs a new scan to show the result.
 */
obd_clear_result_t obd_clear_codes(elm_t *elm, const obd_link_t *link, uint8_t *refusal_code,
                                   obd_link_trace_fn trace, void *ctx);

const char *obd_clear_result_str(obd_clear_result_t result);

#endif
