#include "garage_draw.h"

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

static const char *fit(const char *s, float w, float scale)
{
    static char buf[88];
    snprintf(buf, sizeof buf, "%s", s);
    if (kit_text_width(buf, scale) <= w) {
        return buf;
    }
    for (size_t n = strlen(buf); n > 0; n--) {
        char probe[84];
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

/* "Label value" with the label grey and the value bold. */
static void kv(const char *label, const char *value, float x, float y)
{
    float ls = KIT_PX(10.5f), vs = KIT_PX(12.5f);
    kit_text(label, x, y + 2, ls, KIT_INK3, -1, false);
    kit_text(value, x + kit_text_width(label, ls) + 5, y, vs, KIT_INK, -1, true);
}

static void top_begin(const char *title, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icons_garage_idx, title);
}

void garage_draw_card_top(const garage_card_t *c, const kit_status_t *s)
{
    top_begin("GARAGE", s);
    if (!c->have) {
        panel(17, 66, 366, 60);
        text_mid(c->msg, 200, 96, 13, KIT_INK2, true);
        kit_end();
        return;
    }
    panel(17, 66, 366, 96);
    /* The photo in a white frame (the Garage icon until there is one). */
    garage_rect_t f = garage_photo_frame();
    kit_rrect(f.x - 4, f.y - 4, f.w + 8, f.h + 8, 8, c->photo != NULL ? KIT_INK : KIT_PANEL_EDGE);
    if (c->photo != NULL) {
        kit_image_part(*c->photo, 0, 0, c->photo->subtex->width, c->photo->subtex->height, f.x, f.y,
                       f.w, f.h);
    } else {
        kit_rrect(f.x, f.y, f.w, f.h, 6, KIT_RGB(11, 11, 11));
        C2D_Image icon = kit_icon(icons_garage_idx);
        kit_image(icon, f.x + f.w / 2.0f - icon.subtex->width / 2.0f, f.y + 18);
        kit_text("No photo yet", f.x + f.w / 2.0f, f.y + 48, KIT_PX(9.5f), KIT_INK3, 0, false);
    }
    float x = 144;
    kit_text(fit(c->name, 234, KIT_PX(21)), x, 74, KIT_PX(21), KIT_INK, -1, true);
    if (c->title[0] != '\0') {
        kit_text(fit(c->title, 234, KIT_PX(13)), x, 103, KIT_PX(13), KIT_INK2, -1, true);
    }
    kit_text(fit(c->vin, 234, KIT_PX(11)), x, 124, KIT_PX(11), KIT_INK3, -1, false);

    panel(17, 168, 366, 58);
    kit_text("Odometer", 27, 172, KIT_PX(10.5f), KIT_INK3, -1, false);
    if (c->odo[0] != '\0') {
        float w = kit_lcd(c->odo, 27, 190, 26, KIT_INK);
        kit_text(c->unit, 27 + w + 5, 205, KIT_PX(10.5f), KIT_INK3, -1, false);
    } else {
        kit_text("not read yet", 27, 196, KIT_PX(12), KIT_INK3, -1, false);
    }
    kv("Logged", c->logged, 200, 172);
    kit_text(c->drives, 200 + kit_text_width("Logged", KIT_PX(10.5f)) + 5 +
                            kit_text_width(c->logged, KIT_PX(12.5f)) + 5,
             174, KIT_PX(10.5f), KIT_INK3, -1, false);
    for (int i = 0; i < 2; i++) {
        if (c->best[i] != NULL) {
            kit_text(fit(c->best[i], 176, KIT_PX(11)), 200, 192 + i * 16, KIT_PX(11), KIT_INK2, -1, true);
        }
    }
    if (c->msg[0] != '\0') {
        text_mid(c->msg, 200, 233, 10, KIT_AMBER, false);
    }
    kit_end();
}

void garage_draw_photo_top(const C2D_Image *img, bool live, const char *caption)
{
    kit_begin(true, 0);
    if (img != NULL) {
        kit_image_part(*img, 0, 0, img->subtex->width, img->subtex->height, 0, 0, KIT_TOP_W, KIT_H);
    } else {
        text_mid("Starting the camera...", KIT_TOP_W / 2.0f, KIT_H / 2.0f, 14, KIT_INK2, true);
    }
    if (live) {   /* corner guides around the 4:3 part that becomes the photo */
        const float x0 = 40, x1 = 360, y0 = 8, y1 = 232, l = 26, t = 3;
        u32 c = KIT_INK;
        kit_rect(x0, y0, l, t, c);
        kit_rect(x0, y0, t, l, c);
        kit_rect(x1 - l, y0, l, t, c);
        kit_rect(x1 - t, y0, t, l, c);
        kit_rect(x0, y1 - t, l, t, c);
        kit_rect(x0, y1 - l, t, l, c);
        kit_rect(x1 - l, y1 - t, l, t, c);
        kit_rect(x1 - t, y1 - l, t, l, c);
    }
    if (caption != NULL && caption[0] != '\0') {
        float s = KIT_PX(11), w = kit_text_width(caption, s) + 20;
        kit_rrect(KIT_TOP_W / 2.0f - w / 2, 12, w, 18, 6, KIT_RGBA(0, 0, 0, 0xA0));
        kit_text(caption, KIT_TOP_W / 2.0f, 21 - 30 * s / 2, s, KIT_INK, 0, true);
    }
    kit_end();
}

void garage_draw_trip_top(const garage_trip_t *t, const kit_status_t *s)
{
    top_begin("TRIP METER", s);
    panel(17, 66, 366, 118);
    kit_text(fit(t->car, 300, KIT_PX(10.5f)), 27, 72, KIT_PX(10.5f), KIT_INK3, -1, false);
    if (t->live) {
        kit_text("live", 373, 72, KIT_PX(10.5f), KIT_GREEN, 1, true);
    }
    if (t->msg[0] != '\0') {
        float sc = KIT_PX(12);
        char lines[4][96];
        int n = kit_wrap(t->msg, 340, sc, lines, 4);
        for (int i = 0; i < n; i++) {
            kit_text(lines[i], 27, 98 + i * 16, sc, KIT_INK2, -1, false);
        }
    } else if (t->trip[0] != '\0') {
        float w = kit_lcd_width(t->trip, 52);
        kit_lcd(t->trip, 196 - w / 2, 92, 52, KIT_INK);
        kit_text(t->unit, 196 + w / 2 + 8, 130, KIT_PX(13), KIT_INK3, -1, false);
        text_mid(t->since, 200, 166, 11, KIT_INK3, false);
    } else {
        text_mid("No trip yet: tap Reset Trip to start one here.", 200, 125, 12, KIT_INK2, true);
    }
    if (t->odo[0] != '\0') {
        kit_text(t->odo, 27, 194, KIT_PX(12), KIT_INK, -1, true);
    }
    kit_end();
}

static void button(garage_rect_t r, const char *label, bool large, float px, bool pressed)
{
    kit_button(large ? sysui_button_large_idx : sysui_button_medium_idx, r.x, r.y, r.w, r.h, pressed);
    text_mid(label, r.x + r.w / 2.0f, r.y + r.h / 2.0f + (pressed ? 1 : 0), px, KIT_INK, true);
}

void garage_draw_bottom(garage_kind_t kind, const garage_bottom_t *b)
{
    kit_begin(false, 0);
    if (kind == GAR_CARD) {
        garage_rect_t l = garage_rect(kind, GAR_HIT_PREV), r = garage_rect(kind, GAR_HIT_NEXT);
        bool pl = b->pressed == GAR_HIT_PREV, pr = b->pressed == GAR_HIT_NEXT;
        kit_image(kit_sysui(pl ? sysui_side_left_dn_idx : sysui_side_left_idx),
                  -(LIVE_SIDE_W - LIVE_SIDE_SHOWN), l.y + (pl ? 1 : 0));
        kit_image(kit_sysui(pr ? sysui_side_right_dn_idx : sysui_side_right_idx),
                  KIT_BOTTOM_W - LIVE_SIDE_SHOWN, r.y + (pr ? 1 : 0));
        text_mid(fit(b->name, 220, KIT_PX(17)), 160, 38, 17, KIT_INK, true);
        text_mid(b->sub, 160, 60, 11, KIT_INK3, false);
    }
    for (int i = 0; i < garage_button_count(kind); i++) {
        garage_rect_t r = garage_rect(kind, GAR_HIT_BUTTON + i);
        bool large = !(kind == GAR_CARD && i < 2) && !(kind == GAR_SHOT && i > 0);
        float px = kind == GAR_CAMERA ? 19 : (kind == GAR_TRIP || (kind == GAR_SHOT && i == 0)) ? 17
                 : kind == GAR_ENTRY ? 15 : 14;
        button(r, b->labels[i] != NULL ? b->labels[i] : "", large, px, b->pressed == GAR_HIT_BUTTON + i);
    }
    if (b->caption != NULL && b->caption[0] != '\0') {
        float y = kind == GAR_CAMERA ? 146 : kind == GAR_SHOT ? 164 : 142;
        text_mid(b->caption, 160, y, 11, KIT_INK3, false);
    }
    notice_pill(b->notice);
    kit_back_tab(b->pressed == GAR_HIT_BACK);
    if (kind == GAR_CARD || kind == GAR_TRIP) {
        kit_right_tab("Y", "Info", b->pressed == GAR_HIT_RIGHT);
    }
    kit_end();
}

void garage_draw_miles(const garage_entry_row_t *rows, int n, int scroll, int pressed_row,
                       bool grip_pressed, int pressed, const char *empty, const char *notice)
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
        float ds = KIT_PX(14), ms = KIT_PX(11), es = KIT_PX(12);
        kit_text(rows[i].date, r.x + 12, mid - 30 * ds / 2, ds, KIT_INK, -1, true);
        float mx = r.x + 12 + kit_text_width(rows[i].date, ds) + 10;
        float ew = kit_text_width(rows[i].econ, es);
        kit_text(fit(rows[i].mid, r.x + r.w - 12 - ew - 8 - mx, ms), mx, mid - 30 * ms / 2, ms, KIT_INK2,
                 -1, false);
        kit_text(rows[i].econ, r.x + r.w - 12, mid - 30 * es / 2, es, KIT_INK, 1, true);
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
    kit_back_tab(pressed == GAR_HIT_BACK);
    kit_right_tab("X", "New Entry", pressed == GAR_HIT_RIGHT);
    kit_end();
}
