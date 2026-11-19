#include "track_draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "diag_ui.h"
#include "icons.h"
#include "sysui.h"

#define GREEN    KIT_GREEN
#define AMBER    KIT_AMBER
#define RED      KIT_RED_HI
#define YELLOW   KIT_RGB(255, 210, 63)
#define GREY     KIT_INK3
#define FAINT    KIT_RGB(122, 122, 122)
#define ROW_EDGE KIT_RGB(44, 44, 44)
#define ROW_FILL KIT_RGB(26, 26, 26)
#define DLG_EDGE KIT_RGB(74, 74, 74)
#define DLG_FILL KIT_RGB(48, 48, 48)
#define PI_F     3.14159265f

static void panel(float x, float y, float w, float h)
{
    kit_rrect(x, y, w, h, 6, KIT_PANEL_EDGE);
    kit_rrect(x + 1, y + 1, w - 2, h - 2, 5, KIT_PANEL);
}

/* Text centred on cx with its line's middle at mid. */
static void text_mid(const char *s, float cx, float mid, float px, u32 c, bool bold)
{
    float sc = KIT_PX(px);
    kit_text(s, cx, mid - 30 * sc / 2, sc, c, 0, bold);
}

/* Paragraphs ("\n\n" between them) wrapped to w; returns the height drawn (or that would be,
 * with draw false). */
static float paragraphs(const char *text, float x, float y, float w, float px, float lh, u32 c,
                        bool draw)
{
    float sc = KIT_PX(px), top = y;
    const char *p = text;
    char para[400];
    while (*p != '\0') {
        const char *e = strstr(p, "\n\n");
        size_t n = e != NULL ? (size_t)(e - p) : strlen(p);
        snprintf(para, sizeof para, "%.*s", (int)n, p);
        char lines[12][96];
        int nl = kit_wrap(para, w, sc, lines, 12);
        for (int i = 0; i < nl; i++, y += lh) {
            if (draw) {
                kit_text(lines[i], x, y, sc, c, -1, false);
            }
        }
        p += n;
        if (*p != '\0') {
            p += 2;
            y += lh * 0.45f;
        }
    }
    return y - top;
}

static void notice_pill(const char *notice)
{
    if (notice == NULL || notice[0] == '\0') {
        return;
    }
    float s = KIT_PX(12), w = kit_text_width(notice, s) + 24;
    if (w > 300) {
        w = 300;
    }
    kit_rrect(160 - w / 2 - 1, 179, w + 2, 22, 7, KIT_PANEL_EDGE);
    kit_rrect(160 - w / 2, 180, w, 20, 6, KIT_RGB(24, 24, 24));
    kit_text(notice, 160, 190 - 30 * s / 2, s, AMBER, 0, true);
}

static void tabs(track_kind_t kind, int pressed)
{
    kit_back_tab(pressed == TRK_HIT_BACK);
    if (track_rect(kind, TRK_HIT_INFO).w > 0) {
        kit_right_tab("Y", "Info", pressed == TRK_HIT_INFO);
    }
}

/* ---------- bottom ---------- */

void track_draw_bottom(track_kind_t kind, const track_bottom_t *b)
{
    kit_begin(false, 0);
    if (kind == TRK_NOTICE) {
        kit_rrect(19, 23, 282, 114, 11, DLG_EDGE);
        kit_rrect(20, 24, 280, 112, 10, DLG_FILL);
        static const char *const text = "Time runs only on a closed course, with a passenger "
                                        "working the 3DS or none needed.";
        char lines[4][96];
        float s = KIT_PX(13);
        int n = kit_wrap(text, 244, s, lines, 4);
        float y0 = 80 - n * 18 / 2.0f;
        for (int i = 0; i < n; i++) {
            kit_text(lines[i], 160, y0 + i * 18, s, KIT_INK, 0, true);
        }
    }
    int nb = track_button_count(kind);
    bool medium = kind == TRK_NOTICE || kind == TRK_SHIFT;
    for (int i = 0; i < nb; i++) {
        track_rect_t r = track_rect(kind, TRK_HIT_BUTTON + i);
        bool p = b->pressed == TRK_HIT_BUTTON + i;
        kit_button(medium ? sysui_button_medium_idx : sysui_button_large_idx, r.x, r.y, r.w, r.h, p);
        float px = kind == TRK_SHIFT ? 22 : (kind == TRK_REVSYNC || kind == TRK_ECO) ? 18 : 15;
        text_mid(b->labels[i] != NULL ? b->labels[i] : "", r.x + r.w / 2.0f,
                 r.y + r.h / 2.0f + (p ? 1 : 0), px, KIT_INK, true);
    }
    if (kind == TRK_TA_RUN) {
        track_rect_t st = track_rect(kind, TRK_HIT_STOP), sr = track_rect(kind, TRK_HIT_START);
        size_t tog = b->armed ? sysui_toggle_right_idx : sysui_toggle_left_idx;
        kit_image(kit_sysui(tog), st.x, st.y);
        if (b->pressed == TRK_HIT_STOP || b->pressed == TRK_HIT_START) {
            track_rect_t h = b->pressed == TRK_HIT_STOP ? st : sr;
            kit_image_part(kit_sysui(kit_pressed(tog)), h.x - st.x, 0, h.w, h.h, h.x, h.y, h.w, h.h);
        }
        text_mid("Stop", st.x + st.w / 2.0f, st.y + st.h / 2.0f, 16, KIT_INK, true);
        text_mid("Start", sr.x + sr.w / 2.0f, sr.y + sr.h / 2.0f, 16, KIT_INK, true);
    }
    if (kind == TRK_SHIFT && b->lcd != NULL) {
        kit_text("Shift point", 160, 64, KIT_PX(11), GREY, 0, false);
        float w = kit_lcd_width(b->lcd, 32);
        kit_lcd(b->lcd, 160 - w / 2, 92, 32, KIT_INK);
        kit_text("rpm", 160, 128, KIT_PX(11), GREY, 0, false);
    }
    if (b->status != NULL && b->status[0] != '\0') {
        track_rect_t first = track_rect(kind, kind == TRK_TA_RUN ? TRK_HIT_STOP : TRK_HIT_BUTTON);
        kit_text(b->status, 160, first.y - 28, KIT_PX(12), b->status_color, 0, true);
    }
    notice_pill(b->notice);
    tabs(kind, b->pressed);
    kit_end();
}

void track_draw_info(track_kind_t kind, bool back_pressed)
{
    kit_begin(false, 0);
    panel(14, 8, 292, 186);
    const char *text = track_info_text(kind);
    /* The biggest size that fits. */
    static const float sizes[][2] = { { 11.5f, 15 }, { 11, 14.5f }, { 10.5f, 13.6f }, { 10, 13 } };
    int k = 0;
    while (k < 3 && paragraphs(text, 26, 16, 268, sizes[k][0], sizes[k][1], 0, false) > 172) {
        k++;
    }
    paragraphs(text, 26, 16, 268, sizes[k][0], sizes[k][1], KIT_INK2, true);
    kit_back_tab(back_pressed);
    kit_end();
}

/* A check mark, its bottom point at (x, y). */
static void check_mark(float x, float y, u32 c)
{
    kit_quad(x - 4.5f, y - 4, x - 3, y - 5.5f, x + 0.8f, y - 1.6f, x, y, c);
    kit_quad(x, y, x - 0.8f, y - 2.4f, x + 4.2f, y - 9, x + 5.6f, y - 7.6f, c);
}

void track_draw_ach_bottom(const track_ach_row_t *rows, int n, int scroll, bool grip_pressed,
                           int pressed, const char *notice)
{
    kit_begin(false, 0);
    for (int i = 0; i < n; i++) {
        diag_rect_t r = diag_list_row(i, scroll);
        if (r.y + r.h < 0 || r.y > DIAG_LIST_H) {
            continue;
        }
        float mid = r.y + r.h / 2.0f;
        kit_rrect(r.x, r.y, r.w, r.h, 8, ROW_EDGE);
        kit_rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 7, ROW_FILL);
        float rs = KIT_PX(rows[i].unlocked ? 11.5f : 10.5f), rx = r.x + r.w - 12;
        u32 rc = rows[i].unlocked ? GREEN : GREY;
        float nw = kit_text_width(rows[i].name, KIT_PX(13));
        float room = r.w - 24 - nw - 10;
        char right[64];
        snprintf(right, sizeof right, "%s", rows[i].right);
        while (strlen(right) > 1 && kit_text_width(right, rs) > room) {
            right[strlen(right) - 1] = '\0';
        }
        kit_text(right, rx, mid - 30 * rs / 2, rs, rc, 1, rows[i].unlocked);
        if (rows[i].unlocked) {
            check_mark(rx - kit_text_width(right, rs) - 9, mid + 4, rc);
        }
    }
    kit_grid_band(KIT_BOTTOM_W, DIAG_LIST_H, KIT_H);
    /* The names, over the rows (drawn after so the band can't cover them). */
    for (int i = 0; i < n; i++) {
        diag_rect_t r = diag_list_row(i, scroll);
        if (r.y + r.h < 0 || r.y + r.h / 2 > DIAG_LIST_H) {
            continue;
        }
        float s = KIT_PX(13);
        kit_text(rows[i].name, r.x + 12, r.y + r.h / 2.0f - 30 * s / 2, s, KIT_INK, -1, true);
    }
    if (diag_list_max_scroll(n) > 0) {
        diag_rect_t t = diag_list_track();
        C2D_Image track = kit_sysui(sysui_scroll_track_idx);
        kit_image_part(track, 0, 0, track.subtex->width, track.subtex->height, t.x, t.y, t.w, t.h);
        kit_image(kit_sysui(grip_pressed ? sysui_scroll_grip_dn_idx : sysui_scroll_grip_idx),
                  t.x + t.w / 2.0f - DIAG_GRIP_W / 2.0f, diag_list_grip_y(scroll, n));
    }
    notice_pill(notice);
    tabs(TRK_ACHIEVE, pressed);
    kit_end();
}

/* ---------- top ---------- */

static void top_begin(const char *title, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icons_track_idx, title);
}

void track_draw_text_top(int icon, const char *title, const char *text, const kit_status_t *s)
{
    kit_begin(true, 20);
    kit_status_bar(s);
    kit_title(icon, title);
    float h = paragraphs(text, 27, 75, 346, 12.5f, 15.5f, 0, false);
    panel(17, 67, 366, h + 16);
    paragraphs(text, 27, 75, 346, 12.5f, 15.5f, KIT_INK, true);
    kit_end();
}

void track_draw_ta_top(const track_ta_top_t *t, const kit_status_t *s)
{
    top_begin(t->title, s);
    panel(17, 66, 366, 150);
    if (t->big[0] != '\0' && !t->big_lcd) {
        text_mid(t->big, 200, 105, 40, t->big_color, true);
    } else {   /* the time, or unlit digits until there is one */
        const char *v = t->big[0] != '\0' ? t->big : "-.--";
        float w = kit_lcd_width(v, 50);
        kit_lcd(v, 200 - w / 2, 80, 50, t->big[0] != '\0' ? t->big_color : KIT_RGB(60, 60, 60));
    }
    if (t->flag[0] != '\0') {
        text_mid(t->flag, 200, 146, 13, RED, true);
    } else if (t->say[0] != '\0') {
        text_mid(t->say, 200, 146, 12, KIT_INK2, true);
    }
    if (t->say2[0] != '\0') {
        text_mid(t->say2, 200, 160, 11, GREY, false);
    } else if (t->extra[0] != '\0') {
        text_mid(t->extra, 200, 160, 11, GREY, false);
    }
    if (t->msg[0] != '\0') {
        text_mid(t->msg, 200, 178, 11.5f, AMBER, true);
    } else if (t->speed[0] != '\0') {
        float w = kit_lcd_width(t->speed, 20);
        kit_lcd(t->speed, 196 - w / 2, 168, 20, KIT_INK2);
        kit_text(t->speed_unit, 196 + w / 2 + 6, 176, KIT_PX(11), GREY, -1, false);
    }
    float ls = KIT_PX(11), vs = KIT_PX(14);
    kit_text("Best (this car)", 27, 199, ls, GREY, -1, false);
    kit_text(t->best, 27 + kit_text_width("Best (this car)", ls) + 6, 196, vs, KIT_INK, -1, true);
    float lw = kit_text_width(t->last, vs);
    kit_text(t->last, 373, 196, vs, KIT_INK, 1, true);
    kit_text("Last", 373 - lw - 6, 199, ls, GREY, 1, false);
    text_mid("Closed courses and passengers only.", 200, 228, 11, AMBER, true);
    kit_end();
}

void track_draw_shift_top(const track_shift_top_t *t, const kit_status_t *s)
{
    static const u32 on[3] = { KIT_RGB(79, 203, 98), KIT_RGB(242, 165, 43), KIT_RGB(255, 59, 48) };
    static const u32 off[3] = { KIT_RGB(20, 54, 27), KIT_RGB(61, 42, 11), KIT_RGB(61, 17, 15) };
    top_begin("SHIFT LIGHT", s);
    panel(17, 70, 366, 52);
    for (int i = 0; i < 10; i++) {
        float cx = 47 + i * 34.0f;
        int c = t->color[i] < 0 || t->color[i] > 2 ? 0 : t->color[i];
        bool lit = i < t->lit && t->flash_on;
        if (t->flashing) {
            c = 2;
        }
        kit_circle(cx, 96, 13, KIT_RGB(42, 42, 42));
        kit_circle(cx, 96, 11, lit ? on[c] : off[c]);
        if (lit) {
            kit_circle(cx - 4, 92, 3.5f, KIT_RGBA(255, 255, 255, 0x70));
        }
    }
    if (t->rpm[0] != '\0') {
        float w = kit_lcd_width(t->rpm, 50);
        kit_lcd(t->rpm, 186 - w / 2, 136, 50, t->flashing && t->flash_on ? RED : KIT_INK);
        kit_text("rpm", 186 + w / 2 + 8, 170, KIT_PX(13), GREY, -1, false);
    } else {
        text_mid(t->msg, 200, 160, 13, GREY, true);
    }
    if (t->flashing) {
        text_mid("SHIFT!", 200, 210, 20, t->flash_on ? RED : KIT_RGB(90, 21, 18), true);
    } else {
        text_mid(t->foot, 200, 208, 11, GREY, false);
    }
    kit_end();
}

/* A ring of thickness w around (cx, cy), radius r. */
static void circle_ring(float cx, float cy, float r, float w, u32 c)
{
    for (int i = 0; i < 48; i++) {
        float a0 = 2 * PI_F * i / 48, a1 = 2 * PI_F * (i + 1) / 48;
        kit_quad(cx + r * cosf(a0), cy + r * sinf(a0), cx + r * cosf(a1), cy + r * sinf(a1),
                 cx + (r - w) * cosf(a1), cy + (r - w) * sinf(a1), cx + (r - w) * cosf(a0),
                 cy + (r - w) * sinf(a0), c);
    }
}

void track_draw_gmeter_top(const track_gm_top_t *t, const kit_status_t *s)
{
    top_begin("G-METER", s);
    const float cx = 112, cy = 146, r = 74;
    kit_circle(cx, cy, r, KIT_RGB(16, 16, 16));
    circle_ring(cx, cy, r, 2, KIT_RGB(58, 58, 58));
    for (int i = 0; i < 24; i++) {   /* the 0.5 g ring, dashed */
        float a0 = 2 * PI_F * (i + 0.15f) / 24, a1 = 2 * PI_F * (i + 0.6f) / 24, rr = r / 2;
        kit_quad(cx + rr * cosf(a0), cy + rr * sinf(a0), cx + rr * cosf(a1), cy + rr * sinf(a1),
                 cx + (rr - 1.5f) * cosf(a1), cy + (rr - 1.5f) * sinf(a1),
                 cx + (rr - 1.5f) * cosf(a0), cy + (rr - 1.5f) * sinf(a0), KIT_RGB(48, 48, 48));
    }
    kit_rect(cx - r, cy - 0.5f, 2 * r, 1, KIT_RGB(42, 42, 42));
    kit_rect(cx - 0.5f, cy - r, 1, 2 * r, KIT_RGB(42, 42, 42));
    float ls = KIT_PX(8.5f);
    kit_text("ACCEL", cx, cy - r + 4, ls, FAINT, 0, true);
    kit_text("BRAKE", cx, cy + r - 14, ls, FAINT, 0, true);
    kit_text("0.5", cx + r / 2 + 3, cy - 12, KIT_PX(7.5f), FAINT, -1, false);
    kit_text("1 g", cx + r - 15, cy - 12, KIT_PX(7.5f), FAINT, -1, false);
    if (t->ball) {
        float x = t->lat, y = t->lon, d = sqrtf(x * x + y * y);
        if (d > 1.0f) {   /* past 1 g: pinned to the edge */
            x /= d;
            y /= d;
        }
        float bx = cx + x * r, by = cy - y * r;
        kit_circle(bx, by, 10, KIT_BG);
        kit_circle(bx, by, 8, RED);
        kit_circle(bx - 2.5f, by - 2.5f, 2.5f, KIT_RGBA(255, 255, 255, 0x90));
    }

    panel(200, 72, 183, 150);
    float y = 80;
    for (int i = 0; i < t->nrows; i++) {
        const char *row = t->rows[i];
        if (row[0] == '!') {   /* a heading */
            kit_text(row + 1, 210, y + 2, KIT_PX(11), AMBER, -1, true);
            y += 18;
            continue;
        }
        char a[32] = "", b[24] = "", c[24] = "";
        sscanf(row, "%31[^|]|%23[^|]|%23[^\n]", a, b, c);
        if (b[0] == '\0') {   /* a plain line */
            kit_text(a, 210, y + 2, KIT_PX(10.5f), KIT_INK2, -1, false);
            y += 15;
            continue;
        }
        kit_text(a, 210, y + 2, KIT_PX(11), GREY, -1, false);
        kit_text(b, 262, y, KIT_PX(14), KIT_INK, -1, true);
        if (c[0] != '\0') {
            kit_text(c, 262 + kit_text_width(b, KIT_PX(14)) + 5, y + 2, KIT_PX(11), GREY, -1, false);
        }
        y += 19;
    }
    kit_end();
}

void track_draw_revsync_top(const track_rs_top_t *t, const kit_status_t *s)
{
    top_begin("REV SYNC", s);
    panel(17, 66, 366, 62);
    const float x0 = 30, w = 340;
    const int n = 35;
    for (int i = 0; i < n; i++) {
        float rpm = (i + 0.5f) * 200.0f;
        bool lit = t->rpm > 0 && rpm <= t->rpm;
        u32 c = rpm < 5000 ? (lit ? GREEN : KIT_RGB(20, 54, 27))
              : rpm < 6000 ? (lit ? AMBER : KIT_RGB(61, 42, 11))
                           : (lit ? KIT_RGB(255, 59, 48) : KIT_RGB(61, 17, 15));
        kit_rrect(x0 + i * w / n + 0.75f, 84, w / n - 1.5f, 22, 1.5f, c);
    }
    for (int k = 0; k <= 7; k++) {
        char d[4];
        snprintf(d, sizeof d, "%d", k);
        kit_text(d, x0 + k * w / 7, 110, KIT_PX(8.5f), FAINT, 0, true);
    }
    kit_text("x1000", 372, 118, KIT_PX(8.5f), FAINT, 1, true);
    if (t->target > 0) {
        float tx = x0 + t->target / 7000.0f * w;
        kit_quad(tx - 6, 75, tx + 6, 75, tx, 83, tx, 83, KIT_INK);
        kit_rect(tx - 1, 83, 2, 25, KIT_INK);
    }
    if (t->peak > 0) {
        float px = x0 + (t->peak > 7000 ? 7000 : t->peak) / 7000.0f * w;
        for (float y = 80; y < 110; y += 5) {
            kit_rect(px - 1, y, 2, 3, YELLOW);
        }
    }
    if (t->big[0] != '\0') {
        bool digits = strspn(t->big, "0123456789,") == strlen(t->big);
        if (digits) {
            float bw = kit_lcd_width(t->big, 34);
            kit_lcd(t->big, 200 - bw / 2, 138, 34, t->big_color);
        } else {
            text_mid(t->big, 200, 156, 30, t->big_color, true);
        }
    }
    if (t->say[0] != '\0') {
        text_mid(t->say, 200, 186, 15, t->big[0] != '\0' ? t->big_color : KIT_INK2, true);
    }
    if (t->round[0] != '\0') {
        float ls = KIT_PX(11), vs = KIT_PX(14);
        kit_text("Target", 27, 204, ls, GREY, -1, false);
        kit_text(t->target_text, 27 + kit_text_width("Target", ls) + 5, 201, vs, KIT_INK, -1, true);
        kit_text(t->round, 200, 201, vs, KIT_INK, 0, true);
        kit_text(t->score, 373, 201, vs, KIT_INK, 1, true);
        kit_text("Score", 373 - kit_text_width(t->score, vs) - 5, 204, ls, GREY, 1, false);
    } else if (t->say2[0] != '\0') {
        text_mid(t->say2, 200, 208, 11.5f, GREY, false);
    }
    text_mid(t->best, 200, 228, 11, GREY, false);
    kit_end();
}

void track_draw_eco_top(const track_eco_top_t *t, const kit_status_t *s)
{
    top_begin("ECO CHALLENGE", s);
    panel(17, 66, 366, 76);
    float sw = kit_lcd_width(t->score, 46);
    kit_lcd(t->score, 30, 81, 46, t->rank[0] ? t->rank_color : KIT_INK);
    float rx = 30 + sw + 14;
    if (t->rank[0] != '\0') {
        kit_rrect(rx, 84, 36, 36, 9, t->rank_color);
        text_mid(t->rank, rx + 18, 102, 24, KIT_BG, true);
        kit_text("rank", rx + 18, 123, KIT_PX(10), GREY, 0, false);
    } else {
        kit_text("Keep driving:", rx, 82, KIT_PX(10), GREY, -1, false);
        kit_text("ranked after", rx, 96, KIT_PX(10), GREY, -1, false);
        kit_text("1 min, 0.5 km", rx, 110, KIT_PX(10), GREY, -1, false);
    }
    static const char *const labels[3] = { "Now", "Trip", "Best" };
    const char *values[3] = { t->now, t->trip, t->best };
    for (int i = 0; i < 3; i++) {
        float y = 76 + i * 20;
        kit_text(labels[i], 252, y + 2, KIT_PX(11), GREY, -1, false);
        kit_text(values[i], 290, y, KIT_PX(14), KIT_INK, -1, true);
    }
    panel(17, 148, 366, 62);
    for (int i = 0; i < 6; i++) {
        if (t->stats[i] == NULL) {
            continue;
        }
        char a[24] = "", b[24] = "";
        sscanf(t->stats[i], "%23[^|]|%23[^\n]", a, b);
        float x = 27 + (i % 2) * 175, y = 154 + (i / 2) * 18;
        float ls = KIT_PX(11);
        kit_text(a, x, y + 2, ls, GREY, -1, false);
        kit_text(b, x + kit_text_width(a, ls) + 5, y, KIT_PX(13.5f), KIT_INK, -1, true);
    }
    if (t->tip[0] != '\0') {
        text_mid(t->tip, 200, 224, 11, AMBER, true);
    } else if (t->msg[0] != '\0') {
        text_mid(t->msg, 200, 224, 10.5f, GREY, false);
    }
    kit_end();
}

/* A five-point star at (cx, cy), outer radius r. */
static void star(float cx, float cy, float r, u32 c)
{
    float ox[5], oy[5], ix[5], iy[5];
    for (int k = 0; k < 5; k++) {
        float a = -PI_F / 2 + k * 2 * PI_F / 5, b = a + PI_F / 5;
        ox[k] = cx + r * cosf(a);
        oy[k] = cy + r * sinf(a);
        ix[k] = cx + r * 0.42f * cosf(b);
        iy[k] = cy + r * 0.42f * sinf(b);
    }
    for (int k = 0; k < 5; k++) {
        int p = (k + 4) % 5;
        kit_quad(ox[k], oy[k], ix[k], iy[k], cx, cy, ix[p], iy[p], c);
    }
}

void track_draw_ach_top(const char *const *names, const bool *unlocked, int n, int count,
                        const kit_status_t *s)
{
    static const u32 colors[6] = { KIT_RGB(255, 90, 78), KIT_RGB(79, 203, 98), KIT_RGB(242, 165, 43),
                                   KIT_RGB(90, 169, 255), KIT_RGB(43, 174, 159), KIT_RGB(255, 210, 63) };
    top_begin("ACHIEVEMENTS", s);
    for (int i = 0; i < n && i < 12; i++) {
        float x = 23 + (i % 4) * 90, y = 68 + (i / 4) * 50;
        kit_rrect(x, y, 88, 46, 8, KIT_PANEL_EDGE);
        kit_rrect(x + 1, y + 1, 86, 44, 7, KIT_RGB(22, 22, 22));
        if (unlocked[i]) {
            kit_circle(x + 44, y + 15, 11, colors[i % 6]);
            star(x + 44, y + 15.5f, 7.5f, KIT_BG);
        } else {
            kit_circle(x + 44, y + 15, 11, KIT_RGB(42, 42, 42));
            text_mid("?", x + 44, y + 15, 12, KIT_RGB(85, 85, 85), true);
        }
        char lines[2][96];
        int nl = kit_wrap(names[i], 82, KIT_PX(9.5f), lines, 2);
        for (int k = 0; k < nl; k++) {
            kit_text(lines[k], x + 44, y + 27 + k * 10 - (nl > 1 ? 2 : 0), KIT_PX(9.5f),
                     unlocked[i] ? KIT_INK : KIT_RGB(106, 106, 106), 0, nl == 1);
        }
    }
    char line[32];
    snprintf(line, sizeof line, "%d of %d unlocked", count, n);
    text_mid(line, 200, 228, 11.5f, KIT_INK2, true);
    kit_end();
}
