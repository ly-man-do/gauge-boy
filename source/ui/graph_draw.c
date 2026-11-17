#include "graph_draw.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <math.h>
#include <stdio.h>

#include "home_draw.h"

#define RGB(r, g, b) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | 0xFF000000u)
#define COL_BG    RGB(28, 28, 32)
#define COL_TEXT  RGB(236, 236, 236)
#define COL_DIM   RGB(150, 150, 150)
#define COL_GRID  RGB(64, 64, 72)
#define COL_AXIS  RGB(120, 120, 128)
#define COL_LINE  RGB(143, 212, 42)   /* brief 5b lime accent */

#define PLOT_X 44
#define PLOT_Y 44
#define PLOT_H 160
#define SCALE_TITLE 0.62f
#define SCALE_SMALL 0.42f

static C3D_RenderTarget *target;
static C2D_TextBuf text_buf;
static C2D_Font font;
static float z;

static float next_z(void)
{
    z += 0.0005f;
    return z;
}

bool graph_draw_init(void)
{
    font = (C2D_Font)home_draw_font_small();
    if (font == NULL) {
        return false;
    }
    text_buf = C2D_TextBufNew(1024);
    /* The top console uses RGB565, so the GPU writes that format too. */
    target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (text_buf == NULL || target == NULL) {
        graph_draw_exit();
        return false;
    }
    C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT,
                              GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                              GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                              GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                              GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    return true;
}

void graph_draw_exit(void)
{
    if (target != NULL) {
        C3D_RenderTargetDelete(target);
        target = NULL;
    }
    if (text_buf != NULL) {
        C2D_TextBufDelete(text_buf);
        text_buf = NULL;
    }
}

static void text(const char *s, float x, float y, float scale, u32 c, int align)
{
    C2D_Text t;
    C2D_TextFontParse(&t, font, text_buf, s);
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor | (align < 0 ? 0 : align == 0 ? C2D_AlignCenter : C2D_AlignRight);
    C2D_DrawText(&t, flags, x, y, next_z(), scale, scale, c);
}

static void rect(float x, float y, float w, float h, u32 c)
{
    C2D_DrawRectSolid(x, y, next_z(), w, h, c);
}

static void time_text(double s, char *out, size_t len)
{
    unsigned t = s > 0 ? (unsigned)(s + 0.5) : 0u;
    snprintf(out, len, "%u:%02u", t / 60, t % 60);
}

static void value_text(float v, float step, char *out, size_t len)
{
    snprintf(out, len, step >= 1.0f ? "%.0f" : step >= 0.1f ? "%.1f" : "%.2f", (double)v);
}

void graph_draw(const graph_view_t *v)
{
    if (target == NULL) {
        return;
    }
    z = 0.0f;
    C2D_TextBufClear(text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(target, COL_BG);
    C2D_SceneBegin(target);

    text(v->title, 8, 4, SCALE_TITLE, COL_TEXT, -1);
    text(v->place, 392, 6, SCALE_SMALL, COL_DIM, 1);
    text(v->sub, 8, 24, SCALE_SMALL, COL_DIM, -1);

    const graph_cols_t *g = v->g;
    if (g == NULL || !g->any || v->msg != NULL) {
        text(v->msg != NULL ? v->msg : "No values in this part of the drive.", 200,
             PLOT_Y + PLOT_H / 2 - 8, SCALE_SMALL, COL_TEXT, 0);
        C3D_FrameEnd(0);
        return;
    }

    float lo, hi, step;
    graph_scale(g->vmin, g->vmax, &lo, &hi, &step);
    char buf[80];
    /* Grid lines and value labels. */
    for (float y = lo; y <= hi + step * 0.5f; y += step) {
        int py = PLOT_Y + graph_y(y, lo, hi, PLOT_H);
        rect(PLOT_X, (float)py, GRAPH_PLOT_W, 1, COL_GRID);
        value_text(y, step, buf, sizeof buf);
        text(buf, PLOT_X - 4, (float)py - 6, SCALE_SMALL, COL_DIM, 1);
    }
    rect(PLOT_X, PLOT_Y, 1, PLOT_H, COL_AXIS);
    rect(PLOT_X, PLOT_Y + PLOT_H - 1, GRAPH_PLOT_W, 1, COL_AXIS);

    /* The values: min..max per column, joined between columns. */
    int prev_x = -1;
    float prev_y = 0;
    for (int c = 0; c < g->ncols; c++) {
        if (!g->have[c]) {
            continue;
        }
        float x = (float)(PLOT_X + c);
        float y0 = (float)(PLOT_Y + graph_y(g->max[c], lo, hi, PLOT_H));
        float y1 = (float)(PLOT_Y + graph_y(g->min[c], lo, hi, PLOT_H));
        rect(x, y0, 1, y1 - y0 + 1, COL_LINE);
        float mid = (y0 + y1) / 2;
        if (prev_x >= 0 && c - prev_x > 1) {
            C2D_DrawLine((float)(PLOT_X + prev_x), prev_y, COL_LINE, x, mid, COL_LINE, 1.5f,
                         next_z());
        }
        prev_x = c;
        prev_y = mid;
    }

    /* Time axis: start, middle, end of the window. */
    double t_mid = (g->t0 + g->t1) / 2;
    time_text(g->t0, buf, sizeof buf);
    text(buf, PLOT_X, PLOT_Y + PLOT_H + 3, SCALE_SMALL, COL_DIM, -1);
    time_text(t_mid, buf, sizeof buf);
    text(buf, PLOT_X + GRAPH_PLOT_W / 2.0f, PLOT_Y + PLOT_H + 3, SCALE_SMALL, COL_DIM, 0);
    time_text(g->t1, buf, sizeof buf);
    text(buf, PLOT_X + GRAPH_PLOT_W, PLOT_Y + PLOT_H + 3, SCALE_SMALL, COL_DIM, 1);

    char a[16], b[16], c[16];
    value_text(g->vmin, step / 10.0f, a, sizeof a);
    value_text(g->vmax, step / 10.0f, b, sizeof b);
    value_text(graph_cols_avg(g), step / 10.0f, c, sizeof c);
    snprintf(buf, sizeof buf, "min %s   max %s   avg %s", a, b, c);
    text(buf, 200, 222, SCALE_SMALL, COL_TEXT, 0);

    C3D_FrameEnd(0);
}
