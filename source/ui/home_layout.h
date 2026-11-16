#ifndef GAUGEBOY_HOME_LAYOUT_H
#define GAUGEBOY_HOME_LAYOUT_H

/*
 * HOME bottom screen (320x240): where everything goes and what it says.
 * Platform-free so the layout and touch zones are unit-tested; drawing is
 * in home_draw.c (citro2d).
 *
 *   [ CEL off ] [ Not connected ]      status cards (tap: DIAG / Wi-Fi guide)
 *   [ LIVE ] [ DIAG ] [ TRIM ]         menu cards: icon, label, second line
 *   [ FUN  ] [ LOG  ] [ SETUP ]
 *   ======= START Close =======        bar (not a button)
 */

#include <stdbool.h>

#define HOME_SCREEN_W 320
#define HOME_SCREEN_H 240
#define HOME_BAR_Y    208

typedef enum {
    HOME_CEL = 0,
    HOME_ADAPTER,
    HOME_LIVE,
    HOME_DIAG,
    HOME_TRIM,
    HOME_FUN,
    HOME_LOG,
    HOME_SETUP,
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
    int selected;        /* last tapped item (lime outline), -1 = none */
    int pressed;         /* item flashing after a tap, -1 = none */
    const char *notice;  /* shown in the bar instead of "START Close", or NULL */
} home_view_t;

/* The card as drawn. */
home_rect_t home_item_rect(home_item_t item);

/* The touch zone: the card, except the status cards also take the strip
 * above them so they are 40 px tall. */
home_rect_t home_item_touch(home_item_t item);

/* The item at a touch point, or -1. */
int home_hit(int px, int py);

/* Menu cards: "LIVE" / "Dash gauges" ...; status cards: "" / NULL. */
const char *home_item_label(home_item_t item);
const char *home_item_sub(home_item_t item);

/* Status card text: "CEL off", "CEL ON", "CEL not read"; "Connected",
 * "Not connected". */
const char *home_cel_text(const home_view_t *v);
const char *home_adapter_text(const home_view_t *v);

#endif
