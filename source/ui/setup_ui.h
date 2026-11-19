#ifndef GAUGEBOY_SETUP_UI_H
#define GAUGEBOY_SETUP_UI_H

/*
 * SETUP's screens: layout and touch zones, mockup v1 (docs/mockups/setup-v1.html).
 * Platform-free so it's unit-tested; drawing is in setup_draw.c (citro2d). The
 * make picker uses diag_list_* geometry.
 *
 *   Menu: [Wi-Fi] [Vehicle] [Other] [Tests]        Wi-Fi: rows (label, value, >)
 *   Vehicle: Make >, Model >, Year [-][+], Engine [-][+], VIN >
 *   Other: [ US | Metric ], About >                Guide: (< step >), [Check & Connect]
 *   Tests: eight test buttons; the session log on top
 * Every page: (B) Back bottom-left.
 */

#include <stdbool.h>

typedef struct {
    int x, y, w, h;
} setup_rect_t;

typedef enum {
    SET_MENU = 0,
    SET_WIFI,
    SET_GUIDE,
    SET_VEHICLE,
    SET_MAKE,
    SET_OTHER,
    SET_ABOUT,
    SET_TESTS,
    SET_KIND_COUNT
} setup_kind_t;

typedef enum {
    SET_HIT_NONE = 0,
    SET_HIT_BACK,
    SET_HIT_PREV,        /* the guide's half circles */
    SET_HIT_NEXT,
    SET_HIT_US,          /* Other's Units toggle */
    SET_HIT_METRIC,
    SET_HIT_BUTTON,      /* SET_HIT_BUTTON + i */
} setup_hit_t;

/* Vehicle's buttons, in order. */
enum { SET_VEH_MAKE = 0, SET_VEH_MODEL, SET_VEH_YEAR_DOWN, SET_VEH_YEAR_UP, SET_VEH_ENGINE_DOWN,
       SET_VEH_ENGINE_UP, SET_VEH_VIN, SET_VEH_COUNT };

int setup_button_count(setup_kind_t kind);
/* A control's touch zone (at least 40 x 40). */
setup_rect_t setup_rect(setup_kind_t kind, int hit);
int setup_hit(setup_kind_t kind, int px, int py);

/* A row's drawn box (Wi-Fi rows, Vehicle rows, Other's About): row i of the page. */
setup_rect_t setup_row(setup_kind_t kind, int i);
int setup_rows(setup_kind_t kind);

const char *setup_menu_label(int i);   /* "Wi-Fi", "Vehicle", "Other", "Tests" */

#endif
