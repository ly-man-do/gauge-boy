#ifndef GAUGEBOY_TRACK_DRAW_H
#define GAUGEBOY_TRACK_DRAW_H

/*
 * Draws TRACK and its modes with the UI kit (3DS only), mockup v2
 * (docs/mockups/track-v2.html). Layout and touch zones are in track_ui.h;
 * the menu's and Time Attack list's top screens use diag_draw_facts_top.
 */

#include <stdbool.h>

#include "kit.h"
#include "track_ui.h"

/* ---------- bottom ---------- */

typedef struct {
    const char *labels[TRACK_MENU_ITEMS];   /* the screen's buttons, in order */
    const char *status;       /* a line above the controls ("" none) */
    u32 status_color;
    const char *lcd;          /* Shift Light: the shift point, between - and + */
    bool armed;               /* Time Attack run: Start chosen */
    const char *notice;       /* a pill above the tabs (NULL none) */
    int pressed;              /* track_hit_t held */
} track_bottom_t;

void track_draw_bottom(track_kind_t kind, const track_bottom_t *b);

/* An Info page: the screen's explanation and (B) Back. */
void track_draw_info(track_kind_t kind, bool back_pressed);

typedef struct {
    const char *name;
    const char *right;        /* "11/14" unlocked, or how to get it */
    bool unlocked;
} track_ach_row_t;

/* Achievements' list (scrolls like a DIAG page). */
void track_draw_ach_bottom(const track_ach_row_t *rows, int n, int scroll, bool grip_pressed,
                           int pressed, const char *notice);

/* ---------- top ---------- */

/* A title and a panel of wrapped text (the closed-course notice). */
void track_draw_text_top(int icon, const char *title, const char *text, const kit_status_t *s);

typedef struct {
    const char *title;        /* "0-60 MPH" */
    const char *big;          /* the time ("4.82"), a countdown ("3") or "GO!" ("" none) */
    bool big_lcd;             /* LCD digits (else big text) */
    u32 big_color;
    const char *flag;         /* "NEW RECORD!" ("" none) */
    const char *say, *say2;   /* what to do now */
    const char *extra;        /* trap speed / stopping distance ("" none) */
    const char *speed;        /* "60" ("" unknown) */
    const char *speed_unit;
    const char *best, *last;  /* "4.82 s" / "--" */
    const char *msg;          /* no speed, not connected ("" none) */
} track_ta_top_t;

void track_draw_ta_top(const track_ta_top_t *t, const kit_status_t *s);

typedef struct {
    int lit;                  /* LEDs on (0-10) */
    int color[10];            /* each LED's shift_color_t: 0 green, 1 amber, 2 red */
    bool flashing;            /* at the shift point: all red, blinking */
    bool flash_on;
    const char *rpm;          /* "5,420" ("" none) */
    const char *msg;          /* when there's no RPM */
    const char *foot;         /* "The LEDs light from 3,720 rpm and flash at 6,200." */
} track_shift_top_t;

void track_draw_shift_top(const track_shift_top_t *t, const kit_status_t *s);

typedef struct {
    bool ball;                /* calibrated, with a reading */
    float lat, lon;           /* g: right +, accel + */
    const char *rows[10];     /* the panel's lines: "Side|0.32 g|right" (| between parts) */
    int nrows;
} track_gm_top_t;

void track_draw_gmeter_top(const track_gm_top_t *t, const kit_status_t *s);

typedef struct {
    float rpm;                /* the tach bar (0 none) */
    int target;               /* the target marked (0 none) */
    float peak;               /* the blip's peak (0 none) */
    const char *big;          /* the target, GO!, the blip or the score ("" none) */
    u32 big_color;
    const char *say, *say2;
    const char *round, *score, *target_text;   /* the stats row ("" hides it) */
    const char *best;         /* "High score (this car) 4,310, rank A" */
} track_rs_top_t;

void track_draw_revsync_top(const track_rs_top_t *t, const kit_status_t *s);

typedef struct {
    const char *score;        /* "87" */
    const char *rank;         /* "A" ("" not ranked yet) */
    u32 rank_color;
    const char *now, *trip, *best;            /* "24.1 mpg" ... */
    const char *stats[6];     /* "Distance|12.4 mi", ... */
    const char *tip;          /* amber ("" none) */
    const char *msg;          /* fuel estimate notes ("" none) */
} track_eco_top_t;

void track_draw_eco_top(const track_eco_top_t *t, const kit_status_t *s);

/* The 12 stickers and "n of 12 unlocked". */
void track_draw_ach_top(const char *const *names, const bool *unlocked, int n, int count,
                        const kit_status_t *s);

#endif
