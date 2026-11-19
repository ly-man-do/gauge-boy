#include "kit.h"

#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "icons.h"
#include "icons_t3x.h"
#include "sysui.h"
#include "sysui_t3x.h"

/*
 * Sprites are drawn untinted (Azahar doesn't emulate the procedural textures
 * citro2d tints with); System Settings' buttons are 9-sliced to size.
 */

#define SB_TEXT KIT_RGB(205, 205, 205)   /* the HOME Menu bar's dark version */

static C3D_RenderTarget *bottom_target, *top_target;
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

bool kit_init(void)
{
    romfs_ok = R_SUCCEEDED(romfsInit());
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        kit_exit();
        return false;
    }
    gpu_on = true;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2)) {
        kit_exit();
        return false;
    }
    c2d_on = true;
    C2D_Prepare();
    font_big = C2D_FontLoad("romfs:/fonts/nunito-13.bcfnt");
    font_small = C2D_FontLoad("romfs:/fonts/nunito-8.bcfnt");
    ui_font = R_SUCCEEDED(fontEnsureMapped()) ? NULL : font_big;
    sysui = C2D_SpriteSheetLoadFromMem(sysui_t3x, sysui_t3x_size);
    icons = C2D_SpriteSheetLoadFromMem(icons_t3x, icons_t3x_size);
    text_buf = C2D_TextBufNew(1536);
    bottom_target = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    top_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (font_small == NULL || sysui == NULL || icons == NULL || text_buf == NULL ||
        bottom_target == NULL || top_target == NULL) {
        kit_exit();
        return false;
    }
    set_output(bottom_target, GFX_BOTTOM);
    set_output(top_target, GFX_TOP);
    return true;
}

void kit_exit(void)
{
    if (bottom_target != NULL) {
        C3D_RenderTargetDelete(bottom_target);
        bottom_target = NULL;
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

/* The faint grid behind both screens. */
static void grid(float w, float h, float top)
{
    for (float x = 18; x < w; x += 20) {
        kit_rect(x, top, 2, h - top, KIT_GRID);
    }
    for (float y = top + 18; y < h; y += 20) {
        kit_rect(0, y, w, 2, KIT_GRID);
    }
}

void kit_begin(bool top, float grid_top)
{
    C3D_RenderTarget *t = top ? top_target : bottom_target;
    z = 0.0f;
    C2D_TextBufClear(text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(t, KIT_BG);
    C2D_SceneBegin(t);
    grid(top ? KIT_TOP_W : KIT_BOTTOM_W, KIT_H, grid_top);
}

void kit_end(void)
{
    C3D_FrameEnd(0);
}

/* ---------- shapes ---------- */

void kit_rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, next_z(), w, h, c);
}

void kit_circle(float x, float y, float r, u32 c)
{
    C2D_DrawCircleSolid(x, y, next_z(), r, c);
}

void kit_rrect(float x, float y, float w, float h, float r, u32 c)
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

void kit_ring(float x, float y, float w, float h, float r, float width, u32 c)
{
    kit_rect(x + r, y - width, w - 2 * r, width, c);
    kit_rect(x + r, y + h, w - 2 * r, width, c);
    kit_rect(x - width, y + r, width, h - 2 * r, c);
    kit_rect(x + w, y + r, width, h - 2 * r, c);
    static const float start[4] = { 180, 270, 90, 0 };   /* TL, TR, BL, BR */
    for (int k = 0; k < 4; k++) {
        float cx = (k & 1) ? x + w - r : x + r, cy = (k & 2) ? y + h - r : y + r;
        float ro = r + width;
        for (int s = 0; s < 6; s++) {
            float t0 = (start[k] + s * 15.0f) * 3.14159265f / 180.0f;
            float t1 = (start[k] + (s + 1) * 15.0f) * 3.14159265f / 180.0f;
            kit_quad(cx + r * cosf(t0), cy + r * sinf(t0), cx + ro * cosf(t0), cy + ro * sinf(t0),
                     cx + ro * cosf(t1), cy + ro * sinf(t1), cx + r * cosf(t1), cy + r * sinf(t1), c);
        }
    }
}

void kit_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c)
{
    float d = next_z();
    C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, d);
    C2D_DrawTriangle(x0, y0, c, x2, y2, c, x3, y3, c, d);
}

/* ---------- sprites ---------- */

C2D_Image kit_sysui(size_t idx)
{
    return C2D_SpriteSheetGetImage(sysui, idx);
}

C2D_Image kit_icon(size_t idx)
{
    return C2D_SpriteSheetGetImage(icons, idx);
}

void kit_image(C2D_Image img, float x, float y)
{
    C2D_DrawImageAt(img, x, y, next_z(), NULL, 1.0f, 1.0f);
}

void kit_image_part(C2D_Image img, float sx, float sy, float sw, float sh, float dx, float dy,
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

void kit_nine(C2D_Image img, float x, float y, float w, float h, float c)
{
    float sw = img.subtex->width, sh = img.subtex->height;
    float sx[4] = { 0, c, sw - c, sw }, sy[4] = { 0, c, sh - c, sh };
    float dx[4] = { x, x + c, x + w - c, x + w }, dy[4] = { y, y + c, y + h - c, y + h };
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            kit_image_part(img, sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j], dx[i], dy[j],
                           dx[i + 1] - dx[i], dy[j + 1] - dy[j]);
        }
    }
}

void kit_button(size_t idx, float x, float y, float w, float h, bool pressed)
{
    y += pressed ? 1 : 0;
    kit_nine(kit_sysui(idx), x - 1, y - 1, w + 2, h + 2, 16);
    if (pressed) {
        kit_rrect(x + 3, y + 3, w - 6, h - 6, 8, KIT_PRESS);
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

float kit_text_width(const char *s, float scale)
{
    C2D_Text t;
    float w = 0, h = 0;
    parse(&t, s);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

void kit_text(const char *s, float x, float y, float scale, u32 c, int align, bool bold)
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
    if (bits & 0x01) kit_rect(x + t + g, y, w - 2 * t - 2 * g, t, c);
    if (bits & 0x02) kit_rect(x + w - t, y + t + g, t, half - t - 2 * g, c);
    if (bits & 0x04) kit_rect(x + w - t, y + half + g, t, half - t - 2 * g, c);
    if (bits & 0x08) kit_rect(x + t + g, y + h - t, w - 2 * t - 2 * g, t, c);
    if (bits & 0x10) kit_rect(x, y + half + g, t, half - t - 2 * g, c);
    if (bits & 0x20) kit_rect(x, y + t + g, t, half - t - 2 * g, c);
    if (bits & 0x40) kit_rect(x + t + g, y + half - t / 2, w - 2 * t - 2 * g, t, c);
}

static float lcd(const char *s, float x, float y, float h, u32 c, bool draw)
{
    float x0 = x, adv = h * 0.80f, t = h * 0.14f;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == ',' || *p == '.') {
            if (draw) {
                kit_rect(x - h * 0.04f, y + h - t, t, t, c);
                if (*p == ',') {
                    kit_rect(x - h * 0.06f, y + h, t * 0.7f, t, c);
                }
            }
            x += t + h * 0.12f;
            continue;
        }
        int k = *p == '-' ? 10 : *p >= '0' && *p <= '9' ? *p - '0' : *p == ' ' ? 11 : -1;
        if (k >= 0 && draw) {
            seg_digit(x, y, h, 0x7F, KIT_UNLIT);   /* a space is an unlit "8" */
            if (k < 11) {
                seg_digit(x, y, h, seg_bits[k], c);
            }
        }
        x += adv;
    }
    return x - x0;   /* includes the gap after the last digit */
}

float kit_lcd(const char *s, float x, float y, float h, u32 c)
{
    return lcd(s, x, y, h, c, true);
}

float kit_lcd_width(const char *s, float h)
{
    return lcd(s, 0, 0, h, 0, false);
}

/* ---------- system pieces ---------- */

void kit_status_bar(const kit_status_t *s)
{
    kit_image(kit_sysui(s->connected && s->bars > 0 ? sysui_sb_signal_on_idx : sysui_sb_signal_off_idx), 1, 2);
    kit_image(kit_sysui(s->connected ? sysui_sb_pill_on_idx : sysui_sb_pill_off_idx), 26, 2);
    kit_text(s->connected ? "Connected" : "Not Connected", 80, 2.5f, KIT_PX(12.5f), KIT_INK, 0, true);

    kit_image(kit_sysui(sysui_sb_battery_idx), 368, 0);
    if (s->battery >= 0) {
        static const float width[6] = { 0, 4, 4, 9, 14, 16 };
        int level = s->battery > 5 ? 5 : s->battery;
        if (width[level] > 0) {
            kit_image_part(kit_sysui(level <= 1 ? sysui_sb_cells_low_idx : sysui_sb_cells_idx), 0, 0,
                           width[level], 8, 380, 6, width[level], 8);
        }
    }

    static const char *const days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    struct tm *tm = gmtime(&s->now);   /* the 3DS clock is local time */
    if (tm != NULL) {
        char date[24], clock[8];
        snprintf(date, sizeof date, "%d/%d (%s)", tm->tm_mon + 1, tm->tm_mday, days[tm->tm_wday % 7]);
        snprintf(clock, sizeof clock, "%d:%02d", tm->tm_hour, tm->tm_min);
        float ts = KIT_PX(14);
        float cw = kit_text_width(clock, ts);
        kit_text(clock, 363, 1, ts, SB_TEXT, 1, true);
        kit_text(date, 363 - cw - 9, 1, ts, SB_TEXT, 1, true);
    }
}

void kit_grid_band(float w, float y0, float y1)
{
    kit_rect(0, y0, w, y1 - y0, KIT_BG);
    for (float x = 18; x < w; x += 20) {
        kit_rect(x, y0, 2, y1 - y0, KIT_GRID);
    }
    for (float y = 18; y < y1; y += 20) {
        if (y + 2 > y0) {
            float a = y < y0 ? y0 : y;
            kit_rect(0, a, w, y + 2 - a, KIT_GRID);
        }
    }
}

void kit_rule(float y)
{
    for (float dx = 22; dx < KIT_TOP_W - 18; dx += 8) {
        kit_circle(dx, y, 1.25f, KIT_DOT);
    }
}

void kit_title(int icon, const char *text)
{
    float hs = KIT_PX(20);
    float tw = kit_text_width(text, hs);
    C2D_Image img = kit_icon(icon >= 0 ? (size_t)icon : 0);
    float isz = icon >= 0 ? img.subtex->width : 0;
    float iw = icon >= 0 ? isz + 7 : 0;
    float x = (KIT_TOP_W - (iw + tw)) / 2;
    if (icon >= 0) {
        kit_image(img, x, 37 - img.subtex->height / 2.0f);
    }
    kit_text(text, x + iw, 24, hs, KIT_INK, -1, true);
    kit_rule(58.5f);
}

void kit_back_tab(bool pressed)
{
    /* System Settings' left tab: a "B" button cap and "Back", centred in the
     * 30 px above the bottom edge like the bar's "START Close". */
    C2D_Image tab = kit_sysui(sysui_tab_left_idx);
    float y = KIT_H - tab.subtex->height;
    kit_image(tab, 0, y);
    if (pressed) {
        kit_rect(4, y + 10, 112, KIT_H - y - 10, KIT_PRESS);
    }
    float ls = 0.75f, bs = 0.42f, dy = pressed ? 1 : 0;
    float ly = KIT_H - 15 - 30 * ls / 2 + dy;
    float badge_w = kit_text_width("B", bs) + 10, badge_h = 15;
    float back_w = kit_text_width("Back", ls);
    float x = (118 - (badge_w + 6 + back_w)) / 2;
    float by = KIT_H - 15 - badge_h / 2 + dy;
    kit_rrect(x, by, badge_w, badge_h, 3, KIT_INK);
    kit_text("B", x + badge_w / 2, by + badge_h / 2 - 30 * bs / 2, bs, KIT_RGB(70, 70, 70), 0, true);
    kit_text("Back", x + badge_w + 6, ly - 1, ls, KIT_SHADOW, -1, false);
    kit_text("Back", x + badge_w + 6, ly, ls, KIT_INK, -1, false);
}

void *kit_top_target(void)
{
    return top_target;
}

void *kit_font_small(void)
{
    return font_small;
}
