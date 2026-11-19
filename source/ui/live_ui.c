#include "live_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "home_top.h"

/* View list: rows centred, 236 px wide, 36 tall on a 40 px pitch. */
#define ROW_X     42
#define ROW_W     236
#define ROW_H     36
#define ROW_PITCH 40
#define ROW_TOP   6
/* HOME Menu Settings' scroll bar at the right edge. */
#define TRACK_X   302
#define TRACK_Y   6
#define TRACK_H   190
/* The Back tab (System Settings' left tab). */
#define TAB_W     128
#define TAB_Y     202

live_rect_t live_list_row(int i, int scroll)
{
    live_rect_t r = { ROW_X, ROW_TOP + i * ROW_PITCH - scroll, ROW_W, ROW_H };
    return r;
}

int live_list_max_scroll(int n)
{
    int content = ROW_TOP + n * ROW_PITCH;
    return content > LIVE_LIST_H ? content - LIVE_LIST_H : 0;
}

int live_list_clamp(int scroll, int n)
{
    int max = live_list_max_scroll(n);
    return scroll < 0 ? 0 : scroll > max ? max : scroll;
}

int live_list_hit(int px, int py, int scroll, int n)
{
    if (py < 0 || py >= LIVE_LIST_H) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        live_rect_t r = live_list_row(i, scroll);
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
            return i;
        }
    }
    return -1;
}

live_rect_t live_list_track(void)
{
    live_rect_t r = { TRACK_X, TRACK_Y, 8, TRACK_H };
    return r;
}

int live_list_grip_y(int scroll, int n)
{
    int max = live_list_max_scroll(n);
    int travel = TRACK_H - LIVE_GRIP_H;
    return TRACK_Y + (max > 0 ? (live_list_clamp(scroll, n) * travel + max / 2) / max : 0);
}

live_rect_t live_view_rect(live_hit_t what)
{
    live_rect_t r = { 0, 0, 0, 0 };
    switch (what) {
    case LIVE_HIT_BACK:   r = (live_rect_t){ 0, TAB_Y, TAB_W, LIVE_SCREEN_H - TAB_Y }; break;
    case LIVE_HIT_PREV:   r = (live_rect_t){ 0, 48, 53, 106 }; break;
    case LIVE_HIT_NEXT:   r = (live_rect_t){ LIVE_SCREEN_W - 53, 48, 53, 106 }; break;
    case LIVE_HIT_STOP:   r = (live_rect_t){ 54, 100, 106, 48 }; break;
    case LIVE_HIT_RECORD: r = (live_rect_t){ 160, 100, 106, 48 }; break;
    case LIVE_HIT_EDIT:   r = (live_rect_t){ 84, 156, 152, 40 }; break;
    default: break;
    }
    return r;
}

static bool in(live_rect_t r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

live_hit_t live_view_hit(int px, int py, bool custom)
{
    static const live_hit_t order[] = { LIVE_HIT_BACK, LIVE_HIT_PREV, LIVE_HIT_NEXT,
                                        LIVE_HIT_STOP, LIVE_HIT_RECORD, LIVE_HIT_EDIT };
    for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++) {
        if (order[i] == LIVE_HIT_EDIT && !custom) {
            continue;
        }
        if (in(live_view_rect(order[i]), px, py)) {
            return order[i];
        }
    }
    return LIVE_HIT_NONE;
}

bool live_back_hit(int px, int py)
{
    return in(live_view_rect(LIVE_HIT_BACK), px, py);
}

static int scale(float v, float lo, float hi, int segments)
{
    if (!(v > lo)) {   /* also NaN */
        return 0;
    }
    int lit = (int)lroundf((v - lo) / (hi - lo) * (float)segments);
    return lit > segments ? segments : lit;
}

int live_tach_lit(float rpm)
{
    return scale(rpm, 0.0f, 8000.0f, LIVE_TACH_SEGMENTS);
}

int live_coolant_lit(float celsius)
{
    return scale(celsius, 40.0f, 130.0f, LIVE_MINI_SEGMENTS);
}

int live_volts_lit(float volts)
{
    return scale(volts, 10.0f, 16.0f, LIVE_MINI_SEGMENTS);
}

float live_rec_pulse(unsigned long long ms)
{
    float t = (float)(ms % 2400ULL) / 2400.0f;
    return 0.6f + 0.4f * cosf(t * 2.0f * 3.14159265f);
}

void live_dash_values(const obd_live_t *lv, obd_units_t units, bool have_odo, double odo_km,
                      live_dash_t *d)
{
    memset(d, 0, sizeof *d);
    bool us = units == OBD_UNITS_US;
    float v = 0;
    d->have_rpm = lv != NULL && obd_live_value(lv, 0x0C, &v);
    d->rpm = d->have_rpm ? v : 0;

    d->speed_unit = obd_unit_label(OBD_UNIT_KMH, units);
    d->have_speed = lv != NULL && obd_live_value(lv, 0x0D, &v);
    if (d->have_speed) {
        long s = lroundf(obd_unit_convert(OBD_UNIT_KMH, v, units));
        snprintf(d->speed, sizeof d->speed, "%3ld", s < 0 ? 0 : s > 999 ? 999 : s);
    } else {
        snprintf(d->speed, sizeof d->speed, "---");
    }

    const char *deg = obd_unit_label(OBD_UNIT_CELSIUS, units);
    d->have_coolant = lv != NULL && obd_live_value(lv, 0x05, &v);
    d->coolant_c = d->have_coolant ? v : 0;
    if (d->have_coolant) {
        snprintf(d->coolant, sizeof d->coolant, "%ld %s",
                 lroundf(obd_unit_convert(OBD_UNIT_CELSIUS, v, units)), deg);
    } else {
        snprintf(d->coolant, sizeof d->coolant, "--- %s", deg);
    }

    d->have_volts = lv != NULL && lv->have_volts;
    d->volts = d->have_volts ? lv->volts : 0;
    if (d->have_volts) {
        snprintf(d->battery, sizeof d->battery, "%.1f V", d->volts);
    } else {
        snprintf(d->battery, sizeof d->battery, "--.- V");
    }

    d->odo_unit = us ? "mi" : "km";
    if (lv != NULL && obd_live_value(lv, 0xA6, &v)) {
        have_odo = true;
        odo_km = v;
    }
    if (have_odo && odo_km >= 0) {
        double shown = us ? odo_km / 1.609344 : odo_km;
        home_group_digits((long)(shown + 0.5), d->odo, sizeof d->odo);
    } else {
        snprintf(d->odo, sizeof d->odo, "---,---");
    }
}
