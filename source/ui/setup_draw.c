#include "setup_draw.h"

#include <stdio.h>
#include <string.h>

#include "diag_ui.h"
#include "icons.h"
#include "live_ui.h"
#include "sysui.h"

#define ROW_EDGE KIT_RGB(44, 44, 44)
#define ROW_FILL KIT_RGB(26, 26, 26)
#define ROW_HELD KIT_RGB(40, 40, 40)

static void text_mid(const char *s, float cx, float mid, float px, u32 c, bool bold)
{
    float sc = KIT_PX(px);
    kit_text(s, cx, mid - 30 * sc / 2, sc, c, 0, bold);
}

static void panel(float x, float y, float w, float h)
{
    kit_rrect(x, y, w, h, 6, KIT_PANEL_EDGE);
    kit_rrect(x + 1, y + 1, w - 2, h - 2, 5, KIT_PANEL);
}

/* s cut with "..." to fit w px at scale. */
static const char *fit(const char *s, float w, float scale)
{
    static char buf[80];
    snprintf(buf, sizeof buf, "%s", s);
    if (kit_text_width(buf, scale) <= w) {
        return buf;
    }
    for (size_t n = strlen(buf); n > 0; n--) {
        char probe[76];
        snprintf(probe, sizeof probe, "%.*s...", (int)n, s);
        if (kit_text_width(probe, scale) <= w) {
            snprintf(buf, sizeof buf, "%s", probe);
            return buf;
        }
    }
    return buf;
}

static void notice_pill(const char *notice)
{
    if (notice == NULL || notice[0] == '\0') {
        return;
    }
    float s = KIT_PX(12), w = kit_text_width(notice, s) + 24;
    kit_rrect(160 - w / 2 - 1, 179, w + 2, 22, 7, KIT_PANEL_EDGE);
    kit_rrect(160 - w / 2, 180, w, 20, 6, KIT_RGB(24, 24, 24));
    kit_text(notice, 160, 190 - 30 * s / 2, s, KIT_AMBER, 0, true);
}

/* A System Settings row: the label, its value on the right, and > when it opens something. */
static void row(setup_rect_t r, const char *label, const char *value, bool chevron, bool pressed,
                float value_right)
{
    kit_button(sysui_button_medium_idx, r.x, r.y, r.w, r.h, pressed);
    float mid = r.y + r.h / 2.0f + (pressed ? 1 : 0);
    float ls = KIT_PX(14), vs = KIT_PX(12);
    kit_text(label, r.x + 14, mid - 30 * ls / 2, ls, KIT_INK, -1, true);
    float right = value_right > 0 ? value_right : r.x + r.w - 14;
    if (chevron) {
        text_mid(">", r.x + r.w - 14, mid, 14, KIT_INK3, true);
        right = r.x + r.w - 26;
    }
    if (value != NULL && value[0] != '\0') {
        float left = r.x + 14 + kit_text_width(label, ls) + 12;
        kit_text(fit(value, right - left, vs), right, mid - 30 * vs / 2, vs, KIT_INK2, 1, false);
    }
}

static void stepper(setup_rect_t touch, const char *label, bool pressed)
{
    /* Drawn a little smaller than its touch zone, inside its row. */
    float x = touch.x + 2, y = touch.y + 4, w = touch.w - 4, h = touch.h - 8;
    kit_button(sysui_button_medium_idx, x, y, w, h, pressed);
    text_mid(label, x + w / 2, y + h / 2 + (pressed ? 1 : 0), 17, KIT_INK, true);
}

void setup_draw_bottom(setup_kind_t kind, const setup_bottom_t *b)
{
    kit_begin(false, 0);
    switch (kind) {
    case SET_MENU:
    case SET_TESTS:
        for (int i = 0; i < setup_button_count(kind); i++) {
            setup_rect_t r = setup_rect(kind, SET_HIT_BUTTON + i);
            if (kind == SET_MENU) {
                r.y += 2;
                r.h -= 4;
            }
            bool p = b->pressed == SET_HIT_BUTTON + i || b->busy == i;
            kit_button(kind == SET_MENU ? sysui_button_large_idx : sysui_button_medium_idx, r.x, r.y,
                       r.w, r.h, p);
            text_mid(b->labels[i], r.x + r.w / 2.0f, r.y + r.h / 2.0f + (p ? 1 : 0),
                     kind == SET_MENU ? 15 : 13.5f, b->busy == i ? KIT_AMBER : KIT_INK, true);
        }
        break;
    case SET_WIFI:
        for (int i = 0; i < setup_rows(kind); i++) {
            row(setup_row(kind, i), b->labels[i], b->values[i], b->chevron[i],
                b->pressed == SET_HIT_BUTTON + i, 0);
        }
        break;
    case SET_VEHICLE: {
        static const int row_button[5] = { SET_VEH_MAKE, SET_VEH_MODEL, -1, -1, SET_VEH_VIN };
        for (int i = 0; i < 5; i++) {
            setup_rect_t r = setup_row(kind, i);
            bool p = row_button[i] >= 0 && b->pressed == SET_HIT_BUTTON + row_button[i];
            if (row_button[i] < 0) {   /* Year, Engine: - and + on the row */
                kit_rrect(r.x, r.y, r.w, r.h, 8, ROW_EDGE);
                kit_rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 7, ROW_FILL);
                float ls = KIT_PX(14), vs = KIT_PX(12), mid = r.y + r.h / 2.0f;
                kit_text(b->labels[i], r.x + 14, mid - 30 * ls / 2, ls, KIT_INK, -1, true);
                kit_text(b->values[i], 206, mid - 30 * vs / 2, vs, KIT_INK2, 1, false);
                int down = i == 2 ? SET_VEH_YEAR_DOWN : SET_VEH_ENGINE_DOWN;
                stepper(setup_rect(kind, SET_HIT_BUTTON + down), "-",
                        b->pressed == SET_HIT_BUTTON + down);
                stepper(setup_rect(kind, SET_HIT_BUTTON + down + 1), "+",
                        b->pressed == SET_HIT_BUTTON + down + 1);
            } else {
                row(r, b->labels[i], b->values[i], b->chevron[i], p, 0);
            }
        }
        break;
    }
    case SET_OTHER: {
        kit_text("Units", 160, 28, KIT_PX(11), KIT_INK3, 0, false);
        setup_rect_t us = setup_rect(kind, SET_HIT_US), me = setup_rect(kind, SET_HIT_METRIC);
        size_t tog = b->metric ? sysui_toggle_right_idx : sysui_toggle_left_idx;
        kit_image(kit_sysui(tog), us.x, us.y);
        if (b->pressed == SET_HIT_US || b->pressed == SET_HIT_METRIC) {
            setup_rect_t h = b->pressed == SET_HIT_US ? us : me;
            kit_image_part(kit_sysui(kit_pressed(tog)), h.x - us.x, 0, h.w, h.h, h.x, h.y, h.w, h.h);
        }
        text_mid("US", us.x + us.w / 2.0f, us.y + us.h / 2.0f, 16, KIT_INK, true);
        text_mid("Metric", me.x + me.w / 2.0f, me.y + me.h / 2.0f, 16, KIT_INK, true);
        row(setup_row(kind, 0), b->labels[0], "", true, b->pressed == SET_HIT_BUTTON, 0);
        break;
    }
    case SET_GUIDE: {
        setup_rect_t l = setup_rect(kind, SET_HIT_PREV), r = setup_rect(kind, SET_HIT_NEXT);
        bool pl = b->pressed == SET_HIT_PREV, pr = b->pressed == SET_HIT_NEXT;
        kit_image(kit_sysui(pl ? sysui_side_left_dn_idx : sysui_side_left_idx),
                  -(LIVE_SIDE_W - LIVE_SIDE_SHOWN), l.y + (pl ? 1 : 0));
        kit_image(kit_sysui(pr ? sysui_side_right_dn_idx : sysui_side_right_idx),
                  KIT_BOTTOM_W - LIVE_SIDE_SHOWN, r.y + (pr ? 1 : 0));
        text_mid(b->step, 160, 62, 17, KIT_INK, true);
        text_mid(fit(b->step_title, 220, KIT_PX(12)), 160, 84, 12, KIT_INK3, false);
        if (b->result != NULL && b->result[0] != '\0') {
            text_mid(fit(b->result, 220, KIT_PX(11.5f)), 160, 112, 11.5f, b->result_color, true);
        }
        setup_rect_t c = setup_rect(kind, SET_HIT_BUTTON);
        bool p = b->pressed == SET_HIT_BUTTON;
        kit_button(sysui_button_large_idx, c.x, c.y, c.w, c.h, p);
        text_mid(b->labels[0], c.x + c.w / 2.0f, c.y + c.h / 2.0f + (p ? 1 : 0), 16, KIT_INK, true);
        break;
    }
    case SET_ABOUT: {
        panel(14, 30, 292, 132);
        float s = KIT_PX(11), y = 40;
        const char *p = b->text != NULL ? b->text : "";
        char para[200];
        while (*p != '\0') {
            const char *e = strstr(p, "\n\n");
            size_t n = e != NULL ? (size_t)(e - p) : strlen(p);
            snprintf(para, sizeof para, "%.*s", (int)n, p);
            char lines[4][96];
            int nl = kit_wrap(para, 270, s, lines, 4);
            bool last = e == NULL;
            for (int i = 0; i < nl; i++, y += 14) {
                kit_text(lines[i], 25, y, s, last ? KIT_AMBER : KIT_INK2, -1, last);
            }
            y += 6;
            p += n;
            if (*p != '\0') {
                p += 2;
            }
        }
        break;
    }
    default:
        break;
    }
    notice_pill(b->notice);
    kit_back_tab(b->pressed == SET_HIT_BACK);
    kit_end();
}

void setup_draw_make(const char *const *names, int n, int scroll, int chosen, int pressed_row,
                     bool grip_pressed, bool back_pressed)
{
    kit_begin(false, 0);
    for (int i = 0; i < n; i++) {
        diag_rect_t r = diag_list_row(i, scroll);
        if (r.y + r.h < 0 || r.y > DIAG_LIST_H) {
            continue;
        }
        bool sel = i == chosen, held = i == pressed_row;
        float y = r.y + (held ? 1 : 0), mid = y + r.h / 2.0f;
        kit_rrect(r.x, y, r.w, r.h, 8, sel ? KIT_RED : ROW_EDGE);
        float in = sel ? 2 : 1;
        kit_rrect(r.x + in, y + in, r.w - 2 * in, r.h - 2 * in, 8 - in, held ? ROW_HELD : ROW_FILL);
        float s = KIT_PX(13.5f);
        kit_text(names[i], r.x + 14, mid - 30 * s / 2, s, sel ? KIT_RED_HI : KIT_INK, -1, true);
        if (i == 0) {
            float ds = KIT_PX(11);
            kit_text("from the VIN", r.x + r.w - 12, mid - 30 * ds / 2, ds, KIT_INK3, 1, false);
        }
    }
    kit_grid_band(KIT_BOTTOM_W, DIAG_LIST_H, KIT_H);
    if (diag_list_max_scroll(n) > 0) {
        diag_rect_t t = diag_list_track();
        C2D_Image track = kit_sysui(sysui_scroll_track_idx);
        kit_image_part(track, 0, 0, track.subtex->width, track.subtex->height, t.x, t.y, t.w, t.h);
        kit_image(kit_sysui(grip_pressed ? sysui_scroll_grip_dn_idx : sysui_scroll_grip_idx),
                  t.x + t.w / 2.0f - DIAG_GRIP_W / 2.0f, diag_list_grip_y(scroll, n));
    }
    kit_back_tab(back_pressed);
    kit_end();
}

static void top_begin(const char *title, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icons_setup_idx, title);
}

void setup_draw_wifi_top(const char *const *lines, int n, bool ok, const char *progress,
                         const kit_status_t *s)
{
    top_begin("WI-FI", s);
    float sc = KIT_PX(12);
    if (progress != NULL && progress[0] != '\0') {
        panel(17, 67, 366, 40);
        text_mid(progress, 200, 87, 13, KIT_AMBER, true);
        kit_end();
        return;
    }
    if (n == 0) {
        panel(17, 67, 366, 64);
        kit_text("Check & Connect tries each step and says where it stops.", 27, 76, sc, KIT_INK2,
                 -1, false);
        kit_text("New to this? Start with the Connection Guide.", 27, 96, sc, KIT_INK2, -1, false);
        kit_end();
        return;
    }
    float h = 22 + n * 17 + 8;
    panel(17, 67, 366, h);
    text_mid(lines[0], 200, 82, 15, ok ? KIT_GREEN : KIT_AMBER, true);
    for (int i = 1; i < n; i++) {
        kit_text(lines[i], 27, 96 + (i - 1) * 17, sc, KIT_INK2, -1, false);
    }
    kit_end();
}

void setup_draw_guide_top(const char *heading, const char *const *lines, int n,
                          const kit_status_t *s)
{
    top_begin("WI-FI GUIDE", s);
    panel(17, 64, 366, 168);
    kit_text(heading, 27, 70, KIT_PX(12.5f), KIT_INK, -1, true);
    float sc = KIT_PX(11);
    for (int i = 0; i < n && i < 11; i++) {
        kit_text(lines[i], 27, 90 + i * 12.6f, sc, KIT_INK2, -1, false);
    }
    kit_end();
}

void setup_draw_log_top(const char *const *lines, int n, bool scrolled, const kit_status_t *s)
{
    top_begin("TESTS", s);
    panel(17, 62, 366, 172);
    float sc = KIT_PX(9.5f);
    for (int i = 0; i < n; i++) {
        kit_text(lines[i], 24, 66 + i * 12.2f, sc, KIT_INK2, -1, false);
    }
    if (scrolled) {
        kit_rrect(250, 64, 130, 16, 5, KIT_TAG);
        kit_text("Scrolled back: R to follow", 315, 65, KIT_PX(9), KIT_AMBER, 0, true);
    }
    kit_end();
}
