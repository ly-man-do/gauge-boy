#include "obd_live.h"

#include <stdio.h>
#include <string.h>

#include "obd_pid.h"
#include "obd_reply.h"

#define PID_RPM 0x0C

static uint64_t clock_ms(const elm_t *elm)
{
    return elm->io.now_ms != NULL ? elm->io.now_ms(elm->io.ctx) : 0;
}

static bool lost(elm_status_t st)
{
    return st == ELM_ERR_IO || st == ELM_ERR_TIMEOUT;
}

/* The engine ECU's (physical) and every ECU's (functional) request address
 * on the OBD CAN protocols; NULL for the others. */
static const char *engine_header(int protocol)
{
    return protocol == 6 || protocol == 8 ? "ATSH7E0"
         : protocol == 7 || protocol == 9 ? "ATSHDA10F1" : NULL;
}

static const char *functional_header(int protocol)
{
    return protocol == 6 || protocol == 8 ? "ATSH7DF"
         : protocol == 7 || protocol == 9 ? "ATSHDB33F1" : NULL;
}

static elm_status_t send_header(obd_live_t *lv, elm_t *elm, const char *cmd,
                                obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    uint64_t t0 = clock_ms(elm);
    elm_status_t st = elm_command(elm, cmd, OBD_LIVE_TIMEOUT_MS, &r);
    lv->requests++;
    if (trace != NULL) {
        trace(ctx, cmd, &r, (unsigned)(clock_ms(elm) - t0));
    }
    return st;
}

elm_status_t obd_live_restore_header(obd_live_t *lv, elm_t *elm)
{
    const char *cmd = functional_header(lv->protocol);
    if (!lv->header_set || cmd == NULL) {
        return ELM_OK;
    }
    lv->header_set = false;
    return send_header(lv, elm, cmd, NULL, NULL);
}

/* The first request with the engine ECU's address got no answer: ask every
 * ECU again (not a miss for any PID). */
static obd_live_step_t header_unanswered(obd_live_t *lv, elm_t *elm, obd_link_trace_fn trace,
                                         void *ctx)
{
    lv->header_set = false;
    lv->header_refused = true;
    elm_status_t st = send_header(lv, elm, functional_header(lv->protocol), trace, ctx);
    return lost(st) ? OBD_LIVE_LOST : OBD_LIVE_MISSED;
}

static obd_live_pid_t *find(obd_live_t *lv, uint8_t pid)
{
    for (int i = 0; i < lv->count; i++) {
        if (lv->pid[i].pid == pid) {
            return &lv->pid[i];
        }
    }
    return NULL;
}

void obd_live_seed_baro(obd_live_t *lv, float kpa)
{
    if (lv->have_engine_off_map || kpa < 50.0f || kpa > 110.0f) {
        return;
    }
    lv->engine_off_map_kpa = kpa;
    lv->have_engine_off_map = true;
    lv->engine_off_map_saved = true;
}

void obd_live_set_period(obd_live_t *lv, uint8_t pid, uint32_t period_ms)
{
    obd_live_pid_t *p = find(lv, pid);
    if (p != NULL) {
        p->period_ms = period_ms;
    }
}

const obd_live_pid_t *obd_live_get(const obd_live_t *lv, uint8_t pid)
{
    return find((obd_live_t *)lv, pid);
}

bool obd_live_value(const obd_live_t *lv, uint8_t pid, float *value)
{
    const obd_live_pid_t *p = obd_live_get(lv, pid);
    if (p == NULL || !p->have) {
        return false;
    }
    if (value != NULL) {
        *value = p->value;
    }
    return true;
}

int obd_live_init(obd_live_t *lv, const obd_link_t *link, const uint8_t *wanted, int n)
{
    memset(lv, 0, sizeof *lv);
    lv->protocol = link->protocol;
    for (int i = 0; i < n && lv->count < OBD_LIVE_MAX; i++) {
        uint8_t pid = wanted[i];
        const obd_pid_info_t *info = obd_pid_info(pid);
        if (info != NULL && obd_link_pid_supported(link, pid) && find(lv, pid) == NULL) {
            lv->pid[lv->count].pid = pid;
            lv->pid[lv->count].period_ms = info->refresh_ms;
            lv->count++;
        }
    }
    return lv->count;
}

/* Remember the MAP with the engine off: the barometric pressure for
 * vacuum on cars without PID 33. A MAP read after an RPM of 0 is kept only
 * if the next RPM read is 0 too (the engine may start in between). */
static void note_engine_off_map(obd_live_t *lv, uint8_t pid, float value)
{
    if (pid == OBD_PID_MAP || pid == OBD_PID_MAP_WIDE) {
        float rpm = 0.0f;
        lv->have_pending_map = obd_live_value(lv, PID_RPM, &rpm) && rpm < 1.0f;
        lv->pending_map_kpa = value;
    } else if (pid == PID_RPM) {
        if (value < 1.0f && lv->have_pending_map) {
            lv->engine_off_map_kpa = lv->pending_map_kpa;
            lv->have_engine_off_map = true;
            lv->engine_off_map_saved = false;   /* a fresh reading */
        }
        lv->have_pending_map = false;
    }
}

static obd_live_step_t read_volts(obd_live_t *lv, elm_t *elm, uint64_t t0,
                                  obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    int mv = 0;
    elm_status_t st = elm_read_voltage(elm, &mv, &r);
    uint64_t t1 = clock_ms(elm);
    lv->requests++;
    if (trace != NULL) {
        trace(ctx, "ATRV", &r, (unsigned)(t1 - t0));
    }
    if (lost(st)) {
        return OBD_LIVE_LOST;
    }
    lv->volts_at_ms = t1;
    if (st != ELM_OK) {
        lv->volts_failed = true;   /* not every clone has ATRV: stop asking */
        lv->failures++;
        return OBD_LIVE_MISSED;
    }
    lv->volts = (float)mv / 1000.0f;
    lv->have_volts = true;
    return OBD_LIVE_READ;
}

/* A value read at t1: refresh rate, value, misses, engine-off MAP. */
static void record(obd_live_t *lv, obd_live_pid_t *p, float v, uint64_t t1)
{
    if (p->have && t1 > p->at_ms) {
        float inst = 1000.0f / (float)(t1 - p->at_ms);
        p->hz = p->reads == 1 ? inst : 0.7f * p->hz + 0.3f * inst;
    }
    p->value = v;
    p->have = true;
    p->at_ms = t1;
    p->reads++;
    p->misses = 0;
    note_engine_off_map(lv, p->pid, v);
}

static void miss(obd_live_t *lv, obd_live_pid_t *p)
{
    lv->failures++;
    if (++p->misses >= OBD_LIVE_MAX_MISSES) {
        p->dropped = true;
    }
}

/*
 * CAN: one request for the most overdue PID (first) and up to 5 more that
 * are due or read as often as possible, most overdue first. Returns IDLE
 * when there is only one PID to ask for (the caller sends a single).
 */
static obd_live_step_t multi_step(obd_live_t *lv, elm_t *elm, int first, uint64_t t0,
                                  obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    static obd_reply_t reply;
    int pick[OBD_LIVE_MULTI_MAX];
    int np = 0;
    bool taken[OBD_LIVE_MAX] = { false };
    pick[np++] = first;
    taken[first] = true;
    while (np < OBD_LIVE_MULTI_MAX) {
        int best = -1;
        for (int i = 0; i < lv->count; i++) {
            const obd_live_pid_t *q = &lv->pid[i];
            if (taken[i] || q->dropped || q->single_next ||
                (q->due_ms > t0 && q->period_ms != 0)) {
                continue;
            }
            if (best < 0 || q->due_ms < lv->pid[best].due_ms) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        pick[np++] = best;
        taken[best] = true;
    }
    if (np < 2) {
        return OBD_LIVE_IDLE;
    }

    char cmd[2 + 2 * OBD_LIVE_MULTI_MAX + 2] = "01";
    for (int k = 0; k < np; k++) {
        snprintf(cmd + 2 + 2 * k, sizeof cmd - 2 - 2 * (size_t)k, "%02X",
                 (unsigned)lv->pid[pick[k]].pid);
    }
    /* One ECU answers: the adapter can return on its reply ("...1"). */
    bool count = lv->header_set && lv->use_count && !lv->count_refused && !lv->multi_count_off;
    if (count) {
        strcat(cmd, "1");
    }
    elm_status_t st = elm_command(elm, cmd, OBD_LIVE_TIMEOUT_MS, &r);
    uint64_t t1 = clock_ms(elm);
    lv->requests++;
    lv->multi_requests++;
    if (trace != NULL) {
        trace(ctx, cmd, &r, (unsigned)(t1 - t0));
    }
    if (lost(st)) {
        return OBD_LIVE_LOST;
    }
    if (count && st == ELM_ERR_UNKNOWN_COMMAND) {
        lv->multi_count_off = true;   /* no count on combined requests; nobody's miss */
        lv->multi_count_why = "adapter answered ?";
        return OBD_LIVE_MISSED;
    }

    /* Every PID found in any ECU's reply. */
    bool found[OBD_LIVE_MULTI_MAX] = { false };
    int nfound = 0;
    if (st == ELM_OK && obd_assemble(&r, lv->protocol, false, &reply) == OBD_OK) {
        for (int e = 0; e < reply.ecu_count; e++) {
            for (int m = 0; m < reply.ecu[e].msg_count; m++) {
                const obd_msg_t *msg = &reply.ecu[e].msg[m];
                uint8_t pids[OBD_LIVE_MULTI_MAX];
                float values[OBD_LIVE_MULTI_MAX];
                int n = obd_pid_parse_multi(msg->data, msg->len, pids, values, OBD_LIVE_MULTI_MAX);
                for (int j = 0; j < n; j++) {
                    for (int k = 0; k < np; k++) {
                        if (!found[k] && lv->pid[pick[k]].pid == pids[j]) {
                            found[k] = true;
                            nfound++;
                            record(lv, &lv->pid[pick[k]], values[j], t1);
                        }
                    }
                }
            }
        }
    }
    if (count && nfound == 0) {
        /* Nothing with the count: drop the count first (nobody's miss);
         * only an uncounted request decides the rest. */
        lv->multi_count_off = true;
        lv->multi_count_why = st == ELM_OK ? "reply not understood" : elm_status_str(st);
        return OBD_LIVE_MISSED;
    }
    if (lv->header_set && !lv->header_checked) {
        if (nfound == 0) {
            return header_unanswered(lv, elm, trace, ctx);
        }
        lv->header_checked = true;
    }
    if (count && nfound < np) {
        lv->multi_count_off = true;   /* the count may cut a reply short: stop using it */
        lv->multi_count_why = "a reply left PIDs out";
    }
    if (nfound >= 2) {
        lv->multi_worked = true;
    }
    if (!lv->multi_worked && (nfound == 0 || (nfound == 1 && found[0]))) {
        /* No answer, or only to the first PID: this car takes one PID a
         * request. Nothing counts as a miss; singles from the next step. */
        lv->multi_refused = true;
        if (nfound == 0) {
            lv->failures++;
            return OBD_LIVE_MISSED;
        }
    }
    obd_live_left_out_t *log = NULL;
    if (!lv->multi_refused && nfound < np && lv->left_out_logged < OBD_LIVE_LEFT_OUT_LOG) {
        log = &lv->left_out_log[lv->left_out_logged++];
        memset(log, 0, sizeof *log);
        snprintf(log->cmd, sizeof log->cmd, "%s", cmd);
        elm_escape_raw(r.raw, r.raw_len, log->reply, sizeof log->reply);
    }
    for (int k = 0; k < np; k++) {
        obd_live_pid_t *q = &lv->pid[pick[k]];
        q->due_ms = t1 + q->period_ms;
        if (!found[k] && !lv->multi_refused) {
            /* Not a miss: ask it alone next, where NO DATA counts. */
            q->single_next = true;
            q->due_ms = 0;   /* ahead of PIDs read as often as possible */
            lv->left_out++;
            if (log != NULL) {
                log->missing[log->missing_count++] = q->pid;
            }
        }
    }
    return OBD_LIVE_READ;
}

obd_live_step_t obd_live_step(obd_live_t *lv, elm_t *elm, obd_link_trace_fn trace, void *ctx)
{
    static elm_response_t r;
    static obd_reply_t reply;
    uint64_t t0 = clock_ms(elm);
    if (lv->requests == 0) {
        lv->started_ms = t0;
    }

    if (!lv->volts_failed && (!lv->have_volts || t0 - lv->volts_at_ms >= OBD_LIVE_VOLTS_MS)) {
        return read_volts(lv, elm, t0, trace, ctx);
    }

    /* The most overdue PID still being polled (ties: list order). */
    int idx = -1;
    for (int i = 0; i < lv->count; i++) {
        if (!lv->pid[i].dropped && (idx < 0 || lv->pid[i].due_ms < lv->pid[idx].due_ms)) {
            idx = i;
        }
    }
    if (idx < 0) {
        return OBD_LIVE_IDLE;
    }
    obd_live_pid_t *p = &lv->pid[idx];

    if (lv->use_engine_header && !lv->header_set && !lv->header_refused) {
        const char *cmd = engine_header(lv->protocol);
        if (cmd == NULL) {
            lv->header_refused = true;   /* not an OBD CAN protocol */
        } else {
            elm_status_t hs = send_header(lv, elm, cmd, trace, ctx);
            if (lost(hs)) {
                return OBD_LIVE_LOST;
            }
            lv->header_set = hs == ELM_OK;
            lv->header_refused = hs != ELM_OK;
            lv->header_checked = false;
            return OBD_LIVE_READ;
        }
    }

    bool single = p->single_next;
    p->single_next = false;
    if (!single && lv->use_multi && !lv->multi_refused &&
        obd_family(lv->protocol) == OBD_FAMILY_CAN) {
        obd_live_step_t ms = multi_step(lv, elm, idx, t0, trace, ctx);
        if (ms != OBD_LIVE_IDLE) {
            return ms;
        }
        /* IDLE here: only one PID to ask for, so a single request. */
    }

    char cmd[8];
    bool count = lv->use_count && !lv->count_refused;
    snprintf(cmd, sizeof cmd, count ? "01%02X1" : "01%02X", p->pid);
    elm_status_t st = elm_command(elm, cmd, OBD_LIVE_TIMEOUT_MS, &r);
    uint64_t t1 = clock_ms(elm);
    lv->requests++;
    if (trace != NULL) {
        trace(ctx, cmd, &r, (unsigned)(t1 - t0));
    }
    if (count && st == ELM_ERR_UNKNOWN_COMMAND) {
        /* No response count on this adapter: the same PID, plainly. */
        lv->count_refused = true;
        snprintf(cmd, sizeof cmd, "01%02X", p->pid);
        t0 = t1;
        st = elm_command(elm, cmd, OBD_LIVE_TIMEOUT_MS, &r);
        t1 = clock_ms(elm);
        lv->requests++;
        if (trace != NULL) {
            trace(ctx, cmd, &r, (unsigned)(t1 - t0));
        }
    }
    if (lost(st)) {
        return OBD_LIVE_LOST;
    }

    p->due_ms = t1 + p->period_ms;
    float v = 0.0f;
    if (st == ELM_OK && obd_assemble(&r, lv->protocol, false, &reply) == OBD_OK &&
        obd_pid_from_reply(&reply, p->pid, &v) == OBD_PID_OK) {
        record(lv, p, v, t1);
        lv->header_checked = lv->header_checked || lv->header_set;
        return OBD_LIVE_READ;
    }
    if (lv->header_set && !lv->header_checked) {
        p->due_ms = 0;   /* ask it again with every ECU listening */
        return header_unanswered(lv, elm, trace, ctx);
    }
    miss(lv, p);
    return OBD_LIVE_MISSED;
}

bool obd_live_vacuum(const obd_live_t *lv, float *vacuum_kpa)
{
    float baro = 0.0f, map = 0.0f;
    if (!obd_live_value(lv, OBD_PID_BARO, &baro)) {
        if (!lv->have_engine_off_map) {
            return false;
        }
        baro = lv->engine_off_map_kpa;
    }
    if (!obd_live_value(lv, OBD_PID_MAP, &map) && !obd_live_value(lv, OBD_PID_MAP_WIDE, &map)) {
        return false;
    }
    if (vacuum_kpa != NULL) {
        *vacuum_kpa = obd_vacuum_kpa(baro, map);
    }
    return true;
}

float obd_live_rate(const obd_live_t *lv, uint64_t now_ms)
{
    if (lv->requests == 0 || now_ms <= lv->started_ms) {
        return 0.0f;
    }
    return (float)lv->requests * 1000.0f / (float)(now_ms - lv->started_ms);
}
