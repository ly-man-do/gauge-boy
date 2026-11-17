#ifndef GAUGEBOY_HOME_DRAW_H
#define GAUGEBOY_HOME_DRAW_H

/*
 * Draws the HOME bottom screen with citro2d (3DS only). The bottom screen
 * is otherwise a text console: HOME draws over the whole of it, and the
 * other screens redraw it in full when they open.
 */

#include <stdbool.h>

#include "home_layout.h"

/* Starts citro2d and loads the fonts from romfs. False if that failed:
 * HOME then falls back to the text layout. */
bool home_draw_init(void);

void home_draw(const home_view_t *v);

void home_draw_exit(void);

/* HOME's small font (a C2D_Font), for other citro2d screens; NULL if
 * home_draw_init failed. */
void *home_draw_font_small(void);

#endif
