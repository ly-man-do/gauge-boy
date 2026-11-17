#ifndef GAUGEBOY_GRAPH_DRAW_H
#define GAUGEBOY_GRAPH_DRAW_H

/*
 * LOG's graph replay on the top screen, drawn with citro2d (3DS only):
 * one value against time, with min / max per pixel column, axes, labels
 * and the window's min / max / average. A plain look; the toy style comes
 * in M4. The top screen is otherwise the text console: the screen after
 * the graph clears it.
 */

#include <stdbool.h>

#include "data/graph.h"

#define GRAPH_PLOT_W 348   /* pixel columns: load the graph with this many */

typedef struct {
    const char *title;          /* "RPM (rpm)" */
    const char *sub;            /* "11-16 17:12  2023 Subaru Ascent  4:32" */
    const char *place;          /* "value 2 of 7" */
    const graph_cols_t *g;      /* NULL while loading */
    const char *msg;            /* "Loading..." or an error, or NULL */
} graph_view_t;

/* After home_draw_init (shares its citro2d and font). False if it can't. */
bool graph_draw_init(void);

void graph_draw(const graph_view_t *v);

void graph_draw_exit(void);

#endif
