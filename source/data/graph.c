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
    g->col_sum[c] += v;
    if (g->cnt[c] < UINT16_MAX) {
        g->cnt[c]++;
    }
}

float graph_cols_mean(const graph_cols_t *g, int c)
{
    return g->cnt[c] > 0 ? g->col_sum[c] / (float)g->cnt[c] : g->min[c];
}

int graph_curve_points(const graph_cols_t *g, float *xs, float *ys, int max)
{
    int n = 0;
    int last = -1;          /* the last column with a value */
    float last_v = 0.0f;
    for (int c = 0; c < g->ncols; c++) {
        if (!g->have[c]) {
            continue;
        }
        float v = graph_cols_mean(g, c);
        bool same = last >= 0 && fabsf(v - last_v) <= 1e-6f * (fabsf(last_v) + 1.0f);
        last = c;
        if (same) {
            continue;       /* the value held: not a new reading */
        }
        last_v = v;
        if (n < max) {
            xs[n] = (float)c;
            ys[n] = v;
            n++;
        }
    }
    if (n > 0 && n < max && (int)xs[n - 1] != last) {
        xs[n] = (float)last;  /* reach the end of the window */
        ys[n] = last_v;
        n++;
    }
    return n;
}

void graph_monotone_slopes(const float *x, const float *y, int n, float *m)
{
    if (n < 2) {
        if (n == 1) {
            m[0] = 0.0f;
        }
        return;
    }
    /* Secant slopes, then the Fritsch-Carlson limits. */
    for (int i = 0; i < n; i++) {
        float dl = i > 0 ? (y[i] - y[i - 1]) / (x[i] - x[i - 1]) : 0.0f;
        float dr = i < n - 1 ? (y[i + 1] - y[i]) / (x[i + 1] - x[i]) : 0.0f;
        if (i == 0) {
            m[i] = dr;
        } else if (i == n - 1) {
            m[i] = dl;
        } else if (dl * dr <= 0.0f) {
            m[i] = 0.0f;   /* a peak, a dip or flat: no overshoot */
        } else {
            m[i] = (dl + dr) / 2.0f;
        }
    }
    for (int i = 0; i < n - 1; i++) {
        float d = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
        if (d == 0.0f) {
            m[i] = m[i + 1] = 0.0f;
            continue;
        }
        float a = m[i] / d, b = m[i + 1] / d;
        float s = a * a + b * b;
        if (s > 9.0f) {
            float t = 3.0f / sqrtf(s);
            m[i] = t * a * d;
            m[i + 1] = t * b * d;
        }
    }
}

float graph_hermite(float x0, float y0, float x1, float y1, float m0, float m1, float x)
{
    float h = x1 - x0;
    if (h <= 0.0f) {
        return y0;
    }
    float t = (x - x0) / h, t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * m0 +
           (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * h * m1;
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
