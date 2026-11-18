#include "obd_diag.h"

#include <stdio.h>
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
    obd_status_t as = obd_assemble(&r, protocol, false, &reply);
    *have = obd_decode_codes(as == OBD_OK ? &reply : NULL, protocol, service, list);
    return st;
}

static void progress_to(obd_diag_progress_fn progress, void *ctx, obd_diag_step_t step)
{
    if (progress != NULL) {
        progress(ctx, step);
    }
}

void obd_diag_apply_make(obd_diag_t *d, const char *make_name)
{
    if (d->make_chosen) {
        d->maker = NULL;
        d->make = -1;
        d->make_chosen = false;
    }
    if (d->maker != NULL || make_name == NULL || make_name[0] == '\0') {
        return;
    }
    int idx = obd_dtc_make_find(make_name);
    if (idx < 0) {
        return;
    }
    d->make = idx;
    d->maker = obd_dtc_make_name(idx);
    d->make_chosen = true;
}

void obd_diag_apply_vehicle(obd_diag_t *d, const char *model, int year)
{
    if (d->model_chosen) {
        d->model = NULL;
        d->model_make = NULL;
        d->model_chosen = false;
    }
    if (d->year_chosen) {
        d->model_year = 0;
        d->year_chosen = false;
    }
    if (!d->have_vin && year > 0) {
        d->model_year = year;
        d->year_chosen = true;
    }
    if (d->model == NULL && model != NULL && model[0] != '\0') {
        d->model = model;
        d->model_make = d->maker;
        d->model_chosen = true;
    }
}

elm_status_t obd_diag_read_vin(elm_t *elm, int current_year, obd_diag_t *d,
                               obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    static obd_reply_t reply;
    scan_t sc = { elm, trace, ctx };
    int protocol = d->link.protocol;
    d->have_vin = false;
    d->vin[0] = '\0';
    d->model_year = 0;
    d->model = NULL;
    d->model_make = NULL;
    d->model_chosen = d->year_chosen = false;
    if (!d->make_chosen) {
        d->maker = NULL;
        d->make = -1;
    }
    elm_status_t st = command(&sc, "0902", &r);
    if (st == ELM_OK && obd_assemble(&r, protocol, false, &reply) == OBD_OK &&
        obd_decode_vin(&reply, protocol, d->vin)) {
        d->have_vin = true;
        d->model_year = obd_vin_model_year(d->vin, current_year);
        obd_vin_model_t md;
        if (obd_vin_model(d->vin, d->model_year, &md)) {
            d->model_make = md.make;
            d->model = md.model;
        }
        obd_vin_maker_t mk;
        if (obd_vin_maker(d->vin, &mk)) {
            d->maker = mk.maker;
            d->make = obd_dtc_make_find(mk.dtc_make);
            d->make_chosen = false;
        }
    }
    return st;
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
        if (st == ELM_OK && obd_assemble(&r, protocol, false, &reply) == OBD_OK) {
            out->have_readiness = obd_decode_readiness(&reply, &out->readiness);
        }
    }

    progress_to(progress, ctx, OBD_DIAG_STEP_STORED);
    st = read_codes(&sc, "03", OBD_SERVICE_STORED, protocol, &out->stored, &out->have_stored);
    if (lost(st)) {
        out->stopped_at = OBD_DIAG_STEP_STORED;
        goto lost_link;
    }
    if (out->have_stored && out->stored.count > 0) {
        st = obd_freeze_read(elm, protocol, &out->freeze, trace, ctx);   /* Mode 02 */
        if (lost(st)) {
            out->stopped_at = OBD_DIAG_STEP_STORED;
            goto lost_link;
        }
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
    st = obd_diag_read_vin(elm, current_year, out, trace, ctx);
    if (lost(st)) {
        out->stopped_at = OBD_DIAG_STEP_VIN;
        goto lost_link;
    }

    st = obd_diag_read_modules(elm, out, trace, ctx);
    if (lost(st)) {
        out->stopped_at = OBD_DIAG_STEP_VIN;
        goto lost_link;
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

/* ---------- clearing codes (Mode 04) ---------- */

obd_clear_result_t obd_clear_codes(elm_t *elm, const obd_link_t *link, uint8_t *refusal_code,
                                   obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    static obd_reply_t reply;
    scan_t sc = { elm, trace, ctx };
    int protocol = link->protocol;
    if (refusal_code != NULL) {
        *refusal_code = 0;
    }

    /* Parked check: vehicle speed (01 0D, km/h) must be 0. */
    if (obd_link_pid_supported(link, 0x0D)) {
        elm_status_t st = command(&sc, "010D", &r);
        if (lost(st)) {
            return OBD_CLEAR_LOST;
        }
        if (st == ELM_OK && obd_assemble(&r, protocol, false, &reply) == OBD_OK) {
            for (int e = 0; e < reply.ecu_count; e++) {
                for (int m = 0; m < reply.ecu[e].msg_count; m++) {
                    const obd_msg_t *msg = &reply.ecu[e].msg[m];
                    if (msg->len >= 3 && msg->data[0] == 0x41 && msg->data[1] == 0x0D &&
                        msg->data[2] > 0) {
                        return OBD_CLEAR_MOVING;
                    }
                }
            }
        }
    }

    elm_status_t st = command(&sc, "04", &r);
    if (lost(st)) {
        return OBD_CLEAR_LOST;
    }
    if (st != ELM_OK || obd_assemble(&r, protocol, false, &reply) != OBD_OK) {
        return OBD_CLEAR_NO_REPLY;
    }
    bool refused = false;
    for (int e = 0; e < reply.ecu_count; e++) {
        for (int m = 0; m < reply.ecu[e].msg_count; m++) {
            const obd_msg_t *msg = &reply.ecu[e].msg[m];
            if (msg->len >= 1 && msg->data[0] == 0x44) {
                return OBD_CLEAR_DONE;
            }
            if (msg->len >= 3 && msg->data[0] == 0x7F && msg->data[1] == 0x04) {
                refused = true;
                if (refusal_code != NULL) {
                    *refusal_code = msg->data[2];
                }
            }
        }
    }
    return refused ? OBD_CLEAR_REFUSED : OBD_CLEAR_NO_REPLY;
}

const char *obd_clear_result_str(obd_clear_result_t result)
{
    switch (result) {
    case OBD_CLEAR_DONE:     return "Codes cleared";
    case OBD_CLEAR_MOVING:   return "Not cleared: the car is moving. Stop and park first";
    case OBD_CLEAR_REFUSED:  return "The car refused (try ignition on, engine off)";
    case OBD_CLEAR_NO_REPLY: return "The car didn't confirm the clear";
    case OBD_CLEAR_LOST:     return "Connection to the adapter lost";
    case OBD_CLEAR_RESULT_COUNT: break;
    }
    return "?";
}

/* ---------- modules ---------- */

/* The code's module from a headers-on read of the same service. */
static void tag_codes(obd_code_list_t *list, const obd_code_list_t *tagged)
{
    for (int i = 0; i < list->count; i++) {
        for (int j = 0; j < tagged->count; j++) {
            if (tagged->code[j].has_ecu && strcmp(list->code[i].code, tagged->code[j].code) == 0) {
                list->code[i].has_ecu = true;
                list->code[i].ecu = tagged->code[j].ecu;
                break;
            }
        }
    }
}

elm_status_t obd_diag_read_modules(elm_t *elm, obd_diag_t *d, obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    static obd_reply_t reply;
    static obd_code_list_t tagged;
    scan_t sc = { elm, trace, ctx };
    int protocol = d->link.protocol;
    d->have_modules = false;
    d->module_count = 0;
    elm_status_t st = command(&sc, "ATH1", &r);
    if (st != ELM_OK) {
        return lost(st) ? st : ELM_OK;   /* an adapter without headers: skip it */
    }
    st = command(&sc, "0100", &r);
    if (st == ELM_OK && obd_assemble(&r, protocol, true, &reply) == OBD_OK) {
        for (int e = 0; e < reply.ecu_count && d->module_count < OBD_DIAG_MODULES_MAX; e++) {
            if (reply.ecu[e].has_id) {
                d->module_id[d->module_count++] = reply.ecu[e].id;
            }
        }
        d->have_modules = d->module_count > 0;
    }
    static const struct { const char *cmd; uint8_t service; } reads[] = {
        { "03", OBD_SERVICE_STORED }, { "07", OBD_SERVICE_PENDING },
    };
    for (int i = 0; i < 2 && !lost(st); i++) {
        obd_code_list_t *list = i == 0 ? &d->stored : &d->pending;
        if (list->count == 0) {
            continue;
        }
        st = command(&sc, reads[i].cmd, &r);
        if (st == ELM_OK && obd_assemble(&r, protocol, true, &reply) == OBD_OK &&
            obd_decode_codes(&reply, protocol, reads[i].service, &tagged)) {
            tag_codes(list, &tagged);
        }
    }
    if (lost(st)) {
        return st;
    }
    elm_status_t back = command(&sc, "ATH0", &r);
    return lost(back) ? back : ELM_OK;
}

char *obd_diag_module_name(int protocol, uint32_t id, char *out, size_t len)
{
    /* The standard engine and transmission addresses (ISO 15765-4 and
     * SAE J1979: engine 7E8 / 10, transmission 7E9 / 18). */
    uint32_t src = protocol == 6 || protocol == 8 ? id
                 : (protocol == 7 || protocol == 9) ? (id & 0xFFu) : id;
    bool can11 = protocol == 6 || protocol == 8;
    if ((can11 && src == 0x7E8) || (!can11 && src == 0x10)) {
        snprintf(out, len, "Engine");
    } else if ((can11 && src == 0x7E9) || (!can11 && src == 0x18)) {
        snprintf(out, len, "Transmission");
    } else {
        snprintf(out, len, "Module %X", (unsigned)id);
    }
    return out;
}
