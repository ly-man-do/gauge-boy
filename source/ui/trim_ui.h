#ifndef GAUGEBOY_TRIM_UI_H
#define GAUGEBOY_TRIM_UI_H

/*
 * TRIM's layout, touch zones, bar segments and text, mockup v2
 * (docs/mockups/trim-v2.html). Platform-free so it's unit-tested; drawing is
 * in trim_draw.c (citro2d).
 *
 *   Top: a panel per bank      B  Long  [||||||||||||#####>]  +27.4 %
 *                              1  Short [||||||||||||##    ]   +7.8 %
 *        742 rpm, 24 % load, at idle
 *   Bottom: poll rate, hints, REC m:ss, [ Stop | Record ], (B) Back, (Y) Info
 */

#include <stdbool.h>
#include <stddef.h>

#include "data/trim_view.h"

#define TRIM_SCREEN_W 320
#define TRIM_SCREEN_H 240
#define TRIM_SEGS     25      /* 2 % each, -25 .. +25 % */

typedef struct {
    int x, y, w, h;
} trim_rect_t;

typedef enum {
    TRIM_HIT_NONE = 0,
    TRIM_HIT_BACK,        /* the left tab */
    TRIM_HIT_INFO,        /* the right tab (not on the Info page) */
    TRIM_HIT_STOP,        /* the toggle's halves */
    TRIM_HIT_RECORD,
} trim_hit_t;

trim_rect_t trim_rect(trim_hit_t what);
/* What a touch is on; the Info page has Back only. */
trim_hit_t trim_hit(int px, int py, bool info_page);

/* Segment i (0 = -25 %) of a bar showing v: 0 unlit, 1 lit (between 0 and v),
 * 2 the end arrow (v past the range on that side). have false: all unlit. */
int trim_seg(int i, float v, bool have);
/* The zone of segment i's middle. */
trim_zone_t trim_seg_zone(int i);

/* "+17.2 %", "-1.6 %", "0.0 %"; "-- %" without a value. */
void trim_value_text(bool have, float v, char *out, size_t len);

/* "742 rpm, 24 % load, at idle"; "2,480 rpm, parked"; "Waiting for RPM". */
void trim_engine_text(const trim_state_t *s, bool have_load, float load, char *out, size_t len);

/* Every bank with a reading is within 10 %: the hints are shown green. */
bool trim_all_normal(const trim_state_t *s);

/* The Info page: paragraphs before the colour lines, then after them. */
const char *trim_info_before(void);
const char *trim_info_after(void);

#endif
