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

static obd_live_pid_t *find(obd_live_t *lv, uint8_t pid)
{
    for (int i = 0; i < lv->count; i++) {
        if (lv->pid[i].pid == pid) {
            return &lv->pid[i];
        }
    }
    return NULL;
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
    if (pid == OBD_PID_MAP) {
        float rpm = 0.0f;
        lv->have_pending_map = obd_live_value(lv, PID_RPM, &rpm) && rpm < 1.0f;
        lv->pending_map_kpa = value;
    } else if (pid == PID_RPM) {
        if (value < 1.0f && lv->have_pending_map) {
            lv->engine_off_map_kpa = lv->pending_map_kpa;
            lv->have_engine_off_map = true;
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
        return OBD_LIVE_READ;
    }

    lv->failures++;
    if (++p->misses >= OBD_LIVE_MAX_MISSES) {
        p->dropped = true;
    }
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
    if (!obd_live_value(lv, OBD_PID_MAP, &map)) {
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
