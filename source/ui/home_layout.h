#ifndef GAUGEBOY_HOME_LAYOUT_H
#define GAUGEBOY_HOME_LAYOUT_H

/*
 * HOME bottom screen (320x240): where everything goes and what it says.
 * Platform-free so the layout and touch zones are unit-tested; drawing is
 * in home_draw.c (citro2d).
 *
 *   [ Garage    ] [ Adapter   ]       status buttons (tap: GARAGE / Wi-Fi guide)
 *   [ LIVE ] [ DIAG ] [ TRIM ]         menu buttons: icon and label
 *   [ TRACK ] [ LOG ] [ SETUP ]
 *   ======= START Close =======        bar: tap to close, like START
 *
 * Mockup v7 (docs/mockups/home-v7.html), owner-approved 2026-11-18.
 */

#include <stdbool.h>

#define HOME_SCREEN_W 320
#define HOME_SCREEN_H 240
#define HOME_BAR_Y    202   /* System Settings' 38 px bottom bar (brief 1.22.0) */

typedef enum {
    HOME_GARAGE = 0,
    HOME_ADAPTER,
    HOME_LIVE,
    HOME_DIAG,
    HOME_TRIM,
    HOME_TRACK,
    HOME_LOG,
    HOME_SETUP,
    HOME_CLOSE,          /* the START Close bar */
    HOME_ITEM_COUNT
} home_item_t;

typedef struct {
    int x, y, w, h;
} home_rect_t;

/* What HOME shows; filled by the UI each time it redraws. */
typedef struct {
    bool cel_known;      /* a scan read the check engine light */
    bool cel_on;
    bool connected;      /* adapter connected */
    int selected;        /* last tapped item, -1 = none (not drawn) */
    int pressed;         /* item flashing after a tap, -1 = none */
    const char *notice;  /* shown in the bar instead of "START Close", or NULL */
    const char *car;     /* NULL when no car is known yet (the Garage icon dims) */
    int codes;           /* stored codes for the DIAG badge, -1 if not read */
} home_view_t;

/* The button as drawn. */
home_rect_t home_item_rect(home_item_t item);

/* The touch zone: the button, except the status buttons also take the
 * strip above them, up to the top edge. */
home_rect_t home_item_touch(home_item_t item);

/* The item at a touch point, or -1. */
int home_hit(int px, int py);

/* Menu buttons: "LIVE" ...; status buttons and the bar: "". No second
 * lines any more (NULL). */
const char *home_item_label(home_item_t item);
const char *home_item_sub(home_item_t item);

/* "CEL off", "CEL ON", "CEL not read"; "Connected", "Not connected". */
const char *home_cel_text(const home_view_t *v);

/* The DIAG button's label: "CEL ON" (the button turns amber, the panel's
 * CEL lamp) when the light is on, else "DIAG". */
const char *home_diag_label(const home_view_t *v);

#define HOME_GARAGE_TEXT "Garage"
const char *home_adapter_text(const home_view_t *v);

#endif
