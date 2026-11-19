#include "home_draw.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Colors: night theme, mockup v4 (docs/mockups/home-v4.html). */
/* Same as C2D_Color32(r, g, b, a), but usable in static tables. */
#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define RGB(r, g, b) RGBA(r, g, b, 0xFF)
#define COL_BG        RGB(0, 0, 0)
#define COL_GRID      RGB(10, 10, 10)
#define COL_WHITE     RGB(242, 250, 252)
#define COL_DIM       RGB(154, 154, 154)
#define COL_FAINT     RGB(92, 92, 92)
#define COL_UNLIT     RGB(32, 32, 32)        /* LCD segments that are off */
#define COL_RED       RGB(226, 48, 42)
#define COL_RED_HI    RGB(255, 90, 78)
#define COL_RED_GLOW  RGBA(226, 48, 42, 0x50)
#define COL_RED_HAZE  RGBA(226, 48, 42, 0x28)
#define COL_ORANGE    RGB(255, 138, 43)
#define COL_YELLOW    RGB(245, 208, 74)
#define COL_BLUE      RGB(90, 168, 245)
#define COL_GREY      RGB(168, 168, 168)
#define COL_GREEN     RGB(91, 216, 106)
#define COL_AMBER     RGB(242, 165, 43)
#define COL_AMBER_GLOW RGBA(242, 165, 43, 0x50)
#define COL_BTN_TOP   RGB(58, 58, 58)
#define COL_BTN_BOT   RGB(17, 17, 17)
#define COL_BTN_DOWN_TOP RGB(30, 30, 30)
#define COL_BTN_DOWN_BOT RGB(12, 12, 12)
#define COL_BTN_EDGE  RGB(74, 74, 74)
#define COL_BTN_SHINE RGBA(255, 255, 255, 0x30)
#define COL_BTN_MID   RGB(40, 40, 40)        /* behind icons: holes in the gear */
#define COL_PANEL_TOP RGB(20, 20, 20)
#define COL_PANEL_BOT RGB(10, 10, 10)
#define COL_PANEL_EDGE RGB(46, 46, 46)
#define COL_CHIP      RGB(22, 22, 22)
#define COL_CHIP_EDGE RGB(58, 58, 58)
#define COL_RULE      RGB(120, 120, 120)
#define COL_BAR_TOP   RGB(51, 51, 51)
#define COL_BAR_BOT   RGB(21, 21, 21)
#define COL_BAR_DOWN  RGB(14, 14, 14)
/* Status bar: the 3DS system bar's light grey and blue. */
#define COL_SB_TOP    RGB(250, 250, 250)
#define COL_SB_BOT    RGB(222, 222, 222)
#define COL_SB_LINE   RGB(168, 168, 168)
#define COL_SB_TEXT   RGB(64, 64, 64)
#define COL_SB_BLUE   RGB(61, 151, 232)
#define COL_SB_OFF    RGB(196, 196, 196)
#define COL_PILL_TOP  RGB(124, 196, 246)
#define COL_PILL_BOT  RGB(46, 134, 214)
#define COL_PILL_OFF_TOP RGB(200, 200, 200)
#define COL_PILL_OFF_BOT RGB(150, 150, 150)

static const u32 icon_colors[HOME_ITEM_COUNT] = {
    [HOME_LIVE] = COL_RED_HI, [HOME_DIAG] = COL_ORANGE, [HOME_TRIM] = COL_YELLOW,
    [HOME_TRACK] = COL_WHITE, [HOME_LOG] = COL_BLUE,    [HOME_SETUP] = COL_GREY,
};

#define DEG(a) ((float)(a) * 3.14159265f / 180.0f)

/* The system font draws a line about 30 px tall at scale 1 (an em of
 * about 24 px): px / 24 gives the mockup's CSS sizes. */
#define PX(px) ((float)(px) / 24.0f)

static C3D_RenderTarget *target, *top_target;
static C2D_Font font_big;     /* Nunito Bold 13 pt: fallback when the system font won't map */
static C2D_Font font_small;   /* Nunito Bold 8 pt: GRAPH's font */
static C2D_Font ui_font;      /* NULL = the system font */
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
    text_buf = C2D_TextBufNew(1024);
    target = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    top_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (font_small == NULL || text_buf == NULL || target == NULL || top_target == NULL ||
        (ui_font == NULL && font_big == NULL && R_FAILED(fontEnsureMapped()))) {
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

static u32 mix(u32 a, u32 b, float t)
{
    if (t <= 0.0f) {
        return a;
    }
    if (t >= 1.0f) {
        return b;
    }
    u32 out = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
        out |= (u32)(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

static void rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, next_z(), w, h, c);
}

/* Top-to-bottom gradient. */
static void grad(float x, float y, float w, float h, u32 top, u32 bot)
{
    C2D_DrawRectangle(x, y, next_z(), w, h, top, top, bot, bot);
}

static void circle(float x, float y, float r, u32 c)
{
    C2D_DrawCircleSolid(x, y, next_z(), r, c);
}

static void line(float x0, float y0, float x1, float y1, float width, u32 c)
{
    C2D_DrawLine(x0, y0, c, x1, y1, c, width, next_z());
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

/* A rounded rectangle with a top-to-bottom gradient (the 3DS button look). */
static void rrect_grad(float x, float y, float w, float h, float r, u32 top, u32 bot)
{
    float d = next_z();
    u32 a = mix(top, bot, r / h), b = mix(top, bot, (h - r) / h);
    C2D_DrawRectangle(x + r, y, d, w - 2 * r, h, top, top, bot, bot);
    C2D_DrawRectangle(x, y + r, d, r, h - 2 * r, a, a, b, b);
    C2D_DrawRectangle(x + w - r, y + r, d, r, h - 2 * r, a, a, b, b);
    C2D_DrawCircleSolid(x + r, y + r, d, r, a);
    C2D_DrawCircleSolid(x + w - r, y + r, d, r, a);
    C2D_DrawCircleSolid(x + r, y + h - r, d, r, b);
    C2D_DrawCircleSolid(x + w - r, y + h - r, d, r, b);
}

/* Rounded on top only (the START Close bar), with a gradient. */
static void rrect_top(float x, float y, float w, float h, float r, u32 top, u32 bot)
{
    float d = next_z();
    u32 a = mix(top, bot, r / h);
    C2D_DrawCircleSolid(x + r, y + r, d, r, mix(top, a, 0.5f));
    C2D_DrawCircleSolid(x + w - r, y + r, d, r, mix(top, a, 0.5f));
    d = next_z();   /* over the circles' lower halves */
    C2D_DrawRectangle(x + r, y, d, w - 2 * r, r, top, top, a, a);
    C2D_DrawRectangle(x, y + r, d, w, h - r, a, a, bot, bot);
}

/* Thick arc from a0 to a1 degrees (0 = right, 90 = down), round joints. */
static void arc(float cx, float cy, float r, float a0, float a1, float width, u32 c)
{
    int steps = (int)((a1 - a0) / 10.0f) + 1;
    float px = cx + r * cosf(DEG(a0)), py = cy + r * sinf(DEG(a0));
    circle(px, py, width / 2, c);
    for (int i = 1; i <= steps; i++) {
        float a = a0 + (a1 - a0) * (float)i / (float)steps;
        float x = cx + r * cosf(DEG(a)), y = cy + r * sinf(DEG(a));
        line(px, py, x, y, width, c);
        circle(x, y, width / 2, c);
        px = x;
        py = y;
    }
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

/* ---------- LCD digits (drawn, like a dash cluster's) ---------- */

/* Segments a-g of 0-9 and '-', bit 0 = a (top) ... bit 6 = g (middle). */
static const u8 seg_bits[11] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40 };

static float seg_w(float h)
{
    return h * 0.52f;
}

static void seg_digit(float x, float y, float h, u8 bits, u32 c)
{
    float w = seg_w(h), t = h * 0.13f, half = h / 2;
    float g = 0.6f;   /* gap at the joints */
    if (bits & 0x01) rect(x + t + g, y, w - 2 * t - 2 * g, t, c);                  /* a */
    if (bits & 0x02) rect(x + w - t, y + t + g, t, half - t - 2 * g, c);           /* b */
    if (bits & 0x04) rect(x + w - t, y + half + g, t, half - t - 2 * g, c);        /* c */
    if (bits & 0x08) rect(x + t + g, y + h - t, w - 2 * t - 2 * g, t, c);          /* d */
    if (bits & 0x10) rect(x, y + half + g, t, half - t - 2 * g, c);                /* e */
    if (bits & 0x20) rect(x, y + t + g, t, half - t - 2 * g, c);                   /* f */
    if (bits & 0x40) rect(x + t + g, y + half - t / 2, w - 2 * t - 2 * g, t, c);   /* g */
}

/* Draws s ("123,456", "12.4", "--.-") with unlit "8"s behind each digit;
 * returns the width. measure_only draws nothing. */
static float seg_text(const char *s, float x, float y, float h, u32 c, bool measure_only)
{
    float x0 = x, adv = seg_w(h) + h * 0.16f, t = h * 0.13f;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == '.' || *p == ',') {
            if (!measure_only) {
                rect(x - h * 0.06f, y + h - t, t, t, c);
                if (*p == ',') {
                    line(x - h * 0.06f + t, y + h - 0.5f, x - h * 0.06f, y + h + t, t * 0.7f, c);
                }
            }
            x += t + h * 0.04f;
            continue;
        }
        int k = *p == '-' ? 10 : *p >= '0' && *p <= '9' ? *p - '0' : -1;
        if (k >= 0 && !measure_only) {
            seg_digit(x, y, h, 0x7F, COL_UNLIT);
            seg_digit(x, y, h, seg_bits[k], c);
        }
        x += adv;
    }
    return x - x0;
}

/* ---------- icons (about 22 px, centred on cx, cy) ---------- */

static void icon_gauge(float cx, float cy, u32 c)
{
    arc(cx, cy + 1, 10, 150, 390, 2.5f, c);
    line(cx, cy + 1, cx + 7 * cosf(DEG(305)), cy + 1 + 7 * sinf(DEG(305)), 2.5f, c);
    circle(cx, cy + 1, 2.5f, c);
}

static void icon_engine(float cx, float cy, u32 c)
{
    float w = 2.2f;
    line(cx - 6, cy - 4, cx + 6, cy - 4, w, c);       /* block */
    line(cx + 6, cy - 4, cx + 6, cy + 6, w, c);
    line(cx + 6, cy + 6, cx - 3, cy + 6, w, c);
    line(cx - 3, cy + 6, cx - 6, cy + 3, w, c);
    line(cx - 6, cy + 3, cx - 6, cy - 4, w, c);
    line(cx - 3, cy - 8, cx + 3, cy - 8, w, c);       /* cap */
    line(cx, cy - 8, cx, cy - 4, w, c);
    line(cx - 10, cy - 1, cx - 10, cy + 4, w, c);     /* intake */
    line(cx - 10, cy + 1.5f, cx - 6, cy + 1.5f, w, c);
    line(cx + 6, cy - 1, cx + 10, cy - 3, w, c);      /* fan */
    line(cx + 10, cy - 3, cx + 10, cy + 6, w, c);
    line(cx + 10, cy + 6, cx + 6, cy + 4, w, c);
}

static void icon_sliders(float cx, float cy, u32 c)
{
    static const float knob[3] = { -3, 4, -1 };
    for (int i = 0; i < 3; i++) {
        float y = cy - 7 + i * 7;
        line(cx - 10, y, cx + 10, y, 2, c);
        circle(cx + knob[i], y, 3.2f, c);
        circle(cx + knob[i], y, 1.4f, COL_BTN_MID);
    }
}

static void icon_checkered(float cx, float cy, u32 c)
{
    float x = cx - 7, y = cy - 9, sq = 5;
    line(cx - 9, cy - 11, cx - 9, cy + 11, 2.2f, c);
    rect(x, y, sq * 3, sq * 2, RGB(17, 17, 17));
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            if ((i + j) % 2 == 0) {
                rect(x + i * sq, y + j * sq, sq, sq, c);
            }
        }
    }
    line(x, y, x + 3 * sq, y, 1.4f, c);
    line(x + 3 * sq, y, x + 3 * sq, y + 2 * sq, 1.4f, c);
    line(x + 3 * sq, y + 2 * sq, x, y + 2 * sq, 1.4f, c);
}

static void icon_chart(float cx, float cy, u32 c)
{
    line(cx - 10, cy + 7, cx - 4, cy - 1, 2.2f, c);
    line(cx - 4, cy - 1, cx + 1, cy + 3, 2.2f, c);
    line(cx + 1, cy + 3, cx + 5, cy - 4, 2.2f, c);
    line(cx + 5, cy - 4, cx + 10, cy + 1, 2.2f, c);
    line(cx - 11, cy + 10, cx + 11, cy + 10, 2, c);
}

static void icon_gear(float cx, float cy, u32 c)
{
    for (int k = 0; k < 8; k++) {
        circle(cx + 8.5f * cosf(DEG(k * 45 + 22)), cy + 8.5f * sinf(DEG(k * 45 + 22)), 2.4f, c);
    }
    circle(cx, cy, 8, c);
    circle(cx, cy, 5.5f, COL_BTN_MID);
    circle(cx, cy, 3, c);
    circle(cx, cy, 1.5f, COL_BTN_MID);
}

static void icon_garage(float cx, float cy, u32 c)
{
    float w = 2;
    line(cx - 10, cy - 2, cx, cy - 9, w, c);
    line(cx, cy - 9, cx + 10, cy - 2, w, c);
    line(cx - 10, cy - 2, cx - 10, cy + 9, w, c);
    line(cx + 10, cy - 2, cx + 10, cy + 9, w, c);
    line(cx - 11, cy + 9, cx + 11, cy + 9, w, c);
    line(cx - 6, cy + 9, cx - 6, cy + 1, w, c);
    line(cx + 6, cy + 9, cx + 6, cy + 1, w, c);
    line(cx - 6, cy + 1, cx + 6, cy + 1, w, c);
    line(cx - 6, cy + 4, cx + 6, cy + 4, 1.5f, c);
    line(cx - 6, cy + 6.8f, cx + 6, cy + 6.8f, 1.5f, c);
}

static void icon_wifi(float cx, float cy, u32 c, bool off)
{
    for (int r = 4; r <= 12; r += 4) {
        arc(cx, cy + 6, (float)r, 225, 315, 2, c);
    }
    circle(cx, cy + 6, 1.6f, c);
    if (off) {
        line(cx - 9, cy - 6, cx + 9, cy + 9, 2.2f, c);
    }
}

typedef void (*icon_fn)(float cx, float cy, u32 c);
static const icon_fn icons[HOME_ITEM_COUNT] = {
    [HOME_LIVE] = icon_gauge,      [HOME_DIAG] = icon_engine, [HOME_TRIM] = icon_sliders,
    [HOME_TRACK] = icon_checkered, [HOME_LOG] = icon_chart,   [HOME_SETUP] = icon_gear,
};

/* ---------- buttons ---------- */

/* A raised 3DS-style button: dark lip, edge, gradient face and a shine
 * line; a glowing outline when selected (red, or amber for the CEL). */
static void button(home_rect_t r, bool pressed, bool selected, bool amber)
{
    float y = r.y + (pressed ? 1 : 0);
    rrect(r.x, r.y + 2, r.w, r.h, 8, RGB(0, 0, 0));
    u32 ring = amber ? COL_AMBER : COL_RED;
    if (selected || amber) {
        rrect(r.x - 4, y - 4, r.w + 8, r.h + 8, 12, amber ? COL_AMBER_GLOW : COL_RED_HAZE);
        rrect(r.x - 3, y - 3, r.w + 6, r.h + 6, 11, amber ? COL_AMBER_GLOW : COL_RED_GLOW);
        rrect(r.x - 2, y - 2, r.w + 4, r.h + 4, 10, ring);
    }
    rrect(r.x, y, r.w, r.h, 8, selected || amber ? ring : COL_BTN_EDGE);
    rrect_grad(r.x + 1, y + 1, r.w - 2, r.h - 2, 7, pressed ? COL_BTN_DOWN_TOP : COL_BTN_TOP,
               pressed ? COL_BTN_DOWN_BOT : COL_BTN_BOT);
    if (!pressed) {
        rect(r.x + 7, y + 1.5f, r.w - 14, 1, COL_BTN_SHINE);
    }
}

/* A status button: icon then two lines, centred together. */
static void status_button(home_item_t item, const home_view_t *v, const char *t1, const char *t2,
                          u32 t2_color, bool suggest)
{
    home_rect_t r = home_item_rect(item);
    bool pressed = v->pressed == (int)item;
    button(r, pressed, v->selected == (int)item || suggest, false);
    float y = r.y + (pressed ? 1 : 0);
    float s1 = PX(12), s2 = PX(9.5f);
    float tw = fmaxf(text_width(t1, s1), text_width(t2, s2));
    float x = r.x + (r.w - (22 + 8 + tw)) / 2;
    if (item == HOME_GARAGE) {
        icon_garage(x + 11, y + 20, v->car != NULL ? COL_WHITE : COL_DIM);
    } else {
        icon_wifi(x + 11, y + 18, v->connected ? COL_GREEN : COL_RED_HI, !v->connected);
    }
    text(t1, x + 30, y + 6, s1, COL_WHITE, -1, true);
    text(t2, x + 30, y + 21, s2, t2_color, -1, false);
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
    status_button(HOME_GARAGE, v, HOME_GARAGE_TEXT, v->car != NULL ? v->car : "No car yet",
                  COL_DIM, false);
    status_button(HOME_ADAPTER, v, "Adapter", home_adapter_text(v),
                  v->connected ? COL_GREEN : COL_RED_HI, !v->connected && v->selected < 0);

    for (int i = HOME_LIVE; i <= HOME_SETUP; i++) {
        home_rect_t r = home_item_rect((home_item_t)i);
        bool lamp = i == HOME_DIAG && cel_on;   /* the panel's CEL lamp */
        bool pressed = v->pressed == i;
        button(r, pressed, v->selected == i, lamp);
        float cx = r.x + r.w / 2.0f, y = r.y + (pressed ? 1 : 0);
        icons[i](cx, y + 26, lamp ? COL_AMBER : icon_colors[i]);
        const char *label = i == HOME_DIAG ? home_diag_label(v) : home_item_label((home_item_t)i);
        text(label, cx, y + 44, PX(13), lamp ? COL_AMBER : COL_WHITE, 0, true);
    }

    /* Bar: "START Close" (tap to close), or a notice. */
    bool down = v->pressed == HOME_CLOSE;
    rrect_top(4, HOME_BAR_Y, HOME_SCREEN_W - 8, 32, 14, RGB(58, 58, 58), RGB(58, 58, 58));
    rrect_top(5, HOME_BAR_Y + 1, HOME_SCREEN_W - 10, 31, 13, down ? COL_BAR_DOWN : COL_BAR_TOP,
              COL_BAR_BOT);
    if (v->notice != NULL) {
        text(v->notice, HOME_SCREEN_W / 2.0f, HOME_BAR_Y + 10, PX(10), COL_WHITE, 0, false);
    } else {
        float bs = PX(8.5f), cs = PX(16);
        float badge_w = text_width("START", bs) + 16;
        float close_w = text_width("Close", cs);
        float x = (HOME_SCREEN_W - (badge_w + 6 + close_w)) / 2;
        rrect(x, HOME_BAR_Y + 10, badge_w, 13, 3, COL_WHITE);
        text("START", x + badge_w / 2 - 0.25f, HOME_BAR_Y + 10.5f, bs, RGB(21, 21, 21), 0, true);
        text("Close", x + badge_w + 6, HOME_BAR_Y + 5, cs, COL_WHITE, -1, true);
    }

    C3D_FrameEnd(0);
}

/* ---------- top screen ---------- */

#define TOP_W 400
#define TOP_H 240

static void status_bar(const home_status_t *s)
{
    grad(0, 0, TOP_W, 18, COL_SB_TOP, COL_SB_BOT);
    rect(0, 18, TOP_W, 1, COL_SB_LINE);

    /* Signal bars */
    for (int i = 0; i < 4; i++) {
        float h = 3 + i * 2.4f;
        rect(6 + i * 3, 14 - h, 2, h, i < s->bars ? COL_SB_BLUE : COL_SB_OFF);
    }
    /* Adapter pill */
    const char *pill = s->connected ? "Connected" : "Not connected";
    float ps = PX(9.5f), pw = text_width(pill, ps) + 20;
    rrect(20, 3, pw, 12, 6, s->connected ? RGB(36, 110, 180) : RGB(130, 130, 130));
    rrect_grad(21, 4, pw - 2, 10, 5, s->connected ? COL_PILL_TOP : COL_PILL_OFF_TOP,
               s->connected ? COL_PILL_BOT : COL_PILL_OFF_BOT);
    text(pill, 20 + pw / 2, 2.5f, ps, COL_WHITE, 0, true);

    /* Battery */
    float bx = TOP_W - 24, by = 4.5f;
    rrect(bx, by, 18, 9, 2, COL_SB_TEXT);
    rect(bx + 1, by + 1, 16, 7, COL_SB_TOP);
    rect(bx + 18, by + 2.5f, 2, 4, COL_SB_TEXT);
    int lit = s->battery < 0 ? 0 : (s->battery + 1) / 2 + (s->battery >= 5 ? 1 : 0);
    if (lit > 4) {
        lit = 4;
    }
    for (int i = 0; i < 4; i++) {
        u32 c = i < lit ? (s->battery <= 1 ? COL_RED : COL_SB_BLUE) : RGB(232, 232, 232);
        rect(bx + 2 + i * 3.6f, by + 2, 3, 5, c);
    }
    if (s->charging) {
        line(bx + 10, by + 1, bx + 7, by + 5, 1.5f, RGB(250, 196, 30));
        line(bx + 7, by + 5, bx + 11, by + 4, 1.5f, RGB(250, 196, 30));
        line(bx + 11, by + 4, bx + 8, by + 8, 1.5f, RGB(250, 196, 30));
    }

    /* Date and clock: "11/18 (Wed)  21:12" */
    static const char *const days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    struct tm *tm = gmtime(&s->now);   /* the 3DS clock is local time */
    if (tm != NULL) {
        char date[24], clock[8];
        snprintf(date, sizeof date, "%d/%d (%s)", tm->tm_mon + 1, tm->tm_mday,
                 days[tm->tm_wday % 7]);
        snprintf(clock, sizeof clock, "%d:%02d", tm->tm_hour, tm->tm_min);
        float ts = PX(11);
        float cw = text_width(clock, ts);
        text(clock, bx - 8, 1.5f, ts, COL_SB_TEXT, 1, true);
        text(date, bx - 8 - cw - 8, 1.5f, ts, COL_SB_TEXT, 1, true);
    }
}

static void panel(float x, float y, float w, float h)
{
    rrect(x, y, w, h, 8, COL_PANEL_EDGE);
    rrect_grad(x + 1, y + 1, w - 2, h - 2, 7, COL_PANEL_TOP, COL_PANEL_BOT);
    rect(x + 7, y + 1, w - 14, 1, RGB(58, 58, 58));
}

/* A chip: rounded, an optional lamp, right edge at x; returns its width. */
static float chip(const char *s, float right, float y, u32 c, u32 edge, bool lamp, u32 lamp_c)
{
    float ts = PX(10), w = text_width(s, ts) + 14 + (lamp ? 11 : 0);
    float x = right - w;
    rrect(x, y, w, 16, 8, edge);
    rrect(x + 1, y + 1, w - 2, 14, 7, COL_CHIP);
    if (lamp) {
        circle(x + 10.5f, y + 8, 3.5f, lamp_c);
    }
    text(s, x + 7 + (lamp ? 11 : 0), y + 1.5f, ts, c, -1, true);
    return w;
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
    grid(TOP_W, TOP_H, 19);
    status_bar(s);

    /* Header: GAUGE BOY: OBD-II DELUXE, white then orange, centred. */
    float hs = PX(14);
    float w1 = text_width("GAUGE BOY: ", hs), w2 = text_width("OBD-II DELUXE", hs);
    float hx = (TOP_W - w1 - w2) / 2;
    text("GAUGE BOY: ", hx, 21, hs, COL_WHITE, -1, true);
    text("OBD-II DELUXE", hx + w1, 21, hs, COL_ORANGE, -1, true);
    for (float x = 12; x < TOP_W - 12; x += 6) {
        rect(x, 43, 4, 2, x >= TOP_W - 12 - 64 ? COL_RED : COL_RULE);
    }

    /* Car card */
    char l1[48], l2[48], codes[16], detail[48];
    home_top_car(t, l1, sizeof l1, l2, sizeof l2);
    home_top_codes(t, codes, sizeof codes);
    home_top_detail(t, detail, sizeof detail);
    panel(12, 52, 376, 58);
    text(l1, 24, 58, PX(17), t->have_car ? COL_WHITE : COL_DIM, -1, true);
    text(l2, 24, 81, PX(11), t->have_car ? COL_DIM : COL_FAINT, -1, false);
    bool cel_on = t->cel_known && t->cel_on;
    u32 chip_c = t->codes >= 0 ? RGB(218, 218, 218) : COL_FAINT;
    float cw = chip(codes, 376, 61, chip_c, COL_CHIP_EDGE, false, 0);
    chip(home_top_cel(t), 376 - cw - 4, 61, cel_on ? COL_AMBER : t->cel_known ? chip_c : COL_FAINT,
         cel_on ? COL_AMBER : COL_CHIP_EDGE, true, cel_on ? COL_AMBER : RGB(58, 58, 58));
    text(detail, 376, 83, PX(9.5f), COL_FAINT, 1, false);

    /* Odometer */
    char odo[24], unit_text[24];
    home_top_odometer(t, odo, sizeof odo);
    panel(12, 117, 184, 84);
    text("ODOMETER", 24, 123, PX(9), COL_DIM, -1, true);
    float oh = 24;
    float ow = seg_text(odo[0] ? odo : "---,---", 24, 147, oh, odo[0] ? COL_WHITE : COL_FAINT,
                        false);
    text(home_top_dist_unit(t), 24 + ow + 4, 158, PX(11), odo[0] ? COL_DIM : COL_FAINT, -1, false);

    /* Last drive and economy */
    char dist[16], when[16], econ[16], econ_label[24];
    home_top_last(t, dist, sizeof dist, when, sizeof when);
    home_top_economy(t, econ, sizeof econ, econ_label, sizeof econ_label);
    panel(204, 117, 184, 84);
    text("LAST DRIVE", 216, 123, PX(9), COL_DIM, -1, true);
    float dw = seg_text(dist[0] ? dist : "--.-", 216, 143, 20, dist[0] ? COL_WHITE : COL_FAINT,
                        false);
    snprintf(unit_text, sizeof unit_text, "%s%s%s", home_top_dist_unit(t), when[0] ? " " : "", when);
    text(unit_text, 216 + dw + 6, 150, PX(11), dist[0] ? COL_DIM : COL_FAINT, -1, false);
    if (econ[0]) {
        float ew = seg_text(econ, 216, 172, 17, COL_RED_HI, false);
        text(econ_label, 216 + ew + 6, 175, PX(10), COL_DIM, -1, false);
    } else if (!dist[0]) {
        text("No drives logged yet", 216, 174, PX(10), COL_FAINT, -1, false);
    }

    text("Not for use by the driver while driving", TOP_W / 2.0f, 217, PX(10), COL_FAINT, 0, false);

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
