#ifndef GAUGEBOY_LOG_UI_H
#define GAUGEBOY_LOG_UI_H

/*
 * LOG's screens: layout, touch zones and text, mockup v1
 * (docs/mockups/log-v1.html). Platform-free so it's unit-tested; drawing is in
 * log_draw.c (citro2d). The drive list uses diag_list_* geometry; the Delete
 * question uses diag_dialog_*.
 *
 *   List: drives newest first (scrolls), (B) Back, (Y) Info
 *   A drive: its details on top; [Graph] [Delete], (B) Back
 *   GRAPH: [<<] [Zoom -] [Zoom +] [>>], (< value >) half circles, [Whole Drive], (B) Back
 */

#include <stdbool.h>
#include <stddef.h>

#include "data/drive_list.h"

typedef struct {
    int x, y, w, h;
} log_rect_t;

typedef enum {
    LOG_LIST = 0,
    LOG_DRIVE,
    LOG_GRAPH,
    LOG_INFO,
    LOG_KIND_COUNT
} log_kind_t;

typedef enum {
    LOG_HIT_NONE = 0,
    LOG_HIT_BACK,
    LOG_HIT_INFO,
    LOG_HIT_PREV,        /* GRAPH: the previous / next value (half circles) */
    LOG_HIT_NEXT,
    LOG_HIT_BUTTON,      /* LOG_HIT_BUTTON + i */
} log_hit_t;

/* A drive's page: Graph, Delete. GRAPH: earlier, zoom out, zoom in, later, Whole Drive. */
int log_button_count(log_kind_t kind);
log_rect_t log_rect(log_kind_t kind, int hit);
int log_hit(log_kind_t kind, int px, int py);

/* A list row's parts: "11/19 08:42", the car ("Car not known"), "24:10". */
void log_row_parts(const drive_info_t *d, char *when, size_t wlen, char *car, size_t clen,
                   char *dur, size_t dlen);

/* A duration, "24:10" / "1:02:03" / "--". */
void log_duration(float secs, char *out, size_t len);

/* A size, "412 KB" / "1.8 MB". */
void log_size(unsigned long bytes, char *out, size_t len);

/* The Info page's text ("\n\n" between paragraphs). */
const char *log_info_text(void);

#endif
