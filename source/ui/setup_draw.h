#ifndef GAUGEBOY_SETUP_DRAW_H
#define GAUGEBOY_SETUP_DRAW_H

/*
 * Draws SETUP with the UI kit (3DS only), mockup v1 (docs/mockups/setup-v1.html).
 * Layout is in setup_ui.h; the menu's, Vehicle's, Other's and the make picker's
 * tops use diag_draw_facts_top, About's track_draw_text_top.
 */

#include <stdbool.h>

#include "kit.h"
#include "setup_ui.h"

typedef struct {
    const char *labels[8];    /* buttons (menu, guide, Tests) or rows (Wi-Fi, Vehicle, Other) */
    const char *values[8];    /* a row's value, right of its label ("" none) */
    bool chevron[8];          /* a row that opens something */
    int busy;                 /* Tests: the button whose test is running (-1 none) */
    bool metric;              /* Other: Units */
    const char *step;         /* guide: "Step 2 of 5" */
    const char *step_title;   /* guide: "Save the adapter's Wi-Fi" */
    const char *result;       /* guide: the last check's first line, or its progress ("" none) */
    u32 result_color;
    const char *text;         /* About: the sources ("\n\n" between paragraphs) */
    const char *notice;
    int pressed;              /* setup_hit_t held */
} setup_bottom_t;

void setup_draw_bottom(setup_kind_t kind, const setup_bottom_t *b);

/* The make picker: a scrolling list, the chosen make marked. */
void setup_draw_make(const char *const *names, int n, int scroll, int chosen, int pressed_row,
                     bool grip_pressed, bool back_pressed);

/* Wi-Fi's top: the check's lines (the first one green or amber), its progress, or a hint. */
void setup_draw_wifi_top(const char *const *lines, int n, bool ok, const char *progress,
                         const kit_status_t *s);

/* The guide's top: "Step n of 5: title" and the step's lines (as wrapped by wifi_guide). */
void setup_draw_guide_top(const char *heading, const char *const *lines, int n,
                          const kit_status_t *s);

/* Tests' top: the session log's last lines (scrolled: a note to follow it again). */
void setup_draw_log_top(const char *const *lines, int n, bool scrolled, const kit_status_t *s);

#endif
