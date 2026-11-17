#include "home_draw.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>

/* Colors (RGB): the owner-approved preview. */
/* Same as C2D_Color32(r, g, b, 0xFF), but usable in static tables. */
#define RGB(r, g, b) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | 0xFF000000u)
#define COL_BG       RGB(240, 232, 162)
#define COL_CARD     RGB(248, 246, 237)
#define COL_PRESSED  RGB(232, 226, 196)
#define COL_EDGE     RGB(194, 186, 150)
#define COL_SHADOW   RGB(214, 204, 140)
#define COL_INK      RGB(64, 60, 52)
#define COL_SUB      RGB(130, 122, 104)
#define COL_LIME     RGB(143, 212, 42)   /* brief 5b UI accent */
#define COL_LIME_HI  RGB(198, 242, 122)
#define COL_BAR      RGB(108, 108, 108)
#define COL_BAR_LINE RGB(140, 140, 140)
#define COL_BAR_DOWN RGB(84, 84, 84)
#define COL_WHITE    RGB(255, 255, 255)
#define COL_CEL_CARD RGB(255, 214, 120)
#define COL_AMBER    RGB(242, 168, 26)   /* CEL warning: signal color */
#define COL_OFF      RGB(176, 170, 150)
#define COL_LAMP_RIM RGB(120, 110, 90)
#define COL_NEEDLE   RGB(214, 64, 48)

static const u32 icon_colors[HOME_ITEM_COUNT] = {
    [HOME_LIVE]  = RGB(46, 124, 214),
    [HOME_DIAG]  = RGB(30, 160, 150),
    [HOME_TRIM]  = RGB(196, 72, 190),
    [HOME_FUN]   = RGB(220, 84, 60),
    [HOME_LOG]   = RGB(236, 150, 40),
    [HOME_SETUP] = RGB(180, 150, 40),
};

/* citro2d draws every font so that scale 1.0 is a 30 px line (about a
 * 22 px em for Nunito); these give the preview's sizes. */
#define SCALE_LABEL  0.77f   /* LIVE, DIAG ... (17 px) */
#define SCALE_SUB    0.47f   /* Dash gauges ... (10 px) */
#define SCALE_STATUS 0.68f   /* CEL off, Not connected (15 px) */
#define SCALE_CLOSE  0.80f   /* Close (18 px) */
#define SCALE_BADGE  0.41f   /* START (9 px) */

#define DEG(a) ((float)(a) * 3.14159265f / 180.0f)

static C3D_RenderTarget *target;
static C2D_Font font_big;     /* Nunito Bold 13 pt (about 17 px) */
static C2D_Font font_small;   /* Nunito Bold 8 pt (about 11 px) */
static C2D_TextBuf text_buf;
static bool romfs_ok, gpu_on, c2d_on;
static float z;               /* rising depth: later shapes draw on top */

static float next_z(void)
{
    z += 0.0005f;
    return z;
}

bool home_draw_init(void)
{
    romfs_ok = R_SUCCEEDED(romfsInit());
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        home_draw_exit();
        return false;
    }
    gpu_on = true;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        home_draw_exit();
        return false;
    }
    c2d_on = true;
    C2D_Prepare();
    font_big = C2D_FontLoad("romfs:/fonts/nunito-13.bcfnt");
    font_small = C2D_FontLoad("romfs:/fonts/nunito-8.bcfnt");
    text_buf = C2D_TextBufNew(512);
    /* The bottom console uses RGB565, so the GPU writes that format too. */
    target = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (font_big == NULL || font_small == NULL || text_buf == NULL || target == NULL) {
        home_draw_exit();
        return false;
    }
    C3D_RenderTargetSetOutput(target, GFX_BOTTOM, GFX_LEFT,
                              GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                              GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                              GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                              GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    return true;
}

void home_draw_exit(void)
{
    if (target != NULL) {
        C3D_RenderTargetDelete(target);
        target = NULL;
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

static void rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, next_z(), w, h, c);
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

/* A card: shadow, edge, fill; lime glow when selected. */
static void card(home_rect_t r, u32 fill, bool selected)
{
    rrect(r.x, r.y + 2, r.w, r.h, 8, COL_SHADOW);
    if (selected) {
        rrect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 10, COL_LIME);
        rrect(r.x, r.y, r.w, r.h, 8, COL_LIME_HI);
    } else {
        rrect(r.x, r.y, r.w, r.h, 8, COL_EDGE);
    }
    rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 7, fill);
}

/* ---------- text ---------- */

static float text_width(C2D_Font font, const char *s, float scale)
{
    C2D_Text t;
    float w = 0, h = 0;
    C2D_TextFontParse(&t, font, text_buf, s);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

/* x is the left edge, or the centre if centered; y is the top of the line. */
static void text(C2D_Font font, const char *s, float x, float y, float scale, u32 c, bool centered)
{
    C2D_Text t;
    C2D_TextFontParse(&t, font, text_buf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | (centered ? C2D_AlignCenter : 0), x, y, next_z(), scale,
                 scale, c);
}

/* ---------- icons (about 22 px, centred on cx, cy) ---------- */

static void icon_gauge(float cx, float cy, u32 c, u32 bg)
{
    (void)bg;
    arc(cx, cy, 11, 150, 390, 3, c);
    for (int a = 150; a <= 390; a += 60) {
        line(cx + 8 * cosf(DEG(a)), cy + 8 * sinf(DEG(a)), cx + 11 * cosf(DEG(a)),
             cy + 11 * sinf(DEG(a)), 2, c);
    }
    line(cx, cy, cx + 9 * cosf(DEG(305)), cy + 9 * sinf(DEG(305)), 3, COL_NEEDLE);
    circle(cx, cy, 3, c);
}

static void icon_magnifier(float cx, float cy, u32 c, u32 bg)
{
    line(cx + 2, cy + 2, cx + 9, cy + 9, 4, c);
    circle(cx + 9, cy + 9, 2, c);
    circle(cx - 3, cy - 3, 7.5f, c);
    circle(cx - 3, cy - 3, 4.5f, bg);
}

static void icon_sliders(float cx, float cy, u32 c, u32 bg)
{
    static const int knob[3] = { -4, 3, -1 };
    for (int i = 0; i < 3; i++) {
        float x = cx - 8 + i * 8;
        line(x, cy - 10, x, cy + 10, 2, c);
        rrect(x - 3.5f, cy + knob[i] - 3.5f, 7, 7, 2, c);
        rect(x - 1.5f, cy + knob[i] - 1.5f, 3, 3, bg);
    }
}

static void icon_flag(float cx, float cy, u32 c, u32 bg)
{
    rect(cx - 8, cy - 10, 18, 13.5f, c);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            if ((i + j) % 2 == 1) {
                rect(cx - 7 + i * 4, cy - 9 + j * 4, 4, 4, bg);
            }
        }
    }
    line(cx - 9, cy - 11, cx - 9, cy + 11, 3, c);
}

static void icon_document(float cx, float cy, u32 c, u32 bg)
{
    rrect(cx - 8, cy - 11, 16, 22, 2, c);
    rrect(cx - 6, cy - 9, 12, 18, 1, bg);
    for (int k = -5; k <= 5; k += 5) {
        line(cx - 4, cy + k, cx + 4, cy + k, 2, c);
    }
}

static void icon_gear(float cx, float cy, u32 c, u32 bg)
{
    for (int k = 0; k < 8; k++) {
        circle(cx + 9 * cosf(DEG(k * 45)), cy + 9 * sinf(DEG(k * 45)), 2.6f, c);
    }
    circle(cx, cy, 8, c);
    circle(cx, cy, 3, bg);
}

static void icon_lamp(float cx, float cy, bool on)
{
    circle(cx, cy, 6, COL_LAMP_RIM);
    circle(cx, cy, 5, on ? COL_AMBER : COL_OFF);
}

static void icon_wifi(float cx, float cy, u32 c)
{
    for (int r = 4; r <= 12; r += 4) {
        arc(cx, cy + 4, (float)r, 225, 315, 2, c);
    }
    circle(cx, cy + 4, 1.6f, c);
}

typedef void (*icon_fn)(float cx, float cy, u32 c, u32 bg);
static const icon_fn icons[HOME_ITEM_COUNT] = {
    [HOME_LIVE] = icon_gauge,   [HOME_DIAG] = icon_magnifier, [HOME_TRIM] = icon_sliders,
    [HOME_FUN] = icon_flag,     [HOME_LOG] = icon_document,   [HOME_SETUP] = icon_gear,
};

/* ---------- the screen ---------- */

/* A status card: icon then text, centred together. */
static void status_card(home_item_t item, const char *label, bool lamp, bool on, u32 fill,
                        const home_view_t *v)
{
    home_rect_t r = home_item_rect(item);
    card(r, v->pressed == (int)item ? COL_PRESSED : fill, v->selected == (int)item);
    float scale = SCALE_STATUS;
    float w = 22 + text_width(font_big, label, scale);
    float x = r.x + (r.w - w) / 2;
    if (lamp) {
        icon_lamp(x + 8, r.y + 16, on);
    } else {
        icon_wifi(x + 8, r.y + 13, on ? RGB(62, 172, 109) : COL_OFF);
    }
    text(font_big, label, x + 20, r.y + 6, scale, COL_INK, false);
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

    bool cel_on = v->cel_known && v->cel_on;
    status_card(HOME_CEL, home_cel_text(v), true, cel_on, cel_on ? COL_CEL_CARD : COL_CARD, v);
    status_card(HOME_ADAPTER, home_adapter_text(v), false, v->connected, COL_CARD, v);

    for (int i = HOME_LIVE; i <= HOME_SETUP; i++) {
        home_rect_t r = home_item_rect((home_item_t)i);
        u32 fill = v->pressed == i ? COL_PRESSED : COL_CARD;
        float cx = r.x + r.w / 2.0f;
        card(r, fill, v->selected == i);
        icons[i](cx, r.y + 20, icon_colors[i], fill);
        text(font_big, home_item_label((home_item_t)i), cx, r.y + 34, SCALE_LABEL, COL_INK, true);
        text(font_small, home_item_sub((home_item_t)i), cx, r.y + 56, SCALE_SUB, COL_SUB, true);
    }

    /* Bar: "START Close" (tap to close), or a notice. */
    rect(0, HOME_BAR_Y, HOME_SCREEN_W, HOME_SCREEN_H - HOME_BAR_Y,
         v->pressed == HOME_CLOSE ? COL_BAR_DOWN : COL_BAR);
    rect(0, HOME_BAR_Y, HOME_SCREEN_W, 1, COL_BAR_LINE);
    if (v->notice != NULL) {
        text(font_small, v->notice, HOME_SCREEN_W / 2.0f, HOME_BAR_Y + 9, SCALE_SUB, COL_WHITE, true);
    } else {
        float badge_w = text_width(font_small, "START", SCALE_BADGE) + 8;
        float close_w = text_width(font_big, "Close", SCALE_CLOSE);
        float x = (HOME_SCREEN_W - (badge_w + 6 + close_w)) / 2;
        rrect(x, HOME_BAR_Y + 9, badge_w, 14, 3, COL_WHITE);
        text(font_small, "START", x + 4, HOME_BAR_Y + 10, SCALE_BADGE, RGB(70, 70, 70), false);
        text(font_big, "Close", x + badge_w + 6, HOME_BAR_Y + 3, SCALE_CLOSE, COL_WHITE, false);
    }

    C3D_FrameEnd(0);
}

void *home_draw_font_small(void)
{
    return font_small;
}
