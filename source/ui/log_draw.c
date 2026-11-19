#include "log_draw.h"

#include <stdio.h>
#include <string.h>

#include "diag_ui.h"
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

/* s cut with "..." to fit w px. */
static const char *fit(const char *s, float w, float scale)
{
    static char buf[72];
    snprintf(buf, sizeof buf, "%s", s);
    if (kit_text_width(buf, scale) <= w) {
        return buf;
    }
    for (size_t n = strlen(buf); n > 0; n--) {
        char probe[68];
        snprintf(probe, sizeof probe, "%.*s...", (int)n, s);
        if (kit_text_width(probe, scale) <= w) {
            snprintf(buf, sizeof buf, "%s", probe);
            return buf;
        }
    }
    return buf;
}

void log_draw_list(const log_row_t *rows, int n, int scroll, int pressed_row, bool grip_pressed,
                   int pressed, const char *empty, const char *notice)
{
    kit_begin(false, 0);
    for (int i = 0; i < n; i++) {
        diag_rect_t r = diag_list_row(i, scroll);
        if (r.y + r.h < 0 || r.y > DIAG_LIST_H) {
            continue;
        }
        bool held = i == pressed_row;
        float y = r.y + (held ? 1 : 0), mid = y + r.h / 2.0f;
        kit_rrect(r.x, y, r.w, r.h, 8, ROW_EDGE);
        kit_rrect(r.x + 1, y + 1, r.w - 2, r.h - 2, 7, held ? ROW_HELD : ROW_FILL);
        float ws = KIT_PX(14), cs = KIT_PX(11), ds = KIT_PX(11.5f);
        kit_text(rows[i].when, r.x + 12, mid - 30 * ws / 2, ws, KIT_INK, -1, true);
        float cx = r.x + 12 + kit_text_width(rows[i].when, ws) + 10;
        float dw = kit_text_width(rows[i].dur, ds);
        kit_text(fit(rows[i].car, r.x + r.w - 12 - dw - 8 - cx, cs), cx, mid - 30 * cs / 2, cs,
                 KIT_INK2, -1, false);
        kit_text(rows[i].dur, r.x + r.w - 12, mid - 30 * ds / 2, ds, KIT_INK, 1, true);
    }
    if (n == 0 && empty != NULL) {
        text_mid(empty, 160, 90, 14, KIT_INK, true);
    }
    kit_grid_band(KIT_BOTTOM_W, DIAG_LIST_H, KIT_H);
    if (diag_list_max_scroll(n) > 0) {
        diag_rect_t t = diag_list_track();
        C2D_Image track = kit_sysui(sysui_scroll_track_idx);
        kit_image_part(track, 0, 0, track.subtex->width, track.subtex->height, t.x, t.y, t.w, t.h);
        kit_image(kit_sysui(grip_pressed ? sysui_scroll_grip_dn_idx : sysui_scroll_grip_idx),
                  t.x + t.w / 2.0f - DIAG_GRIP_W / 2.0f, diag_list_grip_y(scroll, n));
    }
    notice_pill(notice);
    kit_back_tab(pressed == LOG_HIT_BACK);
    kit_right_tab("Y", "Info", pressed == LOG_HIT_INFO);
    kit_end();
}

static void buttons(log_kind_t kind, const char *const *labels, const float *sizes, int pressed)
{
    for (int i = 0; i < log_button_count(kind); i++) {
        log_rect_t r = log_rect(kind, LOG_HIT_BUTTON + i);
        bool p = pressed == LOG_HIT_BUTTON + i;
        bool large = kind == LOG_DRIVE;
        kit_button(large ? sysui_button_large_idx : sysui_button_medium_idx, r.x, r.y, r.w, r.h, p);
        text_mid(labels[i], r.x + r.w / 2.0f, r.y + r.h / 2.0f + (p ? 1 : 0), sizes[i], KIT_INK, true);
    }
}

void log_draw_drive(int pressed, const char *notice)
{
    static const char *const labels[] = { "Graph", "Delete" };
    static const float sizes[] = { 15, 15 };
    kit_begin(false, 0);
    buttons(LOG_DRIVE, labels, sizes, pressed);
    notice_pill(notice);
    kit_back_tab(pressed == LOG_HIT_BACK);
    kit_end();
}

void log_draw_graph(const char *value, const char *showing, int pressed)
{
    static const char *const labels[] = { "<<", "Zoom -", "Zoom +", ">>", "Whole Drive" };
    static const float sizes[] = { 17, 13, 13, 17, 14 };
    kit_begin(false, 0);
    buttons(LOG_GRAPH, labels, sizes, pressed);
    /* System Settings' half circles at the edges: the previous / next value. */
    log_rect_t l = log_rect(LOG_GRAPH, LOG_HIT_PREV), r = log_rect(LOG_GRAPH, LOG_HIT_NEXT);
    bool pl = pressed == LOG_HIT_PREV, pr = pressed == LOG_HIT_NEXT;
    kit_image(kit_sysui(pl ? sysui_side_left_dn_idx : sysui_side_left_idx),
              -(LIVE_SIDE_W - LIVE_SIDE_SHOWN), l.y + (pl ? 1 : 0));
    kit_image(kit_sysui(pr ? sysui_side_right_dn_idx : sysui_side_right_idx),
              KIT_BOTTOM_W - LIVE_SIDE_SHOWN, r.y + (pr ? 1 : 0));
    text_mid(fit(value, 220, KIT_PX(16)), 160, 88, 16, KIT_INK, true);
    text_mid(showing, 160, 108, 11, KIT_INK3, false);
    kit_back_tab(pressed == LOG_HIT_BACK);
    kit_end();
}
