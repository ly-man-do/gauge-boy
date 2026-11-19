#include "trim_draw.h"

#include <stdio.h>

#include "icons.h"
#include "live_ui.h"
#include "sysui.h"

#define GREEN    KIT_GREEN
#define AMBER    KIT_AMBER
#define RED      KIT_RED_HI
#define REC_RED  KIT_RGB(255, 90, 78)
#define TICK     KIT_RGB(106, 106, 106)
#define SCALE    KIT_RGB(122, 122, 122)

/* A bank's panel and its bars, from the mockup: bars 218 px wide from 66 px in. */
#define PANEL_X  17
#define PANEL_W  366
#define PANEL_H  70
#define BAR_X    (PANEL_X + 66)
#define BAR_W    218.0f

static u32 zone_color(trim_zone_t z)
{
    return z == TRIM_ZONE_GREEN ? GREEN : z == TRIM_ZONE_AMBER ? AMBER : RED;
}

/* An unlit segment: its zone colour, dim. */
static u32 dim(trim_zone_t z)
{
    return z == TRIM_ZONE_GREEN ? KIT_RGB(17, 45, 22) : z == TRIM_ZONE_AMBER ? KIT_RGB(53, 36, 9)
                                                                             : KIT_RGB(56, 20, 17);
}

static void bar(float v, bool have, float y, float h)
{
    float sw = BAR_W / TRIM_SEGS;
    for (int i = 0; i < TRIM_SEGS; i++) {
        float x = BAR_X + i * sw + 0.75f, w = sw - 1.5f;
        trim_zone_t z = trim_seg_zone(i);
        int st = trim_seg(i, v, have);
        if (st == 2) {   /* past the range: an arrow pointing off the end */
            if (i > 0) {
                kit_quad(x, y, x + w, y + h / 2, x + w, y + h / 2, x, y + h, zone_color(z));
            } else {
                kit_quad(x + w, y, x, y + h / 2, x, y + h / 2, x + w, y + h, zone_color(z));
            }
        } else {
            kit_rrect(x, y, w, h, 1.5f, st == 1 ? zone_color(z) : dim(z));
        }
    }
}

static void bank(float y, int n, const trim_bank_t *b)
{
    kit_rrect(PANEL_X, y, PANEL_W, PANEL_H, 6, KIT_PANEL_EDGE);
    kit_rrect(PANEL_X + 1, y + 1, PANEL_W - 2, PANEL_H - 2, 5, KIT_PANEL);
    char tag[12];
    snprintf(tag, sizeof tag, "BANK %d", n);
    kit_vtag(tag, PANEL_X + 15.5f, y + 33.5f, 58, 15);

    float ls = KIT_PX(11);
    kit_text("Long", PANEL_X + 30, y + 9, ls, KIT_INK3, -1, false);
    kit_text("Short", PANEL_X + 30, y + 32, ls, KIT_INK3, -1, false);
    bar(b->long_term, b->have_long, y + 7, 17);
    bar(b->short_term, b->have_short, y + 30, 17);

    float cx = BAR_X + BAR_W / 2;
    kit_rect(cx - 0.75f, y + 4, 1.5f, 47, KIT_INK);
    static const int ticks[] = { -20, -10, 10, 20 };
    for (int i = 0; i < 4; i++) {
        float x = BAR_X + (ticks[i] + 25) / 50.0f * BAR_W;
        kit_rect(x - 0.5f, y + 50, 1, 3, TICK);
    }
    static const struct { int p; const char *t; } labels[] = {
        { -25, "-25" }, { -10, "-10" }, { 0, "0" }, { 10, "+10" }, { 25, "+25" },
    };
    for (int i = 0; i < 5; i++) {
        kit_text(labels[i].t, BAR_X + (labels[i].p + 25) / 50.0f * BAR_W, y + 53, KIT_PX(8.5f), SCALE,
                 0, true);
    }

    char v[16];
    float vs = KIT_PX(13), vx = BAR_X + BAR_W + 10;
    trim_value_text(b->have_long, b->long_term, v, sizeof v);
    kit_text(v, vx, y + 8, vs, b->have_long ? zone_color(trim_zone(b->long_term)) : KIT_INK3, -1, true);
    trim_value_text(b->have_short, b->short_term, v, sizeof v);
    kit_text(v, vx, y + 31, vs, b->have_short ? zone_color(trim_zone(b->short_term)) : KIT_INK3, -1,
             true);
}

void trim_draw_top(const trim_state_t *t, const char *engine, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icons_trim_idx, "TRIM");
    if (t->banks >= 2) {
        bank(66, 1, &t->bank[0]);
        bank(142, 2, &t->bank[1]);
    } else {
        bank(100, 1, &t->bank[0]);   /* inline engines; unlit when the car reports none */
    }
    kit_text(engine, KIT_TOP_W / 2.0f, 219, KIT_PX(12), KIT_INK2, 0, true);
    kit_end();
}

void trim_draw_bottom(const trim_controls_t *c)
{
    kit_begin(false, 0);

    char line[48];
    if (c->connected) {
        snprintf(line, sizeof line, "%d PID%s, %.1f a second", c->pids, c->pids == 1 ? "" : "s",
                 (double)c->rate);
    } else {
        snprintf(line, sizeof line, "Not connected: connect on HOME");
    }
    kit_text(line, KIT_BOTTOM_W / 2.0f, 8, KIT_PX(11), KIT_INK3, 0, false);

    /* The hints (hints, not diagnoses): up to three lines. */
    char lines[3][96];
    int n = 0;
    for (int i = 0; i < c->nhints && n < 3; i++) {
        n += kit_wrap(c->hints[i], 288, KIT_PX(12.5f), lines + n, 3 - n);
    }
    float hs = KIT_PX(12.5f);
    for (int i = 0; i < n; i++) {
        kit_text(lines[i], KIT_BOTTOM_W / 2.0f, 28 + i * 15, hs, c->hints_ok ? GREEN : AMBER, 0, true);
    }
    if (n > 0) {
        kit_text("Hints, not diagnoses.", KIT_BOTTOM_W / 2.0f, 30 + n * 15, KIT_PX(9), SCALE, 0, false);
    }

    if (c->recording) {
        unsigned long secs = (unsigned long)(c->rec_ms / 1000ULL);
        char rec[24];
        snprintf(rec, sizeof rec, "REC %lu:%02lu", secs / 60, secs % 60);
        float s = KIT_PX(17), tw = kit_text_width(rec, s);
        float x = (KIT_BOTTOM_W - (12 + 6 + tw)) / 2, y = 80;
        float p = live_rec_pulse(c->rec_ms);
        float rad = 6 * (0.8f + 0.2f * (p - 0.2f) / 0.8f);
        kit_circle(x + 6, y + 30 * s / 2, rad, KIT_RGBA(255, 90, 78, (int)(p * 255)));
        kit_text(rec, x + 18, y, s, REC_RED, -1, true);
    }

    trim_rect_t st = trim_rect(TRIM_HIT_STOP), rc = trim_rect(TRIM_HIT_RECORD);
    size_t tog = c->recording ? sysui_toggle_right_idx : sysui_toggle_left_idx;
    kit_image(kit_sysui(tog), st.x, st.y);
    if (c->pressed == TRIM_HIT_STOP || c->pressed == TRIM_HIT_RECORD) {
        trim_rect_t h = c->pressed == TRIM_HIT_STOP ? st : rc;
        kit_image_part(kit_sysui(kit_pressed(tog)), h.x - st.x, 0, h.w, h.h, h.x, h.y, h.w, h.h);
    }
    float ls = KIT_PX(16);
    kit_text("Stop", st.x + st.w / 2.0f, st.y + st.h / 2.0f - 30 * ls / 2, ls, KIT_INK, 0, true);
    kit_text("Record", rc.x + rc.w / 2.0f, rc.y + rc.h / 2.0f - 30 * ls / 2, ls, KIT_INK, 0, true);

    kit_back_tab(c->pressed == TRIM_HIT_BACK);
    kit_right_tab("Y", "Info", c->pressed == TRIM_HIT_INFO);
    kit_end();
}

void trim_draw_info(bool back_pressed)
{
    kit_begin(false, 0);
    kit_rrect(14, 8, 292, 186, 6, KIT_PANEL_EDGE);
    kit_rrect(15, 9, 290, 184, 5, KIT_PANEL);
    float s = KIT_PX(10.5f), lh = 13.6f, x = 25, y = 15;
    char lines[12][96];
    int n = kit_wrap(trim_info_before(), 270, s, lines, 12);
    for (int i = 0; i < n; i++, y += lh) {
        kit_text(lines[i], x, y, s, KIT_INK2, -1, false);
    }
    y += 5;
    static const struct { u32 c; const char *t; } zones[] = {
        { GREEN, "Green within 10 %" }, { AMBER, "Amber 10 to 20 %" }, { RED, "Red beyond 20 %" },
    };
    for (int i = 0; i < 3; i++, y += lh) {
        kit_rrect(x, y + 3, 9, 9, 3, zones[i].c);
        kit_text(zones[i].t, x + 14, y, s, KIT_INK2, -1, false);
    }
    y += 5;
    n = kit_wrap(trim_info_after(), 270, s, lines, 12);
    for (int i = 0; i < n; i++, y += lh) {
        kit_text(lines[i], x, y, s, KIT_INK2, -1, false);
    }
    kit_back_tab(back_pressed);
    kit_end();
}
