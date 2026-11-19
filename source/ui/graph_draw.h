#ifndef GAUGEBOY_GRAPH_DRAW_H
#define GAUGEBOY_GRAPH_DRAW_H

/*
 * LOG's graph replay on the top screen, drawn with the UI kit (3DS only),
 * mockup docs/mockups/log-v1.html: one value against time in the night
 * theme, with min / max per pixel column, axes, labels and the window's
 * min / max / average.
 */

#include <stdbool.h>
#include <time.h>

#include "data/graph.h"

#define GRAPH_PLOT_W 320   /* pixel columns: load the graph with this many */

typedef struct {
    const char *title;          /* "RPM (rpm)" */
    const char *sub;            /* "11-16 17:12  2023 Subaru Ascent  4:32" */
    const char *place;          /* "value 2 of 7" */
    const graph_cols_t *g;      /* NULL while loading */
    const char *msg;            /* "Loading..." or an error, or NULL */
    int status_bars;            /* the status bar (kit_status_t) */
    bool connected;
    int battery;
    bool charging;
    time_t now;
} graph_view_t;

/* After the kit is up. False if it can't draw. */
bool graph_draw_init(void);

void graph_draw(const graph_view_t *v);

void graph_draw_exit(void);

#endif
