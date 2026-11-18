#include "time_attack.h"

#include <math.h>
#include <string.h>

#define STILL_KMH 0.5f   /* OBD speed is whole km/h: under this is 0 */

float ta_target_kmh(bool metric)
{
    return metric ? 100.0f : 60.0f * TA_MPH_KMH;
}

const char *ta_run_name(ta_run_t run, bool metric)
{
    switch (run) {
    case TA_RUN_LAUNCH: return metric ? "0-100 km/h" : "0-60 mph";
    case TA_RUN_BRAKE:  return metric ? "100-0 km/h" : "60-0 mph";
    default:            return "1/4 mile";
    }
}

const char *ta_run_key(ta_run_t run, bool metric)
{
    switch (run) {
    case TA_RUN_LAUNCH: return metric ? "0-100" : "0-60";
    case TA_RUN_BRAKE:  return metric ? "100-0" : "60-0";
    default:            return "quarter";
    }
}

static void set_state(ta_t *ta, ta_state_t st, uint64_t at_ms)
{
    ta->state = st;
    ta->state_ms = at_ms;
}

void ta_arm(ta_t *ta, ta_run_t run, bool metric, uint64_t now_ms)
{
    ta_result_t keep = ta->result;
    memset(ta, 0, sizeof *ta);
    if (keep.have && keep.run == run && keep.metric == metric) {
        ta->result = keep;   /* still shown until the next result */
    }
    ta->run = run;
    ta->metric = metric;
    ta->target_kmh = ta_target_kmh(metric);
    set_state(ta, TA_STOP_FIRST, now_ms);
}

void ta_off(ta_t *ta)
{
    ta->state = TA_OFF;
}

/* Gives up a run (no result); it arms again once the car stands still. */
static void give_up(ta_t *ta, const char *why, uint64_t at_ms)
{
    ta->note = why;
    set_state(ta, TA_STOP_FIRST, at_ms);
}

static double area_m(float kmh0, float kmh1, double ms)
{
    return ((double)kmh0 + (double)kmh1) / 2.0 / 3.6 * ms / 1000.0;
}

static void finish(ta_t *ta, double end_ms, uint64_t at_ms)
{
    ta_result_t *r = &ta->result;
    memset(r, 0, sizeof *r);
    r->have = true;
    r->run = ta->run;
    r->metric = ta->metric;
    double secs = (end_ms - ta->start_ms) / 1000.0;
    r->seconds = (float)secs;
    r->hz = secs > 0.0 ? (float)(ta->samples / secs) : 0.0f;
    ta->note = NULL;
    set_state(ta, TA_DONE, at_ms);
}

/* Launch: the moment the car left 0, from the first two moving readings. */
static void fix_start(ta_t *ta, uint64_t t, float kmh)
{
    double lo = (double)ta->zero_ms, hi = (double)ta->move1_ms;
    double est = (lo + hi) / 2.0;
    if (kmh > ta->move1_kmh && t > ta->move1_ms) {
        double a = ((double)kmh - ta->move1_kmh) / (double)(t - ta->move1_ms);   /* km/h per ms */
        est = hi - ta->move1_kmh / a;
    }
    ta->start_ms = est < lo ? lo : est > hi ? hi : est;
    ta->start_fixed = true;
    ta->dist_m += area_m(0.0f, ta->move1_kmh, hi - ta->start_ms);   /* the ramp from 0 */
}

/* Launch and quarter runs: one moving reading after the first. */
static bool launch_step(ta_t *ta, uint64_t t, float kmh)
{
    if (!ta->start_fixed) {
        fix_start(ta, t, kmh);
    }
    double d_prev = ta->dist_m;
    ta->dist_m += area_m(ta->prev_kmh, kmh, (double)(t - ta->prev_ms));

    if (!ta->split_done && kmh >= ta->target_kmh) {
        double cross = (double)ta->prev_ms;
        if (ta->prev_kmh < ta->target_kmh) {
            double f = (ta->target_kmh - ta->prev_kmh) / (double)(kmh - ta->prev_kmh);
            cross += f * (double)(t - ta->prev_ms);
        }
        ta->split_done = true;
        ta->split_s = (float)((cross - ta->start_ms) / 1000.0);
        if (ta->run == TA_RUN_LAUNCH) {
            finish(ta, cross, t);
            return true;
        }
    }
    if (ta->run == TA_RUN_QUARTER && ta->dist_m >= TA_QUARTER_M) {
        /* Where in the last step: speed changes linearly over it. */
        double h = (double)(t - ta->prev_ms) / 1000.0;
        double v1 = ta->prev_kmh / 3.6, v2 = kmh / 3.6;
        double rem = TA_QUARTER_M - d_prev;
        double a = (v2 - v1) / (2.0 * h);
        double s;
        if (fabs(a) < 1e-9) {
            s = v1 > 0.0 ? rem / v1 : h;
        } else {
            double disc = v1 * v1 + 4.0 * a * rem;
            s = (-v1 + sqrt(disc > 0.0 ? disc : 0.0)) / (2.0 * a);
        }
        s = s < 0.0 ? 0.0 : s > h ? h : s;
        float split = ta->split_done ? ta->split_s : 0.0f;
        finish(ta, (double)ta->prev_ms + s * 1000.0, t);
        ta->result.trap_kmh = (float)((v1 + (v2 - v1) * (h > 0.0 ? s / h : 1.0)) * 3.6);
        ta->result.split_s = split;
        ta->result.distance_m = (float)TA_QUARTER_M;
        return true;
    }
    if ((double)t - ta->start_ms > TA_LAUNCH_MAX_MS) {
        give_up(ta, "Took over a minute", t);
    }
    return false;
}

/* Braking: one reading after the start. */
static bool brake_step(ta_t *ta, uint64_t t, float kmh)
{
    if (kmh > ta->target_kmh) {
        ta->note = "Sped up again";
        set_state(ta, TA_BRAKE_READY, t);
        return false;
    }
    if (kmh < STILL_KMH) {
        /* Stopped between the last moving reading and this one. */
        double lo = (double)ta->prev_ms, hi = (double)t;
        if (lo < ta->start_ms) {
            lo = ta->start_ms;
        }
        double end = (lo + hi) / 2.0;
        if (ta->have_pprev && (double)ta->pprev_ms >= ta->start_ms - 1e-6 &&
            ta->pprev_kmh > ta->prev_kmh && ta->prev_ms > ta->pprev_ms) {
            double a = ((double)ta->pprev_kmh - ta->prev_kmh) / (double)(ta->prev_ms - ta->pprev_ms);
            end = (double)ta->prev_ms + ta->prev_kmh / a;
        }
        end = end < lo ? lo : end > hi ? hi : end;
        ta->dist_m += area_m(ta->prev_kmh, 0.0f, end - (double)ta->prev_ms);
        float dist = (float)ta->dist_m;
        finish(ta, end, t);
        ta->result.distance_m = dist;
        return true;
    }
    ta->dist_m += area_m(ta->prev_kmh, kmh, (double)(t - ta->prev_ms));
    if ((double)t - ta->start_ms > TA_BRAKE_MAX_MS) {
        ta->note = "Too slow for a braking run";
        set_state(ta, TA_BUILD_UP, t);
    }
    return false;
}

bool ta_speed(ta_t *ta, uint64_t t, float kmh)
{
    if (ta->state == TA_OFF || (ta->have_last && t <= ta->last_ms) || isnan(kmh)) {
        return false;
    }
    if (ta->state == TA_RUNNING && ta->have_last && t - ta->last_ms > TA_GAP_MS) {
        give_up(ta, "Speed readings stopped", t);
    }
    bool still = kmh < STILL_KMH;
    if (still && !(ta->have_last && ta->last_kmh < STILL_KMH)) {
        ta->still_since = t;
    }
    ta->have_pprev = ta->have_prev;
    ta->pprev_ms = ta->prev_ms;
    ta->pprev_kmh = ta->prev_kmh;
    ta->have_prev = ta->have_last;
    ta->prev_ms = ta->last_ms;
    ta->prev_kmh = ta->last_kmh;
    ta->have_last = true;
    ta->last_ms = t;
    ta->last_kmh = kmh;

    switch (ta->state) {
    case TA_COUNTDOWN:
        if (!still) {
            give_up(ta, "False start: stop and wait for GO", t);
        }
        return false;
    case TA_GO:
        if (!still) {
            set_state(ta, TA_RUNNING, t);
            ta->zero_ms = ta->have_prev ? ta->prev_ms : t;
            ta->move1_ms = t;
            ta->move1_kmh = kmh;
            ta->start_ms = (double)t;
            ta->start_fixed = false;
            ta->dist_m = 0.0;
            ta->split_done = false;
            ta->samples = 1;
            ta->note = NULL;
        }
        return false;
    case TA_BUILD_UP:
        if (kmh > ta->target_kmh) {
            set_state(ta, TA_BRAKE_READY, t);
        }
        return false;
    case TA_BRAKE_READY:
        if (kmh <= ta->target_kmh && ta->have_prev && ta->prev_kmh > ta->target_kmh) {
            double f = (ta->prev_kmh - ta->target_kmh) / (double)(ta->prev_kmh - kmh);
            ta->start_ms = (double)ta->prev_ms + f * (double)(t - ta->prev_ms);
            ta->start_fixed = true;
            ta->samples = 1;
            ta->note = NULL;
            set_state(ta, TA_RUNNING, t);
            if (still) {
                /* From over the target to 0 in one step: stopped in between. */
                double end = (ta->start_ms + (double)t) / 2.0;
                ta->dist_m = area_m(ta->target_kmh, 0.0f, end - ta->start_ms);
                float dist = (float)ta->dist_m;
                finish(ta, end, t);
                ta->result.distance_m = dist;
                return true;
            }
            ta->dist_m = area_m(ta->target_kmh, kmh, (double)t - ta->start_ms);
        }
        return false;
    case TA_RUNNING:
        ta->samples++;
        if (ta->run == TA_RUN_BRAKE) {
            return brake_step(ta, t, kmh);
        }
        if (still) {
            give_up(ta, ta->run == TA_RUN_LAUNCH ? "Stopped before the target speed"
                                                 : "Stopped before the 1/4 mile", t);
            return false;
        }
        return launch_step(ta, t, kmh);
    default:
        return false;
    }
}

/* The car has stood still for a moment, with readings still coming in. */
static bool stood_still(const ta_t *ta, uint64_t now)
{
    return ta->have_last && ta->last_kmh < STILL_KMH && now - ta->last_ms < TA_GAP_MS &&
           now >= ta->still_since && now - ta->still_since >= TA_STILL_MS;
}

bool ta_tick(ta_t *ta, uint64_t now)
{
    switch (ta->state) {
    case TA_STOP_FIRST:
        if (stood_still(ta, now)) {
            set_state(ta, TA_COUNTDOWN, now);
            return true;
        }
        return false;
    case TA_DONE:
        if (now - ta->state_ms >= TA_REARM_MS && stood_still(ta, now)) {
            set_state(ta, TA_COUNTDOWN, now);
            return true;
        }
        return false;
    case TA_COUNTDOWN:
        if (now - ta->state_ms >= TA_COUNTDOWN_MS) {
            set_state(ta, ta->run == TA_RUN_BRAKE ? TA_BUILD_UP : TA_GO, now);
            return true;
        }
        return false;
    case TA_RUNNING:
        if (ta->have_last && now > ta->last_ms && now - ta->last_ms > TA_GAP_MS) {
            give_up(ta, "Speed readings stopped", now);
            return true;
        }
        return false;
    default:
        return false;
    }
}

int ta_countdown(const ta_t *ta, uint64_t now)
{
    if (ta->state != TA_COUNTDOWN) {
        return 0;
    }
    uint64_t gone = now - ta->state_ms;
    if (gone >= TA_COUNTDOWN_MS) {
        return 1;
    }
    return 3 - (int)(gone / 1000u);
}
