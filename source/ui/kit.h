#ifndef GAUGEBOY_KIT_H
#define GAUGEBOY_KIT_H

/*
 * The shared UI kit for the graphics screens (3DS only): one citro2d setup
 * (both screens' render targets, the system font, the sprite sheets) and the
 * drawing helpers every screen uses: shapes, the 3DS's own system pieces
 * (gfx/sysui, tools/make_sysui.py), Gauge Boy's icons (gfx/icons), text in
 * the system font, LCD digits, the HOME Menu status bar and the Back tab.
 *
 * Night theme (brief 5b). The screens that aren't graphics yet are text
 * consoles: a graphics screen draws over the whole of its screen.
 */

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <time.h>

/* Same as C2D_Color32(r, g, b, a), but usable in static tables. */
#define KIT_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))
#define KIT_RGB(r, g, b) KIT_RGBA(r, g, b, 0xFF)

#define KIT_BG        KIT_RGB(0, 0, 0)
#define KIT_GRID      KIT_RGB(10, 10, 10)
#define KIT_INK       KIT_RGB(242, 244, 245)   /* text */
#define KIT_INK2      KIT_RGB(200, 200, 200)
#define KIT_INK3      KIT_RGB(156, 156, 156)   /* secondary text */
#define KIT_DOT       KIT_RGB(85, 85, 85)
#define KIT_PANEL     KIT_RGB(18, 18, 18)
#define KIT_PANEL_EDGE KIT_RGB(44, 44, 44)
#define KIT_TAG       KIT_RGB(46, 46, 46)
#define KIT_TAG_INK   KIT_RGB(214, 214, 214)
#define KIT_UNLIT     KIT_RGB(24, 24, 24)       /* LCD segments that are off */
#define KIT_RED       KIT_RGB(226, 48, 42)
#define KIT_RED_HI    KIT_RGB(255, 90, 78)
#define KIT_ORANGE    KIT_RGB(255, 138, 43)
#define KIT_AMBER     KIT_RGB(242, 165, 43)
#define KIT_AMBER_GLOW KIT_RGBA(242, 165, 43, 0x60)
#define KIT_PRESS     KIT_RGBA(0, 0, 0, 0x50)   /* over a held button */
#define KIT_SHADOW    KIT_RGB(20, 20, 20)       /* label shadows on the grey bars */

/* The system font draws a line about 30 px tall at scale 1 (an em of about
 * 24 px): KIT_PX(px) gives a CSS-like font size. */
#define KIT_PX(px) ((float)(px) / 24.0f)

#define KIT_TOP_W 400
#define KIT_BOTTOM_W 320
#define KIT_H 240

/* Starts citro2d and loads the fonts and sprite sheets. False if that failed:
 * the screens then fall back to their text layouts. */
bool kit_init(void);
void kit_exit(void);

/* A frame on one screen: clears it to the night background with its grid
 * (below grid_top px). Draw, then kit_end. */
void kit_begin(bool top, float grid_top);
void kit_end(void);

/* Repaints the background (and its grid, from y 0) between y0 and y1: covers
 * what scrolled past the edge of a list. */
void kit_grid_band(float w, float y0, float y1);

/* ---------- shapes ---------- */

void kit_rect(float x, float y, float w, float h, u32 c);
void kit_circle(float x, float y, float r, u32 c);
void kit_rrect(float x, float y, float w, float h, float r, u32 c);
/* A rounded outline `width` thick just outside (x, y, w, h). */
void kit_ring(float x, float y, float w, float h, float r, float width, u32 c);
void kit_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c);

/* ---------- sprites ---------- */

C2D_Image kit_sysui(size_t idx);   /* sysui_*_idx (gfx/sysui) */
C2D_Image kit_icon(size_t idx);    /* icons_*_idx (gfx/icons) */
void kit_image(C2D_Image img, float x, float y);
/* Part (sx, sy, sw, sh) of a sprite drawn into (dx, dy, dw, dh). */
void kit_image_part(C2D_Image img, float sx, float sy, float sw, float sh, float dx, float dy,
                    float dw, float dh);
/* A sprite stretched to (x, y, w, h): corners of size c kept, edges stretched. */
void kit_nine(C2D_Image img, float x, float y, float w, float h, float c);

/* The pressed ("_dn") version of a touchable system piece (the piece itself if none). */
size_t kit_pressed(size_t idx);

/* A System Settings button (sysui_button_large_idx / _medium_idx) at (x, y, w, h);
 * pressed = its darker version, 1 px lower. */
void kit_button(size_t idx, float x, float y, float w, float h, bool pressed);

/* ---------- text (the system font) ---------- */

float kit_text_width(const char *s, float scale);
/* align: -1 left edge at x, 0 centred on x, 1 right edge at x; y is the top of
 * the line. bold draws it twice, half a pixel apart. */
void kit_text(const char *s, float x, float y, float scale, u32 c, int align, bool bold);

/* LCD digits ("123,456", "--.-", " 42") h px tall with unlit "8"s behind (a space is
 * just the unlit "8"); returns the width. */
float kit_lcd(const char *s, float x, float y, float h, u32 c);
float kit_lcd_width(const char *s, float h);

/* ---------- system pieces ---------- */

/* The HOME Menu's status bar (its dark version) across the top screen. */
typedef struct {
    int bars;            /* Wi-Fi signal 0-4 lit bars */
    bool connected;      /* adapter connected: the pill */
    int battery;         /* 0-5 (PTMU level), -1 if unknown */
    bool charging;
    time_t now;          /* local time for the date and clock */
} kit_status_t;

void kit_status_bar(const kit_status_t *s);

/* The title row under the status bar: an icon (icons_*_idx, -1 none) and text,
 * centred, and the dotted rule under it. */
void kit_title(int icon, const char *text);
/* The dotted rule across the top screen at y. */
void kit_rule(float y);

/* A bar label with its key, centred on cx, its line's middle at mid: "(B) Back",
 * "[START] Close" (key caps as 3dsDL draws them). */
void kit_key_label(const char *key, const char *label, float cx, float mid);

/* System Settings' Back tab in the bottom-left corner ("(B) Back"). */
void kit_back_tab(bool pressed);

/* For screens with their own citro2d code (GRAPH). */
void *kit_top_target(void);
void *kit_font_small(void);

#endif
