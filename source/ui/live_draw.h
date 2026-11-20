#ifndef GAUGEBOY_LIVE_DRAW_H
#define GAUGEBOY_LIVE_DRAW_H

/*
 * Draws LIVE with the UI kit (3DS only), mockup v5 (docs/mockups/live-v5.html):
 * the Dashboard and a PID view's rows on the top screen; the view list and a
 * PID view's controls on the bottom. Layout and touch zones are in live_ui.h.
 */

#include <stdbool.h>

#include "data/live_view.h"
#include "kit.h"
#include "live_ui.h"

/* Top: the Dashboard (tach, speed, coolant and battery, CEL, odometer). */
void live_draw_dash(const live_dash_t *d, bool cel_known, bool cel_on, const kit_status_t *s);

/* Bottom: the view list scrolled by scroll px; pressed = the row held (-1 none);
 * grip_pressed = the scroll bar's grip is held. */
void live_draw_list(const char *const *names, int n, int scroll, int pressed, bool grip_pressed,
                    bool back_pressed);

/* Top: a PID view's title ("ENGINE") and rows (n 0 = waiting for the first values), page
 * page of them (LIVE_ROWS_PAGE a page). */
void live_draw_view_top(const char *title, const live_row_t *rows, int n, int page, bool connected,
                        const kit_status_t *s);

typedef struct {
    int pids;                     /* PIDs polled */
    float rate;                   /* requests a second */
    bool recording;
    unsigned long long rec_ms;    /* time recorded */
    bool custom;                  /* Custom: the Edit PIDs button */
    int page, pages;              /* the top screen's page of values (Page when pages > 1) */
    live_hit_t pressed;           /* the control held */
} live_controls_t;

/* Bottom: a PID view's controls (arrows, Stop | Record, Edit PIDs, Back). */
void live_draw_view_bottom(const live_controls_t *c);

#endif
