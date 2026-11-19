#ifndef GAUGEBOY_DIAG_DRAW_H
#define GAUGEBOY_DIAG_DRAW_H

/*
 * Draws DIAG with the UI kit (3DS only), mockup v2 (docs/mockups/diag-v2.html).
 * Layout, touch zones and the text are in diag_ui.h.
 */

#include <stdbool.h>

#include "diag_ui.h"
#include "kit.h"

/* Top: a title (icon orange, text white) and a panel of facts with a note under
 * the rows. status: a line under the panel (scan progress, a result), "" none. */
void diag_draw_facts_top(int icon, const char *title, const diag_fact_t *f, int n,
                         const char *note, const char *status, const kit_status_t *s);

/* Top: the code tapped, its text and its freeze frame. */
void diag_draw_code_top(const diag_code_t *c, const kit_status_t *s);

/* Top: what clearing does (while the question shows). */
void diag_draw_clear_top(const kit_status_t *s);

/* Bottom: DIAG's four buttons, (B) Back and (X) Scan; pressed = the one held. */
void diag_draw_home_bottom(diag_hit_t pressed, const char *notice);

typedef struct {
    const diag_row_t *rows;
    int n;
    int scroll;
    int selected;          /* the code tapped (-1 none) */
    int pressed_row;       /* the row held (-1 none) */
    bool grip_pressed;
    diag_hit_t pressed_tab;
    bool right_tab;        /* (X) Clear (code pages) */
    const char *empty;     /* shown when n is 0 */
    const char *notice;    /* "" or NULL none */
} diag_page_view_t;

/* Bottom: a page's list and its tabs. */
void diag_draw_page_bottom(const diag_page_view_t *v);

/* Bottom: the clear-codes question; pressed 0 none, 1 Cancel, 2 Clear Codes. */
void diag_draw_dialog_bottom(int pressed);

#endif
