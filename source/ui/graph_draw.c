#include "graph_draw.h"

#include <math.h>
#include <stdio.h>

#include "icons.h"
#include "kit.h"

/* The night theme (brief 5b), drawn with the UI kit: the status bar, the title
 * with LOG's icon, then a panel with the drive, the plot and the times, and the
 * window's min / max / average under it. LOG's blue for the line. */
#define COL_LINE  KIT_RGB(90, 169, 255)
#define COL_BAND  KIT_RGBA(90, 169, 255, 0x38)
#define COL_GRID  KIT_RGB(36, 36, 36)
#define COL_AXIS  KIT_RGB(84, 84, 84)
#define COL_LABEL KIT_RGB(122, 122, 122)

#define PANEL_X 17
#define PANEL_Y 64
#define PANEL_W 366
#define PANEL_H 150
#define PLOT_X  ((KIT_TOP_W - GRAPH_PLOT_W) / 2 + 14)   /* room for the value labels */
#define PLOT_Y  86
#define PLOT_H  108

static bool ready;

bool graph_draw_init(void)
{
    ready = kit_top_target() != NULL;
    return ready;
}

void graph_draw_exit(void)
{
    ready = false;
}

static void small(const char *s, float x, float y, u32 c, int align)
{
    kit_text(s, x, y, KIT_PX(9), c, align, true);
}

/* v's y on the plot, unrounded (smooth), clamped to the plot. */
static float plot_y(float v, float lo, float hi)
{
    float f = hi > lo ? (v - lo) / (hi - lo) : 0.0f;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    return PLOT_Y + (1.0f - f) * (float)(PLOT_H - 1);
}

/* A line w px thick from (x0, y0) to (x1, y1). */
static void line(float x0, float y0, float x1, float y1, float w, u32 c)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) {
        return;
    }
    float nx = -dy / len * w / 2, ny = dx / len * w / 2;
    kit_quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

static void time_text(double s, char *out, size_t len)
{
    unsigned t = s > 0 ? (unsigned)(s + 0.5) : 0u;
    if (t >= 3600) {
        snprintf(out, len, "%u:%02u:%02u", t / 3600, t / 60 % 60, t % 60);
    } else {
        snprintf(out, len, "%u:%02u", t / 60, t % 60);
    }
}

static void value_text(float v, float step, char *out, size_t len)
{
    snprintf(out, len, step >= 1.0f ? "%.0f" : step >= 0.1f ? "%.1f" : "%.2f", (double)v);
}

/* "Min 712" with the label grey and the value bold, from x; returns the width. */
static float stat(const char *label, const char *value, float x, float y)
{
    float ls = KIT_PX(11), vs = KIT_PX(13);
    kit_text(label, x, y + 2, ls, KIT_INK3, -1, false);
    float lw = kit_text_width(label, ls) + 5;
    kit_text(value, x + lw, y, vs, KIT_INK, -1, true);
    return lw + kit_text_width(value, vs);
}

void graph_draw(const graph_view_t *v)
{
    if (!ready) {
        return;
    }
    kit_begin(true, 20);
    kit_status_t s;
    s.bars = v->status_bars;
    s.connected = v->connected;
    s.battery = v->battery;
    s.charging = v->charging;
    s.now = v->now;
    kit_status_bar(&s);
    kit_title(icons_log_idx, v->title);

    kit_rrect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 6, KIT_PANEL_EDGE);
    kit_rrect(PANEL_X + 1, PANEL_Y + 1, PANEL_W - 2, PANEL_H - 2, 5, KIT_PANEL);
    kit_text(v->sub, PANEL_X + 10, PANEL_Y + 5, KIT_PX(10), KIT_INK3, -1, false);
    kit_text(v->place, PANEL_X + PANEL_W - 10, PANEL_Y + 5, KIT_PX(10), KIT_INK3, 1, false);

    const graph_cols_t *g = v->g;
    if (g == NULL || !g->any || v->msg != NULL) {
        const char *m = v->msg != NULL ? v->msg : "No values in this part of the drive.";
        kit_text(m, KIT_TOP_W / 2.0f, PLOT_Y + PLOT_H / 2.0f - 8, KIT_PX(13), KIT_INK2, 0, true);
        kit_end();
        return;
    }

    float lo, hi, step;
    graph_scale(g->vmin, g->vmax, &lo, &hi, &step);
    char buf[80];
    /* Grid lines and value labels. */
    for (float y = lo; y <= hi + step * 0.5f; y += step) {
        int py = PLOT_Y + graph_y(y, lo, hi, PLOT_H);
        kit_rect(PLOT_X, (float)py, GRAPH_PLOT_W, 1, COL_GRID);
        value_text(y, step, buf, sizeof buf);
        small(buf, PLOT_X - 4, (float)py - 5, COL_LABEL, 1);
    }
    kit_rect(PLOT_X, PLOT_Y, 1, PLOT_H, COL_AXIS);
    kit_rect(PLOT_X, PLOT_Y + PLOT_H - 1, GRAPH_PLOT_W, 1, COL_AXIS);

    /* A faint band: each column's lowest to highest value, so short spikes
     * stay visible under the smoothed curve. */
    for (int c = 0; c < g->ncols; c++) {
        if (g->have[c] && g->max[c] > g->min[c]) {
            float y0 = plot_y(g->max[c], lo, hi);
            float y1 = plot_y(g->min[c], lo, hi);
            kit_rect((float)(PLOT_X + c), y0, 1, y1 - y0 + 1, COL_BAND);
        }
    }

    /* The curve: a monotone cubic through the column averages (flat where
     * a value held, no overshoot), drawn a pixel at a time. */
    static float xs[GRAPH_MAX_COLS], ys[GRAPH_MAX_COLS], ms[GRAPH_MAX_COLS];
    int np = graph_curve_points(g, xs, ys, GRAPH_MAX_COLS);
    graph_monotone_slopes(xs, ys, np, ms);
    if (np == 1) {
        kit_circle(PLOT_X + xs[0], plot_y(ys[0], lo, hi), 1.8f, COL_LINE);
    }
    for (int i = 0; i + 1 < np; i++) {
        int steps = (int)(xs[i + 1] - xs[i]);
        if (steps < 1) {
            steps = 1;
        }
        float px = PLOT_X + xs[i], py = plot_y(ys[i], lo, hi);
        for (int k = 1; k <= steps; k++) {
            float x = xs[i] + (xs[i + 1] - xs[i]) * (float)k / (float)steps;
            float val = graph_hermite(xs[i], ys[i], xs[i + 1], ys[i + 1], ms[i], ms[i + 1], x);
            float nx = PLOT_X + x, ny = plot_y(val, lo, hi);
            line(px, py, nx, ny, 2.0f, COL_LINE);
            px = nx;
            py = ny;
        }
    }

    /* Time axis: start, middle, end of the window. */
    double t_mid = (g->t0 + g->t1) / 2;
    float ty = PLOT_Y + PLOT_H + 4;
    time_text(g->t0, buf, sizeof buf);
    small(buf, PLOT_X, ty, COL_LABEL, -1);
    time_text(t_mid, buf, sizeof buf);
    small(buf, PLOT_X + GRAPH_PLOT_W / 2.0f, ty, COL_LABEL, 0);
    time_text(g->t1, buf, sizeof buf);
    small(buf, PLOT_X + GRAPH_PLOT_W, ty, COL_LABEL, 1);

    char a[16], b[16], c[16];
    value_text(g->vmin, step / 10.0f, a, sizeof a);
    value_text(g->vmax, step / 10.0f, b, sizeof b);
    value_text(graph_cols_avg(g), step / 10.0f, c, sizeof c);
    stat("Min", a, 30, 220);
    stat("Max", b, 158, 220);
    stat("Average", c, 270, 220);
    kit_end();
}
