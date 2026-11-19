#include "home_draw.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "icons_t3x.h"
#include "sysui.h"
#include "sysui_t3x.h"

/*
 * HOME, mockup v7 (docs/mockups/home-v7.html): the 3DS's own system pieces from
 * gfx/sysui (the HOME Menu status bar, System Settings' bottom bar and buttons, made by
 * tools/make_sysui.py) and Gauge Boy's icons from gfx/icons (tools/make_icons.py), on the
 * night theme's black background. Sprites are drawn untinted (Azahar doesn't emulate the
 * procedural textures citro2d tints with); buttons are 9-sliced to size.
 */

/* Same as C2D_Color32(r, g, b, a), but usable in static tables. */
#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 0xFF)
#define COL_BG        RGB(0, 0, 0)
#define COL_GRID      RGB(10, 10, 10)
#define COL_INK       RGB(242, 244, 245)
#define COL_INK3      RGB(156, 156, 156)
#define COL_DOT       RGB(85, 85, 85)
#define COL_PANEL     RGB(18, 18, 18)
#define COL_PANEL_EDGE RGB(44, 44, 44)
#define COL_TAG       RGB(46, 46, 46)
#define COL_TAG_INK   RGB(214, 214, 214)
#define COL_UNLIT     RGB(24, 24, 24)        /* LCD segments that are off */
#define COL_RED       RGB(226, 48, 42)
#define COL_RED_HI    RGB(255, 90, 78)
#define COL_ORANGE    RGB(255, 138, 43)
#define COL_AMBER     RGB(242, 165, 43)
#define COL_AMBER_GLOW RGBA(242, 165, 43, 0x60)
#define COL_LAMP_OFF  RGB(62, 62, 62)
#define COL_PRESS     RGBA(0, 0, 0, 0x50)
#define COL_SB_TEXT   RGB(205, 205, 205)     /* the HOME Menu bar's dark version */
#define COL_SHADOW    RGB(20, 20, 20)

/* The system font draws a line about 30 px tall at scale 1 (an em of about
 * 24 px): px / 24 gives the mockup's CSS font sizes. */
#define PX(px) ((float)(px) / 24.0f)

static C3D_RenderTarget *target, *top_target;
static C2D_Font font_big;     /* Nunito Bold 13 pt: fallback when the system font won't map */
static C2D_Font font_small;   /* Nunito Bold 8 pt: GRAPH's font */
static C2D_Font ui_font;      /* NULL = the system font */
static C2D_SpriteSheet sysui, icons;
static C2D_TextBuf text_buf;
static bool romfs_ok, gpu_on, c2d_on;
static float z;               /* rising depth: later shapes draw on top */

static float next_z(void)
{
    z += 0.0005f;
    return z;
}

static void set_output(C3D_RenderTarget *t, gfxScreen_t screen)
{
    /* The consoles use RGB565, so the GPU writes that format too. */
    C3D_RenderTargetSetOutput(t, screen, GFX_LEFT,
                              GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                              GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                              GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                              GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
}

bool home_draw_init(void)
{
    romfs_ok = R_SUCCEEDED(romfsInit());
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        home_draw_exit();
        return false;
    }
    gpu_on = true;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2)) {
        home_draw_exit();
        return false;
    }
    c2d_on = true;
    C2D_Prepare();
    font_big = C2D_FontLoad("romfs:/fonts/nunito-13.bcfnt");
    font_small = C2D_FontLoad("romfs:/fonts/nunito-8.bcfnt");
    ui_font = R_SUCCEEDED(fontEnsureMapped()) ? NULL : font_big;
    sysui = C2D_SpriteSheetLoadFromMem(sysui_t3x, sysui_t3x_size);
    icons = C2D_SpriteSheetLoadFromMem(icons_t3x, icons_t3x_size);
    text_buf = C2D_TextBufNew(1024);
    target = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    top_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (font_small == NULL || sysui == NULL || icons == NULL || text_buf == NULL ||
        target == NULL || top_target == NULL) {
        home_draw_exit();
        return false;
    }
    set_output(target, GFX_BOTTOM);
    set_output(top_target, GFX_TOP);
    return true;
}

void home_draw_exit(void)
{
    if (target != NULL) {
        C3D_RenderTargetDelete(target);
        target = NULL;
    }
    if (top_target != NULL) {
        C3D_RenderTargetDelete(top_target);
        top_target = NULL;
    }
    if (text_buf != NULL) {
        C2D_TextBufDelete(text_buf);
        text_buf = NULL;
    }
    if (sysui != NULL) {
        C2D_SpriteSheetFree(sysui);
        sysui = NULL;
    }
    if (icons != NULL) {
        C2D_SpriteSheetFree(icons);
        icons = NULL;
    }
    if (font_big != NULL) {
        C2D_FontFree(font_big);
        font_big = NULL;
    }
    if (font_small != NULL) {
        C2D_FontFree(font_small);
        font_small = NULL;
    }
    if (c2d_on) {
        C2D_Fini();
        c2d_on = false;
    }
    if (gpu_on) {
        C3D_Fini();
        gpu_on = false;
    }
    if (romfs_ok) {
        romfsExit();
        romfs_ok = false;
    }
}

/* ---------- shapes ---------- */

static void rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, next_z(), w, h, c);
}

static void circle(float x, float y, float r, u32 c)
{
    C2D_DrawCircleSolid(x, y, next_z(), r, c);
}

static void rrect(float x, float y, float w, float h, float r, u32 c)
{
    float d = next_z();
    C2D_DrawRectSolid(x + r, y, d, w - 2 * r, h, c);
    C2D_DrawRectSolid(x, y + r, d, r, h - 2 * r, c);
    C2D_DrawRectSolid(x + w - r, y + r, d, r, h - 2 * r, c);
    C2D_DrawCircleSolid(x + r, y + r, d, r, c);
    C2D_DrawCircleSolid(x + w - r, y + r, d, r, c);
    C2D_DrawCircleSolid(x + r, y + h - r, d, r, c);
    C2D_DrawCircleSolid(x + w - r, y + h - r, d, r, c);
}

/* A rounded outline `width` thick just outside (x, y, w, h), corner radius r. */
static void ring(float x, float y, float w, float h, float r, float width, u32 c)
{
    rect(x + r, y - width, w - 2 * r, width, c);
    rect(x + r, y + h, w - 2 * r, width, c);
    rect(x - width, y + r, width, h - 2 * r, c);
    rect(x + w, y + r, width, h - 2 * r, c);
    static const float start[4] = { 180, 270, 90, 0 };   /* TL, TR, BL, BR */
    for (int k = 0; k < 4; k++) {
        float cx = (k & 1) ? x + w - r : x + r, cy = (k & 2) ? y + h - r : y + r;
        float ro = r + width;
        for (int s = 0; s < 6; s++) {
            float t0 = (start[k] + s * 15.0f) * 3.14159265f / 180.0f;
            float t1 = (start[k] + (s + 1) * 15.0f) * 3.14159265f / 180.0f;
            float d = next_z();
            C2D_DrawTriangle(cx + r * cosf(t0), cy + r * sinf(t0), c, cx + ro * cosf(t0), cy + ro * sinf(t0), c,
                             cx + ro * cosf(t1), cy + ro * sinf(t1), c, d);
            C2D_DrawTriangle(cx + r * cosf(t0), cy + r * sinf(t0), c, cx + ro * cosf(t1), cy + ro * sinf(t1), c,
                             cx + r * cosf(t1), cy + r * sinf(t1), c, d);
        }
    }
}

/* The amber outline of DIAG while the CEL is on, with its glow. */
static void cel_outline(float x, float y, float w, float h)
{
    ring(x - 3, y - 3, w + 6, h + 6, 12, 3, COL_AMBER_GLOW);
    ring(x - 1, y - 1, w + 2, h + 2, 10, 2, COL_AMBER_GLOW);
    ring(x, y, w, h, 9, 2.5f, COL_AMBER);
}

/* The faint grid behind both screens. */
static void grid(float w, float h, float top)
{
    for (float x = 18; x < w; x += 20) {
        rect(x, top, 2, h - top, COL_GRID);
    }
    for (float y = top + 18; y < h; y += 20) {
        rect(0, y, w, 2, COL_GRID);
    }
}

/* ---------- sprites ---------- */

static C2D_Image sprite(C2D_SpriteSheet sheet, size_t idx)
{
    return C2D_SpriteSheetGetImage(sheet, idx);
}

static void image(C2D_Image img, float x, float y)
{
    C2D_DrawImageAt(img, x, y, next_z(), NULL, 1.0f, 1.0f);
}

/* Part (sx, sy, sw, sh) of a sprite drawn into (dx, dy, dw, dh). */
static void image_part(C2D_Image img, float sx, float sy, float sw, float sh, float dx, float dy,
                       float dw, float dh)
{
    const Tex3DS_SubTexture *s = img.subtex;
    Tex3DS_SubTexture sub;
    sub.width = (u16)(sw + 0.5f);
    sub.height = (u16)(sh + 0.5f);
    sub.left = s->left + (s->right - s->left) * sx / s->width;
    sub.right = s->left + (s->right - s->left) * (sx + sw) / s->width;
    sub.top = s->top + (s->bottom - s->top) * sy / s->height;
    sub.bottom = s->top + (s->bottom - s->top) * (sy + sh) / s->height;
    C2D_Image part = { img.tex, &sub };
    C2D_DrawImageAt(part, dx, dy, next_z(), NULL, dw / sub.width, dh / sub.height);
}

/* A Nintendo button stretched to (x, y, w, h): corners kept, edges stretched. */
static void nine(C2D_Image img, float x, float y, float w, float h, float c)
{
    float sw = img.subtex->width, sh = img.subtex->height;
    float sx[4] = { 0, c, sw - c, sw }, sy[4] = { 0, c, sh - c, sh };
    float dx[4] = { x, x + c, x + w - c, x + w }, dy[4] = { y, y + c, y + h - c, y + h };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            image_part(img, sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j], dx[i], dy[j],
                       dx[i + 1] - dx[i], dy[j + 1] - dy[j]);
        }
    }
}

/* ---------- text ---------- */

static void parse(C2D_Text *t, const char *s)
{
    if (ui_font == NULL) {
        C2D_TextParse(t, text_buf, s);
    } else {
        C2D_TextFontParse(t, ui_font, text_buf, s);
    }
}

static float text_width(const char *s, float scale)
{
    C2D_Text t;
    float w = 0, h = 0;
    parse(&t, s);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

/* align: -1 left edge at x, 0 centred on x, 1 right edge at x; y is the
 * top of the line. bold draws it twice, half a pixel apart. */
static void text(const char *s, float x, float y, float scale, u32 c, int align, bool bold)
{
    if (s == NULL || s[0] == '\0') {
        return;
    }
    C2D_Text t;
    parse(&t, s);
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor | (align < 0 ? 0 : align == 0 ? C2D_AlignCenter : C2D_AlignRight);
    C2D_DrawText(&t, flags, x, y, next_z(), scale, scale, c);
    if (bold) {
        C2D_DrawText(&t, flags, x + 0.5f, y, next_z(), scale, scale, c);
    }
}

/* ---------- LCD digits ---------- */

/* Segments a-g of 0-9 and '-', bit 0 = a (top) ... bit 6 = g (middle). */
static const u8 seg_bits[11] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40 };

static void seg_digit(float x, float y, float h, u8 bits, u32 c)
{
    float w = h * 0.52f, t = h * 0.14f, half = h / 2, g = 0.5f;
    if (bits & 0x01) rect(x + t + g, y, w - 2 * t - 2 * g, t, c);
    if (bits & 0x02) rect(x + w - t, y + t + g, t, half - t - 2 * g, c);
    if (bits & 0x04) rect(x + w - t, y + half + g, t, half - t - 2 * g, c);
    if (bits & 0x08) rect(x + t + g, y + h - t, w - 2 * t - 2 * g, t, c);
    if (bits & 0x10) rect(x, y + half + g, t, half - t - 2 * g, c);
    if (bits & 0x20) rect(x, y + t + g, t, half - t - 2 * g, c);
    if (bits & 0x40) rect(x + t + g, y + half - t / 2, w - 2 * t - 2 * g, t, c);
}

/* Draws s ("123,456", "---,---") with unlit "8"s behind each digit; returns the width. */
static float seg_text(const char *s, float x, float y, float h, u32 c)
{
    float x0 = x, adv = h * 0.80f, t = h * 0.14f;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == ',' || *p == '.') {
            rect(x - h * 0.04f, y + h - t, t, t, c);
            if (*p == ',') {
                rect(x - h * 0.06f, y + h, t * 0.7f, t, c);
            }
            x += t + h * 0.12f;
            continue;
        }
        int k = *p == '-' ? 10 : *p >= '0' && *p <= '9' ? *p - '0' : -1;
        if (k >= 0) {
            seg_digit(x, y, h, 0x7F, COL_UNLIT);
            seg_digit(x, y, h, seg_bits[k], c);
        }
        x += adv;
    }
    return x - x0;
}

/* ---------- bottom screen ---------- */

/* A System Settings button (sprite) at r; pressed = darker and 1 px lower. */
static void button(home_rect_t r, size_t idx, bool pressed)
{
    float y = r.y + (pressed ? 1 : 0);
    nine(sprite(sysui, idx), r.x - 1, y - 1, r.w + 2, r.h + 2, 16);
    if (pressed) {
        rrect(r.x + 3, y + 3, r.w - 6, r.h - 6, 8, COL_PRESS);
    }
}

static void status_button(home_item_t item, const home_view_t *v, size_t icon, const char *label)
{
    home_rect_t r = home_item_rect(item);
    bool pressed = v->pressed == (int)item;
    button(r, sysui_button_medium_idx, pressed);
    float y = r.y + (pressed ? 1 : 0);
    float s = PX(15), tw = text_width(label, s);
    float x = r.x + (r.w - (22 + 8 + tw)) / 2;
    image(sprite(icons, icon), x, y + (r.h - 22) / 2);
    text(label, x + 30, y + 8, s, COL_INK, -1, true);
}

void home_draw(const home_view_t *v)
{
    if (target == NULL) {
        return;
    }
    z = 0.0f;
    C2D_TextBufClear(text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(target, COL_BG);
    C2D_SceneBegin(target);
    grid(HOME_SCREEN_W, HOME_SCREEN_H, 0);

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
        image(sprite(icons, lamp ? icons_diag_cel_idx : tile_icon[i]), cx - 15, y + 11);
        text(home_item_label((home_item_t)i), cx, y + 45, PX(14), lamp ? COL_AMBER : COL_INK, 0, true);
        if (lamp) {
            cel_outline(r.x, r.y, r.w, r.h);
        }
    }

    /* System Settings' bottom bar: "START Close" (tap to close), or a notice. */
    image(sprite(sysui, sysui_bar_idx), 0, HOME_BAR_Y);
    if (v->pressed == HOME_CLOSE) {
        rect(4, HOME_BAR_Y + 10, HOME_SCREEN_W - 8, HOME_SCREEN_H - HOME_BAR_Y - 10, COL_PRESS);
    }
    /* The label as System Settings sets it: 22.5 px tall (scale 0.75), centred in the
     * 30 px above the bottom edge, its shadow 1 px up behind it. */
    float ls = 0.75f, ly = HOME_SCREEN_H - 15 - 30 * ls / 2;
    if (v->notice != NULL) {
        text(v->notice, HOME_SCREEN_W / 2.0f, ly + 4, 0.5f, COL_INK, 0, false);
    } else {
        float bs = 0.42f;
        float badge_w = text_width("START", bs) + 12, badge_h = 15;
        float close_w = text_width("Close", ls);
        float x = (HOME_SCREEN_W - (badge_w + 7 + close_w)) / 2;
        float by = HOME_SCREEN_H - 15 - badge_h / 2;
        rrect(x, by, badge_w, badge_h, 3, COL_INK);
        text("START", x + badge_w / 2, by + badge_h / 2 - 30 * bs / 2, bs, RGB(70, 70, 70), 0, true);
        text("Close", x + badge_w + 7, ly - 1, ls, COL_SHADOW, -1, false);
        text("Close", x + badge_w + 7, ly, ls, COL_INK, -1, false);
    }

    C3D_FrameEnd(0);
}

/* ---------- top screen ---------- */

#define TOP_W 400
#define TOP_H 240

/* The HOME Menu's status bar, its dark version: sprites placed as layout HudMenu_00 does. */
static void status_bar(const home_status_t *s)
{
    image(sprite(sysui, s->connected && s->bars > 0 ? sysui_sb_signal_on_idx : sysui_sb_signal_off_idx), 1, 2);
    image(sprite(sysui, s->connected ? sysui_sb_pill_on_idx : sysui_sb_pill_off_idx), 26, 2);
    text(s->connected ? "Connected" : "Not Connected", 80, 2.5f, PX(12.5f), COL_INK, 0, true);

    image(sprite(sysui, sysui_sb_battery_idx), 368, 0);
    if (s->battery >= 0) {
        static const float width[6] = { 0, 4, 4, 9, 14, 16 };
        int level = s->battery > 5 ? 5 : s->battery;
        if (width[level] > 0) {
            image_part(sprite(sysui, level <= 1 ? sysui_sb_cells_low_idx : sysui_sb_cells_idx), 0, 0,
                       width[level], 8, 380, 6, width[level], 8);
        }
    }

    static const char *const days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    struct tm *tm = gmtime(&s->now);   /* the 3DS clock is local time */
    if (tm != NULL) {
        char date[24], clock[8];
        snprintf(date, sizeof date, "%d/%d (%s)", tm->tm_mon + 1, tm->tm_mday, days[tm->tm_wday % 7]);
        snprintf(clock, sizeof clock, "%d:%02d", tm->tm_hour, tm->tm_min);
        float ts = PX(14);
        float cw = text_width(clock, ts);
        text(clock, 363, 1, ts, COL_SB_TEXT, 1, true);
        text(date, 363 - cw - 9, 1, ts, COL_SB_TEXT, 1, true);
    }
}

static u32 tone_color(home_tone_t tone)
{
    switch (tone) {
    case HOME_TONE_DIM:     return COL_INK3;
    case HOME_TONE_RED:     return COL_RED_HI;
    case HOME_TONE_AMBER:   return COL_AMBER;
    default:                return COL_INK;
    }
}

void home_draw_top(const home_top_t *t, const home_status_t *s)
{
    if (top_target == NULL) {
        return;
    }
    z = 0.0f;
    C2D_TextBufClear(text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top_target, COL_BG);
    C2D_SceneBegin(top_target);
    grid(TOP_W, TOP_H, 20);
    status_bar(s);

    /* Title row: the app icon, GAUGE BOY: (white) OBD-II DELUXE (orange), centred. */
    float hs = PX(20);
    float w1 = text_width("GAUGE BOY: ", hs), w2 = text_width("OBD-II DELUXE", hs);
    float x = (TOP_W - (24 + 7 + w1 + w2)) / 2;
    image(sprite(icons, icons_app_idx), x, 25);
    text("GAUGE BOY: ", x + 31, 24, hs, COL_INK, -1, true);
    text("OBD-II DELUXE", x + 31 + w1, 24, hs, COL_ORANGE, -1, true);
    for (float dx = 22; dx < TOP_W - 18; dx += 8) {
        circle(dx, 58.5f, 1.25f, COL_DOT);
    }

    /* The info panel: fact rows, a tag and a value each. */
    rrect(17, 67, 366, 158, 6, COL_PANEL_EDGE);
    rrect(18, 68, 364, 156, 5, COL_PANEL);
    home_row_t rows[HOME_ROWS];
    home_top_rows(t, rows);
    /* Rows 26 px apart; the odometer's row is taller for its big digits. */
    static const float row_y[HOME_ROWS] = { 74, 100, 126, 152, 197 };
    static const float row_h[HOME_ROWS] = { 20, 20, 20, 40, 20 };
    for (int i = 0; i < HOME_ROWS; i++) {
        const home_row_t *r = &rows[i];
        float y = row_y[i], mid = y + row_h[i] / 2;
        rrect(27, mid - 7.5f, 74, 15, 4, COL_TAG);
        text(r->tag, 33, mid - 6.5f, PX(10.5f), COL_TAG_INK, -1, true);
        float vx = 109;
        u32 c = tone_color(r->tone);
        if (r->lamp) {
            bool on = r->tone == HOME_TONE_AMBER;
            if (on) {
                circle(vx + 4.5f, mid, 6.5f, COL_AMBER_GLOW);
            }
            circle(vx + 4.5f, mid, 4.5f, on ? COL_AMBER : COL_LAMP_OFF);
            vx += 14;
        }
        float vw;
        if (r->lcd) {
            vw = seg_text(r->value, vx, mid - 13, 26, c) + 4;
            text(r->detail, vx + vw + 2, mid - 2, PX(14), COL_INK3, -1, false);
            continue;
        }
        text(r->value, vx, mid - 10, PX(15), c, -1, true);
        vw = text_width(r->value, PX(15));
        text(r->detail, vx + vw + 6, mid - 6.5f, PX(12), COL_INK3, -1, false);
    }

    C3D_FrameEnd(0);
}

void *home_draw_top_target(void)
{
    return top_target;
}

void *home_draw_font_small(void)
{
    return font_small;
}
