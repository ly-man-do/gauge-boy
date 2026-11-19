#ifndef GAUGEBOY_LIVE_UI_H
#define GAUGEBOY_LIVE_UI_H

/*
 * LIVE screens' layout, touch zones and the Dashboard's gauge math, mockup v5
 * (docs/mockups/live-v5.html). Platform-free so it's unit-tested; drawing is
 * in live_draw.c (citro2d).
 *
 *   View list (bottom):            PID view (bottom):
 *     [ Engine      ]  |grip|        8 PIDs, 5.5 a second
 *     [ Warm-up     ]  |    |     (<            * REC 1:24          >)
 *     [ Drive ...   ]  |    |     (<   [ Stop  |  Record ]           >)
 *   [B Back]                       [B Back]
 */

#include <stdbool.h>

#include "obd/obd_live.h"
#include "obd/obd_pid.h"

#define LIVE_SCREEN_W 320
#define LIVE_SCREEN_H 240
#define LIVE_LIST_H   198     /* the list's visible height, above the Back tab */

typedef struct {
    int x, y, w, h;
} live_rect_t;

/* ---------- the view list ---------- */

/* Row i's button with the list scrolled by scroll px (y can be off screen). */
live_rect_t live_list_row(int i, int scroll);

/* The furthest the list scrolls with n rows (0 if they all fit). */
int live_list_max_scroll(int n);

int live_list_clamp(int scroll, int n);

/* The row under a touch, or -1 (the Back tab and gaps aren't rows). */
int live_list_hit(int px, int py, int scroll, int n);

/* The scroll bar: its track and the grip's top for this scroll. */
live_rect_t live_list_track(void);
int live_list_grip_y(int scroll, int n);
#define LIVE_GRIP_W 20
#define LIVE_GRIP_H 34

/* The scroll bar's touch zone: the strip at the right edge beside the rows. */
bool live_list_bar_hit(int px, int py);
/* Where to grab the grip for a touch at py: its offset from the grip's top (the touch
 * point if it's on the grip, else the grip's middle, so the grip jumps under the finger). */
int live_list_grip_grab(int py, int scroll, int n);
/* The scroll that puts the grip's top at grip_top (clamped). */
int live_list_scroll_for_grip(int grip_top, int n);

/* ---------- a PID view ---------- */

typedef enum {
    LIVE_HIT_NONE = 0,
    LIVE_HIT_BACK,       /* the Back tab (both screens) */
    LIVE_HIT_PREV,       /* the left half circle: the previous view */
    LIVE_HIT_NEXT,       /* the right one: the next view */
    LIVE_HIT_STOP,       /* the toggle's left half */
    LIVE_HIT_RECORD,     /* its right half */
    LIVE_HIT_EDIT,       /* Custom only: Edit PIDs */
} live_hit_t;

/* The half circles show this much of themselves at the screen's edges, as on System
 * Settings' pages (the rest of the circle is off screen). Their touch zones are a
 * little wider: 40 px. */
#define LIVE_SIDE_SHOWN 34
#define LIVE_SIDE_W     53    /* the sprite: the circle's half, its centre on its outer edge */

live_rect_t live_view_rect(live_hit_t what);

/* What a touch on a PID view's bottom screen hits (Edit only on Custom). */
live_hit_t live_view_hit(int px, int py, bool custom);

/* The Back tab, on either screen. */
bool live_back_hit(int px, int py);

/* ---------- the Dashboard ---------- */

#define LIVE_TACH_SEGMENTS 40   /* 0-8000 rpm, 5 per 1000 */
#define LIVE_TACH_RED      35   /* from 7000 rpm */
#define LIVE_MINI_SEGMENTS 12
#define LIVE_COOLANT_RED   9    /* from about 108 C */

/* Lit segments for a reading (clamped to the gauge). */
int live_tach_lit(float rpm);
int live_coolant_lit(float celsius);      /* 40-130 C, C to H */
int live_volts_lit(float volts);          /* 10-16 V */

/* The Dashboard's readings in display units. The odometer is the car's own
 * (PID A6) when it has one, else the garage's (have_odo, odo_km). lv may be
 * NULL (not polling yet). */
typedef struct {
    bool have_rpm, have_speed, have_coolant, have_volts;
    float rpm, coolant_c, volts;
    char speed[4];           /* "  0", "123", "---" (right-aligned, LCD) */
    const char *speed_unit;  /* "mph", "km/h" */
    char coolant[12];        /* "190 F", "--- F" */
    char battery[12];        /* "14.2 V", "--.- V" */
    char odo[16];            /* "123,456", "---,---" (LCD) */
    const char *odo_unit;    /* "mi", "km" */
} live_dash_t;

void live_dash_values(const obd_live_t *lv, obd_units_t units, bool have_odo, double odo_km,
                      live_dash_t *d);

/* The REC dot's brightness (0.2-1) at ms into recording: one slow pulse every 2.4 s. */
float live_rec_pulse(unsigned long long ms);

#endif
