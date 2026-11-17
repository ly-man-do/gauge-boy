#include "graph.h"

#include <math.h>
#include <string.h>

void graph_cols_init(graph_cols_t *g, double t0, double t1, int ncols)
{
    memset(g, 0, sizeof *g);
    g->t0 = t0;
    g->t1 = t1 > t0 ? t1 : t0 + 1.0;
    g->ncols = ncols < 1 ? 1 : ncols > GRAPH_MAX_COLS ? GRAPH_MAX_COLS : ncols;
}

void graph_cols_add(graph_cols_t *g, double t, float v)
{
    if (t < g->t0 || t > g->t1 || isnan(v)) {
        return;
    }
    int c = (int)((t - g->t0) / (g->t1 - g->t0) * g->ncols);
    if (c >= g->ncols) {
        c = g->ncols - 1;
    }
    if (!g->have[c]) {
        g->have[c] = true;
        g->min[c] = g->max[c] = v;
    } else {
        g->min[c] = v < g->min[c] ? v : g->min[c];
        g->max[c] = v > g->max[c] ? v : g->max[c];
    }
    if (!g->any) {
        g->any = true;
        g->vmin = g->vmax = v;
    } else {
        g->vmin = v < g->vmin ? v : g->vmin;
        g->vmax = v > g->vmax ? v : g->vmax;
    }
    g->sum += v;
    g->n++;
}

float graph_cols_avg(const graph_cols_t *g)
{
    return g->n > 0 ? (float)(g->sum / g->n) : 0.0f;
}

int graph_y(float v, float lo, float hi, int height)
{
    if (hi <= lo || height <= 1) {
        return height > 0 ? height - 1 : 0;
    }
    float f = (v - lo) / (hi - lo);
    int y = (int)lroundf((1.0f - f) * (float)(height - 1));
    return y < 0 ? 0 : y > height - 1 ? height - 1 : y;
}

void graph_scale(float vmin, float vmax, float *lo, float *hi, float *step)
{
    if (vmax < vmin) {
        float t = vmin;
        vmin = vmax;
        vmax = t;
    }
    float range = vmax - vmin;
    if (range < 1e-6f) {
        range = fabsf(vmax) > 1.0f ? fabsf(vmax) * 0.1f : 1.0f;   /* a flat line */
    }
    /* About 4 steps over the range. */
    float raw = range / 4.0f;
    float p = powf(10.0f, floorf(log10f(raw)));
    float m = raw / p;
    float s = (m <= 1.0f ? 1.0f : m <= 2.0f ? 2.0f : m <= 5.0f ? 5.0f : 10.0f) * p;
    *step = s;
    *lo = floorf(vmin / s) * s;
    *hi = ceilf(vmax / s) * s;
    if (*hi - *lo < s) {
        *hi = *lo + s;
    }
}
