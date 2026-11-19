#include "trim_ui.h"

#include <stdio.h>

#include "obd/obd_pid.h"

#define TAB_W 128
#define TAB_Y 202

trim_rect_t trim_rect(trim_hit_t what)
{
    trim_rect_t r = { 0, 0, 0, 0 };
    switch (what) {
    case TRIM_HIT_BACK:   r = (trim_rect_t){ 0, TAB_Y, TAB_W, TRIM_SCREEN_H - TAB_Y }; break;
    case TRIM_HIT_INFO:   r = (trim_rect_t){ TRIM_SCREEN_W - TAB_W, TAB_Y, TAB_W, TRIM_SCREEN_H - TAB_Y }; break;
    case TRIM_HIT_STOP:   r = (trim_rect_t){ 54, 108, 106, 48 }; break;
    case TRIM_HIT_RECORD: r = (trim_rect_t){ 160, 108, 106, 48 }; break;
    default: break;
    }
    return r;
}

static bool in(trim_rect_t r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

trim_hit_t trim_hit(int px, int py, bool info_page)
{
    static const trim_hit_t order[] = { TRIM_HIT_BACK, TRIM_HIT_INFO, TRIM_HIT_STOP, TRIM_HIT_RECORD };
    for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++) {
        if (info_page && order[i] != TRIM_HIT_BACK) {
            continue;
        }
        if (in(trim_rect(order[i]), px, py)) {
            return order[i];
        }
    }
    return TRIM_HIT_NONE;
}

int trim_seg(int i, float v, bool have)
{
    if (!have || i < 0 || i >= TRIM_SEGS || !(v == v)) {
        return 0;
    }
    if ((i == TRIM_SEGS - 1 && v > TRIM_BAR_RANGE) || (i == 0 && v < -TRIM_BAR_RANGE)) {
        return 2;
    }
    float mid = -TRIM_BAR_RANGE + 2.0f * (float)i + 1.0f;   /* this segment's middle, % */
    return (mid > 0 && mid <= v + 1.0f) || (mid < 0 && mid >= v - 1.0f) ? 1 : 0;
}

trim_zone_t trim_seg_zone(int i)
{
    return trim_zone(-TRIM_BAR_RANGE + 2.0f * (float)i + 1.0f);
}

void trim_value_text(bool have, float v, char *out, size_t len)
{
    if (!have || !(v == v)) {
        snprintf(out, len, "-- %%");
        return;
    }
    if (v > -0.05f && v < 0.05f) {
        v = 0.0f;   /* no "-0.0" */
    }
    snprintf(out, len, "%s%.1f %%", v > 0 ? "+" : "", (double)v);
}

void trim_engine_text(const trim_state_t *s, bool have_load, float load, char *out, size_t len)
{
    if (!s->have_rpm) {
        snprintf(out, len, "Waiting for RPM");
        return;
    }
    char rpm[24];
    obd_format_grouped((long long)(s->rpm + 0.5f), rpm, sizeof rpm);
    const char *state = s->rpm < 400.0f              ? "not running"
                      : trim_at_idle(s)              ? "at idle"
                      : s->have_speed && s->speed_kmh < 1.0f ? "parked"
                      : s->have_speed                ? "driving"
                                                     : "";
    char load_text[24] = "";
    if (have_load) {
        snprintf(load_text, sizeof load_text, ", %.0f %% load", (double)load);
    }
    snprintf(out, len, "%s rpm%s%s%s", rpm, load_text, state[0] ? ", " : "", state);
}

bool trim_all_normal(const trim_state_t *s)
{
    bool any = false;
    for (int b = 0; b < s->banks && b < 2; b++) {
        float t;
        if (trim_total(&s->bank[b], &t)) {
            any = true;
            if (trim_zone(t) != TRIM_ZONE_GREEN) {
                return false;
            }
        }
    }
    return any;
}

const char *trim_info_before(void)
{
    return "Fuel trims: how much fuel the engine computer adds (+) or takes away (-).\n\n"
           "Short: right now. Long: learned. Total: both together.";
}

const char *trim_info_after(void)
{
    return "Bars run from -25 to +25 %; an arrow at the end means beyond that. Check trims warm, "
           "at idle, then at a steady 2500 rpm (parked). REC saves the trims to the SD card (LOG "
           "lists it, GRAPH replays it).";
}
