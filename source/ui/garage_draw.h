#ifndef GAUGEBOY_GARAGE_DRAW_H
#define GAUGEBOY_GARAGE_DRAW_H

/*
 * Draws GARAGE, its camera, TRIP and the MILEAGE log with the UI kit (3DS
 * only), mockup v2 (docs/mockups/garage-v2.html). Layout is in garage_ui.h;
 * the mileage tops use diag_draw_facts_top, Info track_draw_info_text, the
 * Delete question diag_draw_question.
 */

#include <stdbool.h>

#include "garage_ui.h"
#include "kit.h"

typedef struct {
    bool have;                /* a car to show */
    const char *name;         /* the nickname, else the title */
    const char *title;        /* "2008 Honda Ridgeline" ("" when the name is the title) */
    const char *vin;          /* "VIN 1HG..." or "(set in SETUP)" */
    const char *odo;          /* "123,456" ("" none) */
    const char *unit;         /* "mi" / "km" */
    const char *logged;       /* "1,204 mi" */
    const char *drives;       /* "in 23 drives" */
    const char *best[2];      /* "0-60 4.82 s" ... (NULL none) */
    const char *msg;          /* "Counting drive 3 of 9..." / "No cars yet..." ("" none) */
    const C2D_Image *photo;   /* the car's photo, NULL for the frame's icon */
} garage_card_t;

void garage_draw_card_top(const garage_card_t *c, const kit_status_t *s);

/* The camera live (brackets and a hint) or the shot, across the whole top screen. */
void garage_draw_photo_top(const C2D_Image *img, bool live, const char *caption);

typedef struct {
    const char *car;          /* "Big Red, 2008 Honda Ridgeline" */
    bool live;
    const char *trip;         /* "248.6" ("" no trip yet) */
    const char *unit;
    const char *since;        /* "since 11/12, at 123,208 mi" */
    const char *odo;          /* "Odometer now 123,456 mi" */
    const char *msg;          /* when there's no odometer ("" none) */
} garage_trip_t;

void garage_draw_trip_top(const garage_trip_t *t, const kit_status_t *s);

typedef struct {
    const char *labels[4];
    const char *name, *sub;   /* the card: between the half circles */
    const char *caption;      /* a grey line under the main button ("" none) */
    const char *notice;
    int pressed;              /* garage_hit_t */
} garage_bottom_t;

void garage_draw_bottom(garage_kind_t kind, const garage_bottom_t *b);

typedef struct {
    const char *date;         /* "11/18" */
    const char *mid;          /* "123,456 mi, 312 mi" */
    const char *econ;         /* "18.4 mpg" */
} garage_entry_row_t;

void garage_draw_miles(const garage_entry_row_t *rows, int n, int scroll, int pressed_row,
                       bool grip_pressed, int pressed, const char *empty, const char *notice);

#endif
