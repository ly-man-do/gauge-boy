#include "live_draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "sysui.h"

#define LIT       KIT_RGB(242, 244, 245)
#define LIT_RED   KIT_RGB(255, 59, 48)
#define DARK      KIT_RGB(44, 44, 44)
#define DARK_RED  KIT_RGB(90, 21, 18)
#define TICK      KIT_RGB(140, 140, 140)
#define LABEL     KIT_RGB(200, 200, 200)
#define REC_RED   KIT_RGB(255, 90, 78)

#define DEG(d) ((d) * 3.14159265f / 180.0f)

/* Text whose baseline is at y (the mockup's SVG text), size px. */
static void text_at(const char *s, float x, float y, float px, u32 c, int align)
{
    kit_text(s, x, y - px * 1.02f, KIT_PX(px), c, align, true);
}

static u32 seg_color(int i, int lit, int red_from)
{
    bool red = i >= red_from;
    return i < lit ? (red ? LIT_RED : LIT) : (red ? DARK_RED : DARK);
}

/* ---------- the Dashboard ---------- */

/* The tach: a flattened quarter ellipse from the lower left over to the upper right. */
#define TACH_CX   262.0f
#define TACH_CY   225.0f
#define TACH_RX   236.0f
#define TACH_RY   178.0f
#define TACH_BAND 20.0f
#define TACH_A0   180.0f
#define TACH_A1   292.0f

static void tach_point(float deg, float inset, float *x, float *y)
{
    *x = TACH_CX + (TACH_RX - inset) * cosf(DEG(deg));
    *y = TACH_CY + (TACH_RY - inset) * sinf(DEG(deg));
}

static void tach(float rpm)
{
    int lit = live_tach_lit(rpm);
    float span = TACH_A1 - TACH_A0;
    for (int i = 0; i < LIVE_TACH_SEGMENTS; i++) {
        float t0 = TACH_A0 + span * i / LIVE_TACH_SEGMENTS + 0.55f;
        float t1 = TACH_A0 + span * (i + 1) / LIVE_TACH_SEGMENTS - 0.55f;
        float x0, y0, x1, y1, x2, y2, x3, y3;
        tach_point(t0, 0, &x0, &y0);
        tach_point(t1, 0, &x1, &y1);
        tach_point(t1, TACH_BAND, &x2, &y2);
        tach_point(t0, TACH_BAND, &x3, &y3);
        kit_quad(x0, y0, x1, y1, x2, y2, x3, y3, seg_color(i, lit, LIVE_TACH_RED));
    }
    for (int k = 0; k <= 8; k++) {
        float t = TACH_A0 + span * k / 8;
        bool red = k >= 7;
        /* A 2 px tick inside the band, and its number. */
        float ax, ay, bx, by;
        tach_point(t, TACH_BAND + 2, &ax, &ay);
        tach_point(t, TACH_BAND + 8, &bx, &by);
        float nx = -sinf(DEG(t)), ny = cosf(DEG(t));
        kit_quad(ax - nx, ay - ny, ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny,
                 red ? LIT_RED : TICK);
        char num[2] = { (char)('0' + k), '\0' };
        float x, y;
        tach_point(t, TACH_BAND + 22, &x, &y);
        kit_text(num, x, y - 30 * KIT_PX(18) / 2, KIT_PX(18), red ? LIT_RED : LIT, 0, true);
    }
    text_at("x1000 r/min", 86, 230, 10, LABEL, -1);   /* left of the odometer */
}

/* A small arc gauge (coolant, battery) centred on (gx, gy). */
static void mini(float gx, float gy, int lit, int red_from)
{
    for (int i = 0; i < LIVE_MINI_SEGMENTS; i++) {
        float t0 = DEG(200 + 140.0f * i / LIVE_MINI_SEGMENTS + 1);
        float t1 = DEG(200 + 140.0f * (i + 1) / LIVE_MINI_SEGMENTS - 1);
        kit_quad(gx + 30 * cosf(t0), gy + 30 * sinf(t0), gx + 30 * cosf(t1), gy + 30 * sinf(t1),
                 gx + 22 * cosf(t1), gy + 22 * sinf(t1), gx + 22 * cosf(t0), gy + 22 * sinf(t0),
                 seg_color(i, lit, red_from));
    }
}

void live_draw_dash(const live_dash_t *d, bool cel_known, bool cel_on, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);

    tach(d->have_rpm ? d->rpm : 0);

    /* Speed: big LCD digits, the unit beside them. */
    kit_lcd(d->speed, 148, 124, 56, LIT);
    text_at(d->speed_unit, 286, 178, 14, KIT_INK3, -1);

    /* Coolant, C to H. */
    mini(360, 132, d->have_coolant ? live_coolant_lit(d->coolant_c) : 0, LIVE_COOLANT_RED);
    text_at("C", 328, 138, 10, KIT_INK3, -1);
    text_at("H", 386, 138, 10, LIT_RED, -1);
    text_at(d->coolant, 360, 148, 11, LABEL, 0);

    /* Battery, 10 to 16 V. */
    mini(360, 206, d->have_volts ? live_volts_lit(d->volts) : 0, LIVE_MINI_SEGMENTS);
    text_at("10", 326, 212, 9, KIT_INK3, -1);
    text_at("16", 384, 212, 9, KIT_INK3, -1);
    text_at(d->battery, 360, 222, 11, LABEL, 0);

    /* The CEL: the engine icon, dim when off (or unknown), amber when on. */
    kit_image(kit_icon(cel_known && cel_on ? icons_cel_on_idx : icons_cel_off_idx), 94, 168);

    /* The odometer at the bottom, right of the tach's label. */
    float oh = 20, uw = kit_text_width(d->odo_unit, KIT_PX(11));
    float ow = kit_lcd_width(d->odo, oh);
    float ox = 226 - (ow + 4 + uw) / 2;
    if (ox < 158) {
        ox = 158;
    }
    kit_lcd(d->odo, ox, 212, oh, LIT);
    text_at(d->odo_unit, ox + ow + 4, 231, 11, KIT_INK3, -1);

    kit_end();
}

/* ---------- the view list ---------- */

void live_draw_list(const char *const *names, int n, int scroll, int pressed, bool grip_pressed,
                    bool back_pressed)
{
    kit_begin(false, 0);
    for (int i = 0; i < n; i++) {
        live_rect_t r = live_list_row(i, scroll);
        if (r.y + r.h < 0 || r.y > LIVE_LIST_H) {
            continue;
        }
        bool p = i == pressed;
        kit_button(sysui_button_medium_idx, r.x, r.y, r.w, r.h, p);
        float s = KIT_PX(14);
        kit_text(names[i], r.x + r.w / 2.0f, r.y + (p ? 1 : 0) + r.h / 2.0f - 30 * s / 2, s, KIT_INK, 0,
                 true);
    }
    /* Rows that scrolled past the list's bottom edge go under the background. */
    kit_grid_band(KIT_BOTTOM_W, LIVE_LIST_H, KIT_H);

    live_rect_t t = live_list_track();
    C2D_Image track = kit_sysui(sysui_scroll_track_idx);
    kit_image_part(track, 0, 0, track.subtex->width, track.subtex->height, t.x, t.y, t.w, t.h);
    if (live_list_max_scroll(n) > 0) {
        float gx = t.x + t.w / 2.0f - LIVE_GRIP_W / 2.0f;
        kit_image(kit_sysui(grip_pressed ? sysui_scroll_grip_dn_idx : sysui_scroll_grip_idx), gx,
                  live_list_grip_y(scroll, n));
    }
    kit_back_tab(back_pressed);
    kit_end();
}

/* ---------- a PID view ---------- */

void live_draw_view_top(const char *title, const live_row_t *rows, int n, bool connected,
                        const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icons_live_idx, title);

    kit_rrect(17, 67, 366, 158, 6, KIT_PANEL_EDGE);
    kit_rrect(18, 68, 364, 156, 5, KIT_PANEL);
    if (n <= 0) {
        text_at(connected ? "Waiting for the first values..." : "Not connected: connect on HOME",
                200, 152, 13, KIT_INK3, 0);
        kit_end();
        return;
    }

    /* Eight rows a column; past eight, two narrower columns split evenly (and a count
     * of the rest). */
    bool two = n > 8;
    int shown = n > 16 ? 16 : n;
    int per_col = two ? (shown + 1) / 2 : 8;
    float col_w = two ? 178 : 356, tag_w = two ? 76 : 108, val_w = two ? 46 : 64;
    for (int i = 0; i < shown; i++) {
        int col = i / per_col, row = i % per_col;
        float x = 27 + col * col_w, top = 74 + row * 18.2f, mid = top + 9;
        kit_rrect(x, mid - 7, tag_w, 14, 4, KIT_TAG);
        char tag[24];
        snprintf(tag, sizeof tag, "%s", rows[i].label);
        /* Long labels lose letters at the end to fit their tag. */
        while (strlen(tag) > 1 && kit_text_width(tag, KIT_PX(10)) > tag_w - 8) {
            tag[strlen(tag) - 1] = '\0';
        }
        kit_text(tag, x + 6, mid - 6.5f, KIT_PX(10), KIT_TAG_INK, -1, true);
        float vx = x + tag_w + 8 + val_w;
        kit_text(rows[i].value, vx, mid - 9, KIT_PX(15), KIT_INK, 1, true);
        kit_text(rows[i].unit, vx + 8, mid - 6.5f, KIT_PX(11.5f), KIT_INK3, -1, false);
    }
    if (n > shown) {
        char more[24];
        snprintf(more, sizeof more, "+%d more", n - shown);
        text_at(more, 200, 236, 10.5f, KIT_INK3, 0);
    }
    kit_end();
}

void live_draw_view_bottom(const live_controls_t *c)
{
    kit_begin(false, 0);

    char line[40];
    snprintf(line, sizeof line, "%d PID%s, %.1f a second", c->pids, c->pids == 1 ? "" : "s", c->rate);
    kit_text(line, KIT_BOTTOM_W / 2.0f, 8, KIT_PX(11), KIT_INK3, 0, false);

    /* System Settings' half-circle arrows at the edges: the previous / next view. */
    live_rect_t l = live_view_rect(LIVE_HIT_PREV), r = live_view_rect(LIVE_HIT_NEXT);
    bool pl = c->pressed == LIVE_HIT_PREV, pr = c->pressed == LIVE_HIT_NEXT;
    kit_image(kit_sysui(pl ? sysui_side_left_dn_idx : sysui_side_left_idx),
              -(LIVE_SIDE_W - LIVE_SIDE_SHOWN), l.y + (pl ? 1 : 0));
    kit_image(kit_sysui(pr ? sysui_side_right_dn_idx : sysui_side_right_idx),
              LIVE_SCREEN_W - LIVE_SIDE_SHOWN, r.y + (pr ? 1 : 0));

    /* "● REC m:ss" above the toggle while recording, the dot pulsing slowly. */
    if (c->recording) {
        unsigned long secs = (unsigned long)(c->rec_ms / 1000ULL);
        char rec[24];
        snprintf(rec, sizeof rec, "REC %lu:%02lu", secs / 60, secs % 60);
        float s = KIT_PX(17), tw = kit_text_width(rec, s);
        float x = (KIT_BOTTOM_W - (12 + 6 + tw)) / 2, y = 66;
        float p = live_rec_pulse(c->rec_ms);   /* 0.2 - 1 */
        float rad = 6 * (0.8f + 0.2f * (p - 0.2f) / 0.8f);
        kit_circle(x + 6, y + 30 * s / 2, rad, KIT_RGBA(255, 90, 78, (int)(p * 255)));
        kit_text(rec, x + 18, y, s, REC_RED, -1, true);
    }

    /* HOME Menu Settings' OFF / ON toggle as Stop | Record: the chosen half red with its notch. */
    live_rect_t st = live_view_rect(LIVE_HIT_STOP), rc = live_view_rect(LIVE_HIT_RECORD);
    size_t tog = c->recording ? sysui_toggle_right_idx : sysui_toggle_left_idx;
    kit_image(kit_sysui(tog), st.x, st.y);
    /* The held half: the same half of the darker toggle over it. */
    if (c->pressed == LIVE_HIT_STOP || c->pressed == LIVE_HIT_RECORD) {
        live_rect_t h = c->pressed == LIVE_HIT_STOP ? st : rc;
        kit_image_part(kit_sysui(kit_pressed(tog)), h.x - st.x, 0, h.w, h.h, h.x, h.y, h.w, h.h);
    }
    float ls = KIT_PX(16);
    kit_text("Stop", st.x + st.w / 2.0f, st.y + st.h / 2.0f - 30 * ls / 2, ls, KIT_INK, 0, true);
    kit_text("Record", rc.x + rc.w / 2.0f, rc.y + rc.h / 2.0f - 30 * ls / 2, ls, KIT_INK, 0, true);

    if (c->custom) {
        live_rect_t e = live_view_rect(LIVE_HIT_EDIT);
        bool p = c->pressed == LIVE_HIT_EDIT;
        kit_button(sysui_button_medium_idx, e.x, e.y, e.w, e.h, p);
        float s = KIT_PX(14);
        kit_text("Edit PIDs", e.x + e.w / 2.0f, e.y + (p ? 1 : 0) + e.h / 2.0f - 30 * s / 2, s, KIT_INK, 0,
                 true);
    }

    kit_back_tab(c->pressed == LIVE_HIT_BACK);
    kit_end();
}
