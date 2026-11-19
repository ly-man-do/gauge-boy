#ifndef GAUGEBOY_TRIM_DRAW_H
#define GAUGEBOY_TRIM_DRAW_H

/*
 * Draws TRIM with the UI kit (3DS only), mockup v2 (docs/mockups/trim-v2.html).
 * Layout, bar segments and text are in trim_ui.h.
 */

#include <stdbool.h>

#include "kit.h"
#include "trim_ui.h"

/* Top: a panel per bank (Long and Short bars, their values) and the engine line. */
void trim_draw_top(const trim_state_t *t, const char *engine, const kit_status_t *s);

typedef struct {
    int pids;
    float rate;
    bool connected;
    const char *const *hints;   /* trim_hints() */
    int nhints;
    bool hints_ok;              /* all within 10 %: green */
    bool recording;
    unsigned long long rec_ms;
    trim_hit_t pressed;
} trim_controls_t;

/* Bottom: poll rate, hints, REC, Stop | Record, (B) Back and (Y) Info. */
void trim_draw_bottom(const trim_controls_t *c);

/* Bottom: the Info page (what trims mean) and (B) Back. */
void trim_draw_info(bool back_pressed);

#endif
