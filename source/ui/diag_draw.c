#include "diag_draw.h"

#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "sysui.h"

#define ROW_EDGE   KIT_RGB(44, 44, 44)
#define ROW_FILL   KIT_RGB(26, 26, 26)
#define ROW_HELD   KIT_RGB(40, 40, 40)
#define ROW_SEL    KIT_RGB(38, 38, 38)
#define DESC_INK   KIT_RGB(200, 200, 200)
#define PILL_AMBER KIT_RGB(90, 61, 16)
#define DLG_EDGE   KIT_RGB(74, 74, 74)
#define DLG_FILL   KIT_RGB(48, 48, 48)
#define RED_FACE   KIT_RGBA(214, 44, 38, 0xE0)

static u32 ink(diag_ink_t k)
{
    switch (k) {
    case DIAG_INK_RED:   return KIT_RED_HI;
    case DIAG_INK_AMBER: return KIT_AMBER;
    case DIAG_INK_GREEN: return KIT_GREEN;
    case DIAG_INK_GREY:  return KIT_INK3;
    default:             return KIT_INK;
    }
}

/* s cut (with "...") to fit w px at scale. */
static const char *fit(const char *s, float w, float scale)
{
    static char buf[160];
    snprintf(buf, sizeof buf, "%s", s);
    if (kit_text_width(buf, scale) <= w) {
        return buf;
    }
    size_t n = strlen(buf);
    while (n > 0) {
        buf[--n] = '\0';
        char probe[164];
        snprintf(probe, sizeof probe, "%s...", buf);
        if (kit_text_width(probe, scale) <= w) {
            snprintf(buf + n, sizeof buf - n, "...");
            return buf;
        }
    }
    return buf;
}

static void panel(float y, float h)
{
    kit_rrect(17, y, 366, h, 6, KIT_PANEL_EDGE);
    kit_rrect(18, y + 1, 364, h - 2, 5, KIT_PANEL);
}

/* ---------- top ---------- */

#define FACT_PITCH 20.5f

void diag_draw_facts_top(int icon, const char *title, const diag_fact_t *f, int n,
                         const char *note, const char *status, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icon, title);

    char lines[4][96];
    int nl = note != NULL && note[0] ? kit_wrap(note, 344, KIT_PX(11), lines, 4) : 0;
    float h = 12 + n * FACT_PITCH + (nl > 0 ? 8 + nl * 15 : 0);
    if (h < 40) {
        h = 40;
    }
    panel(67, h);
    for (int i = 0; i < n; i++) {
        float mid = 67 + 6 + i * FACT_PITCH + FACT_PITCH / 2;
        float x = 27;
        kit_rrect(x, mid - 7, 90, 14, 4, KIT_TAG);
        kit_text(fit(f[i].tag, 82, KIT_PX(10)), x + 6, mid - 6.5f, KIT_PX(10), KIT_TAG_INK, -1, true);
        float vx = x + 98;
        if (f[i].cel_icon) {
            C2D_Image img = kit_icon(icons_cel_on_idx);
            kit_image_part(img, 0, 0, img.subtex->width, img.subtex->height, vx, mid - 9, 18, 18);
            vx += 22;
        }
        const char *v = fit(f[i].value, 372 - vx, KIT_PX(14));
        kit_text(v, vx, mid - 9, KIT_PX(14), ink(f[i].ink), -1, true);
        float dx = vx + kit_text_width(v, KIT_PX(14)) + 8;
        if (f[i].detail[0] != '\0' && dx < 360) {
            kit_text(fit(f[i].detail, 374 - dx, KIT_PX(11.5f)), dx, mid - 6.5f, KIT_PX(11.5f),
                     KIT_INK3, -1, false);
        }
    }
    float ny = 67 + 6 + n * FACT_PITCH + 6;
    for (int i = 0; i < nl; i++) {
        kit_text(lines[i], 27, ny + i * 15, KIT_PX(11), KIT_INK3, -1, false);
    }
    if (status != NULL && status[0] != '\0') {
        kit_text(fit(status, 360, KIT_PX(12)), 200, 67 + h + 6, KIT_PX(12), KIT_AMBER, 0, true);
    }
    kit_end();
}

static float pill(const char *text, float x, float mid, u32 bg, u32 fg)
{
    float w = kit_text_width(text, KIT_PX(10)) + 14;
    kit_rrect(x, mid - 8, w, 16, 4, bg);
    kit_text(text, x + 7, mid - 6.5f, KIT_PX(10), fg, -1, true);
    return w;
}

void diag_draw_code_top(const diag_code_t *c, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);

    /* "P0171 [Stored] [Engine]" centred under the status bar. */
    float cs = KIT_PX(26), mid = 38;
    float cw = kit_text_width(c->code, cs);
    float k1 = kit_text_width(c->kind, KIT_PX(10)) + 14;
    float k2 = c->module[0] ? kit_text_width(c->module, KIT_PX(10)) + 14 : 0;
    float total = cw + 8 + k1 + (k2 > 0 ? 8 + k2 : 0);
    float x = (KIT_TOP_W - total) / 2;
    kit_text(c->code, x, mid - 30 * cs / 2, cs, KIT_RED_HI, -1, true);
    x += cw + 8;
    x += pill(c->kind, x, mid, c->kind[0] == 'S' ? PILL_AMBER : KIT_TAG,
              c->kind[0] == 'S' ? KIT_AMBER : KIT_TAG_INK) + 8;
    if (k2 > 0) {
        pill(c->module, x, mid, KIT_TAG, KIT_TAG_INK);
    }

    /* Its text: one line, or two smaller ones. */
    char lines[3][96];
    int nl = kit_wrap(c->text, 364, KIT_PX(14), lines, 3);
    if (nl <= 1) {
        kit_text(c->text, 200, 57, KIT_PX(14), KIT_INK, 0, true);
    } else {
        nl = kit_wrap(c->text, 364, KIT_PX(12), lines, 2);
        for (int i = 0; i < nl; i++) {
            kit_text(lines[i], 200, 53 + i * 14, KIT_PX(12), KIT_INK, 0, true);
        }
    }
    kit_rule(84);

    if (!c->have_freeze) {
        const char *why = c->kind[0] == 'S'
                              ? "No freeze frame for this code: the car keeps one, for the code "
                                "that set first."
                          : c->kind[0] == 'P' && c->kind[1] == 'e'
                              ? "Pending codes have no freeze frame. If the fault is seen again, "
                                "the code is stored and turns on the CEL."
                              : "A permanent code stays until the car's own tests pass again. "
                                "Clearing can't remove it.";
        char wl[4][96];
        int nw = kit_wrap(why, 340, KIT_PX(12), wl, 4);
        panel(96, 20 + nw * 17);
        for (int i = 0; i < nw; i++) {
            kit_text(wl[i], 30, 106 + i * 17, KIT_PX(12), KIT_INK2, -1, false);
        }
        kit_end();
        return;
    }

    kit_text("Freeze frame: when the code was set", 20, 89, KIT_PX(10.5f), KIT_INK3, -1, false);
    panel(105, 120);
    /* Seven rows a column; past seven, two columns. */
    int shown = c->freeze_count > 14 ? 14 : c->freeze_count;
    bool two = shown > 7;
    float col_w = two ? 178 : 356, tag_w = two ? 90 : 120;
    for (int i = 0; i < shown; i++) {
        int col = i / 7, row = i % 7;
        float fx = 27 + col * col_w, fmid = 105 + 8 + row * 15.5f + 7;
        kit_rrect(fx, fmid - 6.5f, tag_w, 13, 4, KIT_TAG);
        kit_text(fit(c->freeze_label[i], tag_w - 10, KIT_PX(9.5f)), fx + 5, fmid - 6.2f, KIT_PX(9.5f),
                 KIT_TAG_INK, -1, true);
        kit_text(fit(c->freeze_value[i], col_w - tag_w - 18, KIT_PX(12.5f)), fx + tag_w + 8,
                 fmid - 8, KIT_PX(12.5f), KIT_INK, -1, true);
    }
    if (c->freeze_count > shown) {
        char more[24];
        snprintf(more, sizeof more, "+%d more", c->freeze_count - shown);
        kit_text(more, 375, 210, KIT_PX(10), KIT_INK3, 1, false);
    }
    kit_end();
}

void diag_draw_clear_top(const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icons_diag_cel_idx, "CLEAR CODES?");
    static const char *const text =
        "Clearing turns off the check engine light and erases the stored codes and the freeze "
        "frame.\n\nIt also resets the readiness monitors: the car may fail an emissions "
        "inspection until it has been driven for a while.\n\nThe fault itself is not fixed. "
        "Park first: ignition on, engine off works on most cars.";
    char lines[12][96];
    int n = kit_wrap(text, 344, KIT_PX(12.5f), lines, 12);
    panel(67, 158);
    for (int i = 0; i < n; i++) {
        kit_text(lines[i], 27, 74 + i * 15.5f, KIT_PX(12.5f), KIT_INK, -1, false);
    }
    kit_end();
}

/* ---------- bottom ---------- */

static void notice_pill(const char *notice)
{
    if (notice == NULL || notice[0] == '\0') {
        return;
    }
    float s = KIT_PX(12);
    const char *t = fit(notice, 260, s);
    float w = kit_text_width(t, s) + 24;
    kit_rrect(160 - w / 2 - 1, 177, w + 2, 24, 7, KIT_PANEL_EDGE);
    kit_rrect(160 - w / 2, 178, w, 22, 6, KIT_RGB(24, 24, 24));
    kit_text(t, 160, 189 - 30 * s / 2, s, KIT_AMBER, 0, true);
}

void diag_draw_home_bottom(diag_hit_t pressed, const char *notice)
{
    kit_begin(false, 0);
    for (int i = 0; i < DIAG_HOME_BUTTONS; i++) {
        diag_rect_t r = diag_home_button(i);
        bool p = pressed == (diag_hit_t)(DIAG_HIT_STORED + i);
        kit_button(sysui_button_large_idx, r.x, r.y, r.w, r.h, p);
        float s = KIT_PX(14);
        kit_text(diag_home_label(i), r.x + r.w / 2.0f, r.y + (p ? 1 : 0) + r.h / 2.0f - 30 * s / 2, s,
                 KIT_INK, 0, true);
    }
    notice_pill(notice);
    kit_back_tab(pressed == DIAG_HIT_BACK);
    kit_right_tab("X", "Scan", pressed == DIAG_HIT_RIGHT);
    kit_end();
}

/* A check mark, 9 px wide, its bottom point at (x, y). */
static void check_mark(float x, float y, u32 c)
{
    kit_quad(x - 4.5f, y - 4, x - 3, y - 5.5f, x + 0.8f, y - 1.6f, x, y, c);
    kit_quad(x, y, x - 0.8f, y - 2.4f, x + 4.2f, y - 9, x + 5.6f, y - 7.6f, c);
}

void diag_draw_page_bottom(const diag_page_view_t *v)
{
    kit_begin(false, 0);
    for (int i = 0; i < v->n; i++) {
        diag_rect_t r = diag_list_row(i, v->scroll);
        if (r.y + r.h < 0 || r.y > DIAG_LIST_H) {
            continue;
        }
        const diag_row_t *row = &v->rows[i];
        bool sel = i == v->selected, held = i == v->pressed_row;
        float y = r.y + (held ? 1 : 0), mid = y + r.h / 2.0f;
        kit_rrect(r.x, y, r.w, r.h, 8, sel ? KIT_RED : ROW_EDGE);
        float in = sel ? 2 : 1;
        kit_rrect(r.x + in, y + in, r.w - 2 * in, r.h - 2 * in, 8 - in,
                  held ? ROW_HELD : sel ? ROW_SEL : ROW_FILL);
        if (row->code) {
            float s = KIT_PX(14);
            kit_text(row->name, r.x + 12, mid - 30 * s / 2, s, sel ? KIT_RED_HI : KIT_INK, -1, true);
            float dx = r.x + 12 + 58;
            float ds = KIT_PX(11);
            kit_text(fit(row->right, r.x + r.w - 10 - dx, ds), dx, mid - 30 * ds / 2, ds, DESC_INK,
                     -1, false);
        } else {
            float s = KIT_PX(13);
            kit_text(row->name, r.x + 12, mid - 30 * s / 2, s, KIT_INK, -1, true);
            float ds = KIT_PX(11.5f);
            float rx = r.x + r.w - 12;
            kit_text(row->right, rx, mid - 30 * ds / 2, ds, ink(row->ink), 1, true);
            if (row->ink == DIAG_INK_GREEN) {
                check_mark(rx - kit_text_width(row->right, ds) - 9, mid + 4, ink(row->ink));
            }
        }
    }
    if (v->n == 0 && v->empty != NULL) {
        kit_text(v->empty, 160, 84, KIT_PX(15), KIT_INK, 0, true);
    }
    /* Rows that scrolled past the list's bottom edge go under the background. */
    kit_grid_band(KIT_BOTTOM_W, DIAG_LIST_H, KIT_H);

    if (diag_list_max_scroll(v->n) > 0) {
        diag_rect_t t = diag_list_track();
        C2D_Image track = kit_sysui(sysui_scroll_track_idx);
        kit_image_part(track, 0, 0, track.subtex->width, track.subtex->height, t.x, t.y, t.w, t.h);
        float gx = t.x + t.w / 2.0f - DIAG_GRIP_W / 2.0f;
        kit_image(kit_sysui(v->grip_pressed ? sysui_scroll_grip_dn_idx : sysui_scroll_grip_idx), gx,
                  diag_list_grip_y(v->scroll, v->n));
    }
    notice_pill(v->notice);
    kit_back_tab(v->pressed_tab == DIAG_HIT_BACK);
    if (v->right_tab) {
        kit_right_tab("X", "Clear", v->pressed_tab == DIAG_HIT_RIGHT);
    } else if (v->corner != NULL && v->corner[0] != '\0') {
        kit_text(v->corner, KIT_BOTTOM_W - 14, KIT_H - 22, KIT_PX(11), KIT_INK3, 1, false);
    }
    kit_end();
}

void diag_draw_dialog_bottom(int pressed)
{
    diag_draw_question("Clear the codes and reset", "the readiness monitors?", "Clear Codes", pressed);
}

void diag_draw_question(const char *line1, const char *line2, const char *yes, int pressed)
{
    kit_begin(false, 0);
    kit_rrect(19, 29, 282, 102, 11, DLG_EDGE);
    kit_rrect(20, 30, 280, 100, 10, DLG_FILL);
    float s = KIT_PX(15);
    if (line2 != NULL && line2[0] != '\0') {
        kit_text(line1, 160, 58, s, KIT_INK, 0, true);
        kit_text(line2, 160, 80, s, KIT_INK, 0, true);
    } else {
        kit_text(line1, 160, 69, s, KIT_INK, 0, true);
    }
    for (int i = 0; i < 2; i++) {
        bool clear = i == 1, p = pressed == i + 1;
        diag_rect_t r = diag_dialog_button(clear);
        kit_button(sysui_button_medium_idx, r.x, r.y, r.w, r.h, p);
        float y = r.y + (p ? 1 : 0);
        if (clear) {
            /* Red: it can't be undone. */
            kit_rrect(r.x + 4, y + 4, r.w - 8, r.h - 9, 9, p ? KIT_RGBA(150, 30, 26, 0xE8) : RED_FACE);
        }
        float ts = KIT_PX(14);
        kit_text(clear ? yes : "Cancel", r.x + r.w / 2.0f, y + r.h / 2.0f - 30 * ts / 2 - 1, ts,
                 KIT_INK, 0, true);
    }
    kit_end();
}
