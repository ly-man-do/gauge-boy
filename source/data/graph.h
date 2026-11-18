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
    float col_sum[GRAPH_MAX_COLS];   /* per column, for its average */
    uint16_t cnt[GRAPH_MAX_COLS];
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

/* Column c's average (have[c] must be true). */
float graph_cols_mean(const graph_cols_t *g, int c);

/*
 * Points for a smooth curve: one per reading. A run of equal column
 * averages is one value held between readings (slow buses refresh a PID
 * every second or so), so only its first column counts: the curve passes
 * through each new reading instead of stepping. The last column is kept
 * too, so the curve reaches the window's end. xs are column numbers.
 * Returns the count (at most max).
 */
int graph_curve_points(const graph_cols_t *g, float *xs, float *ys, int max);

/*
 * Fritsch-Carlson slopes for a monotone cubic through (x[i], y[i]), x
 * rising: the curve never overshoots its neighbouring points, so it
 * invents no peaks. m gets n slopes.
 */
void graph_monotone_slopes(const float *x, const float *y, int n, float *m);

/* The cubic Hermite curve between (x0, y0) and (x1, y1) with slopes m0, m1,
 * at x (x0 <= x <= x1). */
float graph_hermite(float x0, float y0, float x1, float y1, float m0, float m1, float x);

/* A y pixel for v on a plot height px high (0 = top) with the scale
 * lo..hi (lo at the bottom). Clamped to the plot. */
int graph_y(float v, float lo, float hi, int height);

/* A rounded scale around vmin..vmax for the axis: lo and hi a little
 * outside, on a "nice" step (1, 2 or 5 times a power of ten). */
void graph_scale(float vmin, float vmax, float *lo, float *hi, float *step);

#endif
