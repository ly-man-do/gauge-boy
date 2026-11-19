#ifndef GAUGEBOY_LOG_DRAW_H
#define GAUGEBOY_LOG_DRAW_H

/*
 * Draws LOG's bottom screens with the UI kit (3DS only), mockup v1
 * (docs/mockups/log-v1.html). The tops use diag_draw_facts_top and
 * track_draw_text_top; GRAPH's top is graph_draw.c. Layout is in log_ui.h.
 */

#include <stdbool.h>

#include "kit.h"
#include "log_ui.h"

typedef struct {
    const char *when;    /* "11/19 08:42" */
    const char *car;
    const char *dur;     /* "24:10" */
} log_row_t;

/* The drive list (scrolls), (B) Back and (Y) Info; empty: a line in its place. */
void log_draw_list(const log_row_t *rows, int n, int scroll, int pressed_row, bool grip_pressed,
                   int pressed, const char *empty, const char *notice);

/* A drive's page: Graph, Delete and (B) Back. */
void log_draw_drive(int pressed, const char *notice);

/* GRAPH's controls: the value's name and the window shown ("0:00 to 24:10 of 24:10"). */
void log_draw_graph(const char *value, const char *showing, int pressed);

#endif
