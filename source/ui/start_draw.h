#ifndef GAUGEBOY_START_DRAW_H
#define GAUGEBOY_START_DRAW_H

/*
 * The first screens (3DS only), mockup docs/mockups/start-v1.html: the startup
 * safety notice (brief: not for use by the driver while driving) and, on an
 * Old 3DS / 2DS, the unsupported-device screen. Drawn with the UI kit before
 * the app starts; main.c falls back to text if the kit can't start.
 */

#include <stdbool.h>

/* Draws both screens once. unsupported: the Old 3DS screen; pulse: 0.35 - 1, the
 * tap ring's brightness. */
void start_draw(bool unsupported, const char *version, float pulse);

#endif
