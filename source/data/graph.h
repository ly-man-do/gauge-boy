#ifndef GAUGEBOY_GRAPH_H
#define GAUGEBOY_GRAPH_H

/*
 * Graph columns: samples (time, value) squeezed into a fixed number of
 * pixel columns over a time window, keeping each column's min and max so
 * spikes stay visible however many samples fall in one column. Also the
 * window's overall min / max / average. Platform-free; used by LOG's
 * graph replay (from a drive file) and the live history.
 */

#include <stdbool.h>
#include <stdint.h>

#define GRAPH_MAX_COLS 400   /* the top screen is 400 px wide */

typedef struct {
    double t0, t1;           /* the window, seconds */
    int ncols;
    float min[GRAPH_MAX_COLS], max[GRAPH_MAX_COLS];
    bool have[GRAPH_MAX_COLS];
    bool any;
    float vmin, vmax;        /* over the window */
    double sum;
    uint32_t n;              /* samples in the window */
} graph_cols_t;

/* An empty graph of ncols columns (capped at GRAPH_MAX_COLS) for t0..t1. */
void graph_cols_init(graph_cols_t *g, double t0, double t1, int ncols);

/* Adds a sample; ignored outside the window. */
void graph_cols_add(graph_cols_t *g, double t, float v);

/* The window's average; 0 without samples. */
float graph_cols_avg(const graph_cols_t *g);

/* A y pixel for v on a plot height px high (0 = top) with the scale
 * lo..hi (lo at the bottom). Clamped to the plot. */
int graph_y(float v, float lo, float hi, int height);

/* A rounded scale around vmin..vmax for the axis: lo and hi a little
 * outside, on a "nice" step (1, 2 or 5 times a power of ten). */
void graph_scale(float vmin, float vmax, float *lo, float *hi, float *step);

#endif
