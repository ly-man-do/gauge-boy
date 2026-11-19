#ifndef GAUGEBOY_TRACK_UI_H
#define GAUGEBOY_TRACK_UI_H

/*
 * TRACK's screens: layout, touch zones and the Info pages' text, mockup v2
 * (docs/mockups/track-v2.html). Platform-free so it's unit-tested; drawing is
 * in track_draw.c (citro2d).
 *
 * Every TRACK screen has (B) Back bottom-left and (Y) Info bottom-right; an
 * Info page has Back only. The closed-course notice has Back and a
 * full-width I Understand.
 */

#include <stdbool.h>

#define TRACK_SCREEN_W 320
#define TRACK_SCREEN_H 240
#define TRACK_LIST_H   198   /* above the tabs */

typedef struct {
    int x, y, w, h;
} track_rect_t;

typedef enum {
    TRK_MENU = 0,       /* the six modes */
    TRK_NOTICE,         /* Time Attack's closed-course notice */
    TRK_TA_LIST,        /* Time Attack: the three runs */
    TRK_TA_RUN,         /* a run: Stop | Start */
    TRK_SHIFT,          /* - shift point + */
    TRK_GMETER,         /* Calibrate, Reset Peaks */
    TRK_REVSYNC,        /* Start / Again */
    TRK_ECO,            /* Reset Trip */
    TRK_ACHIEVE,        /* a list (diag_list_* geometry) */
    TRK_INFO,           /* any Info page */
    TRK_KIND_COUNT
} track_kind_t;

typedef enum {
    TRK_HIT_NONE = 0,
    TRK_HIT_BACK,
    TRK_HIT_INFO,
    TRK_HIT_STOP,        /* the Time Attack run's toggle */
    TRK_HIT_START,
    TRK_HIT_BUTTON,      /* TRK_HIT_BUTTON + i: the screen's buttons in order */
} track_hit_t;

#define TRACK_MENU_ITEMS 6

/* How many buttons a screen has (TRK_HIT_BUTTON + 0 .. n-1). */
int track_button_count(track_kind_t kind);

/* A control's rect on a screen (zero size if the screen hasn't it). */
track_rect_t track_rect(track_kind_t kind, int hit);

/* What a touch is on. */
int track_hit(track_kind_t kind, int px, int py);

/* The menu's labels, in button order (Time Attack ... Achievements). */
const char *track_menu_label(int i);

/* The Info page's text for a screen ("\n\n" between paragraphs). */
const char *track_info_text(track_kind_t kind);

/* Time Attack's run buttons: "0-60 mph" / "0-100 km/h", ... */
const char *track_ta_label(int run, bool metric);

#endif
