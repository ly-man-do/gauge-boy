#include "obd_diag.h"

#include <string.h>

#include "dtc.h"
#include "obd_reply.h"

typedef struct {
    elm_t *elm;
    obd_link_trace_fn trace;
    void *ctx;
} scan_t;

static elm_status_t command(scan_t *sc, const char *cmd, elm_response_t *r)
{
    elm_t *elm = sc->elm;
    uint64_t t0 = elm->io.now_ms != NULL ? elm->io.now_ms(elm->io.ctx) : 0;
    elm_status_t st = elm_command(elm, cmd, OBD_DIAG_TIMEOUT_MS, r);
    if (sc->trace != NULL) {
        uint64_t t1 = elm->io.now_ms != NULL ? elm->io.now_ms(elm->io.ctx) : t0;
        sc->trace(sc->ctx, cmd, r, (unsigned)(t1 - t0));
    }
    return st;
}

static bool lost(elm_status_t st)
{
    return st == ELM_ERR_IO || st == ELM_ERR_TIMEOUT;
}

/* Sends a code request and decodes it. NO DATA counts as "no codes". */
static elm_status_t read_codes(scan_t *sc, const char *cmd, uint8_t service, int protocol,
                               obd_code_list_t *list, bool *have)
{
    static elm_response_t r;
    static obd_reply_t reply;
    elm_status_t st = command(sc, cmd, &r);
    *have = false;
    memset(list, 0, sizeof *list);
    if (st == ELM_ERR_NO_DATA) {
        *have = true;
        return st;
    }
    if (st != ELM_OK) {
        return st;
    }
    obd_status_t as = obd_assemble(r.lines, r.line_count, protocol, false, &reply);
    *have = obd_decode_codes(as == OBD_OK ? &reply : NULL, protocol, service, list);
    return st;
}

static void progress_to(obd_diag_progress_fn progress, void *ctx, obd_diag_step_t step)
{
    if (progress != NULL) {
        progress(ctx, step);
    }
}

obd_link_result_t obd_diag_scan(elm_t *elm, bool reset, int current_year, obd_diag_t *out,
                                obd_link_trace_fn trace, obd_diag_progress_fn progress,
                                void *ctx)
{
    static elm_response_t r;
    static obd_reply_t reply;
    scan_t sc = { elm, trace, ctx };
    memset(out, 0, sizeof *out);
    out->make = -1;

    progress_to(progress, ctx, OBD_DIAG_STEP_CONNECT);
    out->result = obd_link_open_traced(elm, reset, &out->link, trace, ctx);
    if (out->result != OBD_LINK_OK) {
        out->stopped_at = OBD_DIAG_STEP_CONNECT;
        return out->result;
    }
    int protocol = out->link.protocol;
    elm_status_t st;

    /* Check engine light and readiness. */
    progress_to(progress, ctx, OBD_DIAG_STEP_STATUS);
    if (obd_link_pid_supported(&out->link, 0x01)) {
        st = command(&sc, "0101", &r);
        if (lost(st)) {
            goto lost_link;
        }
        if (st == ELM_OK && obd_assemble(r.lines, r.line_count, protocol, false, &reply) == OBD_OK) {
            out->have_readiness = obd_decode_readiness(&reply, &out->readiness);
        }
    }

    progress_to(progress, ctx, OBD_DIAG_STEP_STORED);
    st = read_codes(&sc, "03", OBD_SERVICE_STORED, protocol, &out->stored, &out->have_stored);
    if (lost(st)) {
        out->stopped_at = OBD_DIAG_STEP_STORED;
        goto lost_link;
    }

    progress_to(progress, ctx, OBD_DIAG_STEP_PENDING);
    st = read_codes(&sc, "07", OBD_SERVICE_PENDING, protocol, &out->pending, &out->have_pending);
    if (lost(st)) {
        out->stopped_at = OBD_DIAG_STEP_PENDING;
        goto lost_link;
    }

    /* Permanent codes: 2010+ US cars. Older cars refuse or say NO DATA. */
    progress_to(progress, ctx, OBD_DIAG_STEP_PERMANENT);
    st = read_codes(&sc, "0A", OBD_SERVICE_PERMANENT, protocol, &out->permanent,
                    &out->have_permanent);
    if (lost(st)) {
        out->stopped_at = OBD_DIAG_STEP_PERMANENT;
        goto lost_link;
    }
    out->permanent_unsupported = st != ELM_OK ||
                                 (out->permanent.ecus_answered == 0 && out->permanent.ecus_refused > 0);
    if (out->permanent_unsupported) {
        out->have_permanent = false;
    }

    progress_to(progress, ctx, OBD_DIAG_STEP_VIN);
    st = command(&sc, "0902", &r);
    if (lost(st)) {
        out->stopped_at = OBD_DIAG_STEP_VIN;
        goto lost_link;
    }
    if (st == ELM_OK && obd_assemble(r.lines, r.line_count, protocol, false, &reply) == OBD_OK &&
        obd_decode_vin(&reply, protocol, out->vin)) {
        out->have_vin = true;
        out->model_year = obd_vin_model_year(out->vin, current_year);
        obd_vin_maker_t mk;
        if (obd_vin_maker(out->vin, &mk)) {
            out->maker = mk.maker;
            out->make = obd_dtc_make_find(mk.dtc_make);
        }
    }

    out->stopped_at = OBD_DIAG_STEP_DONE;
    progress_to(progress, ctx, OBD_DIAG_STEP_DONE);
    return out->result;

lost_link:
    if (out->stopped_at == OBD_DIAG_STEP_CONNECT) {
        out->stopped_at = OBD_DIAG_STEP_STATUS;
    }
    out->result = OBD_LINK_LOST;
    return out->result;
}

const char *obd_diag_step_str(obd_diag_step_t step)
{
    switch (step) {
    case OBD_DIAG_STEP_CONNECT:   return "Finding the car's protocol";
    case OBD_DIAG_STEP_STATUS:    return "Reading the check engine light";
    case OBD_DIAG_STEP_STORED:    return "Reading stored codes";
    case OBD_DIAG_STEP_PENDING:   return "Reading pending codes";
    case OBD_DIAG_STEP_PERMANENT: return "Reading permanent codes";
    case OBD_DIAG_STEP_VIN:       return "Reading the VIN";
    case OBD_DIAG_STEP_DONE:      return "Done";
    }
    return "?";
}
