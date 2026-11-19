#include "home_draw.h"

#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "kit.h"
#include "sysui.h"

/*
 * HOME, mockup v7 (docs/mockups/home-v7.html): the 3DS's own system pieces from
 * gfx/sysui (the HOME Menu status bar, System Settings' bottom bar and buttons, made by
 * tools/make_sysui.py) and Gauge Boy's icons from gfx/icons (tools/make_icons.py), on the
 * night theme's black background, drawn with the UI kit (kit.h).
 */

#define COL_LAMP_OFF  KIT_RGB(62, 62, 62)

static bool ready;

bool home_draw_init(void)
{
    ready = kit_init();
    return ready;
}

void home_draw_exit(void)
{
    kit_exit();
    ready = false;
}

/* The amber outline of DIAG while the CEL is on, with its glow. */
static void cel_outline(float x, float y, float w, float h)
{
    kit_ring(x - 3, y - 3, w + 6, h + 6, 12, 3, KIT_AMBER_GLOW);
    kit_ring(x - 1, y - 1, w + 2, h + 2, 10, 2, KIT_AMBER_GLOW);
    kit_ring(x, y, w, h, 9, 2.5f, KIT_AMBER);
}

/* ---------- bottom screen ---------- */

static void button(home_rect_t r, size_t idx, bool pressed)
{
    kit_button(idx, r.x, r.y, r.w, r.h, pressed);
}

static void status_button(home_item_t item, const home_view_t *v, size_t icon, const char *label)
{
    home_rect_t r = home_item_rect(item);
    bool pressed = v->pressed == (int)item;
    button(r, sysui_button_medium_idx, pressed);
    float y = r.y + (pressed ? 1 : 0);
    float s = KIT_PX(15), tw = kit_text_width(label, s);
    float x = r.x + (r.w - (22 + 8 + tw)) / 2;
    kit_image(kit_icon(icon), x, y + (r.h - 22) / 2);
    kit_text(label, x + 30, y + 8, s, KIT_INK, -1, true);
}

void home_draw(const home_view_t *v)
{
    if (!ready) {
        return;
    }
    kit_begin(false, 0);

    bool cel_on = v->cel_known && v->cel_on;

    status_button(HOME_GARAGE, v, v->car != NULL ? icons_garage_idx : icons_garage_dim_idx,
                  HOME_GARAGE_TEXT);
    status_button(HOME_ADAPTER, v, v->connected ? icons_adapter_on_idx : icons_adapter_off_idx,
                  "Adapter");

    static const size_t tile_icon[HOME_ITEM_COUNT] = {
        [HOME_LIVE] = icons_live_idx, [HOME_DIAG] = icons_diag_idx, [HOME_TRIM] = icons_trim_idx,
        [HOME_TRACK] = icons_track_idx, [HOME_LOG] = icons_log_idx, [HOME_SETUP] = icons_setup_idx,
    };
    for (int i = HOME_LIVE; i <= HOME_SETUP; i++) {
        home_rect_t r = home_item_rect((home_item_t)i);
        bool lamp = i == HOME_DIAG && cel_on;   /* the panel's CEL lamp */
        bool pressed = v->pressed == i;
        button(r, sysui_button_large_idx, pressed);
        float cx = r.x + r.w / 2.0f, y = r.y + (pressed ? 1 : 0);
        kit_image(kit_icon(lamp ? icons_diag_cel_idx : tile_icon[i]), cx - 15, y + 11);
        kit_text(home_item_label((home_item_t)i), cx, y + 45, KIT_PX(14), lamp ? KIT_AMBER : KIT_INK, 0, true);
        if (lamp) {
            cel_outline(r.x, r.y, r.w, r.h);
        }
    }

    /* System Settings' bottom bar: "START Close" (tap to close), or a notice. */
    kit_image(kit_sysui(sysui_bar_idx), 0, HOME_BAR_Y);
    if (v->pressed == HOME_CLOSE) {
        kit_rect(4, HOME_BAR_Y + 10, HOME_SCREEN_W - 8, HOME_SCREEN_H - HOME_BAR_Y - 10, KIT_PRESS);
    }
    /* The label as System Settings sets it: 22.5 px tall (scale 0.75), centred in the
     * 30 px above the bottom edge, its shadow 1 px up behind it. */
    float ls = 0.75f, ly = HOME_SCREEN_H - 15 - 30 * ls / 2;
    if (v->notice != NULL) {
        kit_text(v->notice, HOME_SCREEN_W / 2.0f, ly + 4, 0.5f, KIT_INK, 0, false);
    } else {
        kit_key_label("START", "Close", HOME_SCREEN_W / 2.0f, HOME_SCREEN_H - 15);
    }

    kit_end();
}

/* ---------- top screen ---------- */

static u32 tone_color(home_tone_t tone)
{
    switch (tone) {
    case HOME_TONE_DIM:     return KIT_INK3;
    case HOME_TONE_RED:     return KIT_RED_HI;
    case HOME_TONE_AMBER:   return KIT_AMBER;
    default:                return KIT_INK;
    }
}

void home_draw_top(const home_top_t *t, const home_status_t *s)
{
    if (!ready) {
        return;
    }
    kit_begin(true, 20);
    kit_status_bar(s);

    /* Title row: the app icon, GAUGE BOY: (white) OBD-II DELUXE (orange), centred. */
    float hs = KIT_PX(20);
    float w1 = kit_text_width("GAUGE BOY: ", hs), w2 = kit_text_width("OBD-II DELUXE", hs);
    float x = (KIT_TOP_W - (24 + 7 + w1 + w2)) / 2;
    kit_image(kit_icon(icons_app_idx), x, 25);
    kit_text("GAUGE BOY: ", x + 31, 24, hs, KIT_INK, -1, true);
    kit_text("OBD-II DELUXE", x + 31 + w1, 24, hs, KIT_ORANGE, -1, true);
    kit_rule(58.5f);

    /* The info panel: fact rows, a tag and a value each. */
    kit_rrect(17, 67, 366, 158, 6, KIT_PANEL_EDGE);
    kit_rrect(18, 68, 364, 156, 5, KIT_PANEL);
    home_row_t rows[HOME_ROWS];
    home_top_rows(t, rows);
    /* Rows 26 px apart; the odometer's row is taller for its big digits. */
    static const float row_y[HOME_ROWS] = { 74, 100, 126, 152, 197 };
    static const float row_h[HOME_ROWS] = { 20, 20, 20, 40, 20 };
    for (int i = 0; i < HOME_ROWS; i++) {
        const home_row_t *r = &rows[i];
        float y = row_y[i], mid = y + row_h[i] / 2;
        kit_rrect(27, mid - 7.5f, 74, 15, 4, KIT_TAG);
        kit_text(r->tag, 33, mid - 6.5f, KIT_PX(10.5f), KIT_TAG_INK, -1, true);
        float vx = 109;
        u32 c = tone_color(r->tone);
        if (r->lamp) {
            bool on = r->tone == HOME_TONE_AMBER;
            if (on) {
                kit_circle(vx + 4.5f, mid, 6.5f, KIT_AMBER_GLOW);
            }
            kit_circle(vx + 4.5f, mid, 4.5f, on ? KIT_AMBER : COL_LAMP_OFF);
            vx += 14;
        }
        float vw;
        if (r->lcd) {
            vw = kit_lcd(r->value, vx, mid - 13, 26, c) + 4;
            kit_text(r->detail, vx + vw + 2, mid - 2, KIT_PX(14), KIT_INK3, -1, false);
            continue;
        }
        kit_text(r->value, vx, mid - 10, KIT_PX(15), c, -1, true);
        vw = kit_text_width(r->value, KIT_PX(15));
        kit_text(r->detail, vx + vw + 6, mid - 6.5f, KIT_PX(12), KIT_INK3, -1, false);
    }

    kit_end();
}

void *home_draw_top_target(void)
{
    return kit_top_target();
}

void *home_draw_font_small(void)
{
    return kit_font_small();
}
