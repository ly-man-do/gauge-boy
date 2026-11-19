#ifndef GAUGEBOY_HOME_DRAW_H
#define GAUGEBOY_HOME_DRAW_H

/*
 * Draws HOME with citro2d (3DS only): the bottom screen's buttons and the
 * top screen's status bar, car card and LCD panels. Both screens are
 * otherwise text consoles: HOME draws over the whole of each, and the
 * other screens redraw them in full when they open.
 *
 * Night theme, mockup v4 (docs/mockups/home-v4.html). Text uses the 3DS
 * system font, loaded from the console (brief 1.21.0); the romfs Nunito
 * font is only a fallback and GRAPH's font.
 */

#include <stdbool.h>
#include <time.h>

#include "home_layout.h"
#include "home_top.h"

/* The top screen's status bar, like the 3DS system bar. */
typedef struct {
    int bars;            /* Wi-Fi signal 0-4 lit bars */
    bool connected;      /* adapter connected: the pill */
    int battery;         /* 0-5 (PTMU level), -1 if unknown */
    bool charging;
    time_t now;          /* local time for the date and clock */
} home_status_t;

/* Starts citro2d and loads the fonts. False if that failed: HOME then
 * falls back to the text layout. */
bool home_draw_init(void);

void home_draw(const home_view_t *v);

void home_draw_top(const home_top_t *t, const home_status_t *s);

void home_draw_exit(void);

/* The top screen's render target (a C3D_RenderTarget *), shared with
 * GRAPH: a screen has only one linked target. NULL before init. */
void *home_draw_top_target(void);

/* HOME's small romfs font (a C2D_Font), for other citro2d screens; NULL if
 * home_draw_init failed. */
void *home_draw_font_small(void);

#endif
