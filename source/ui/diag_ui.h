#ifndef GAUGEBOY_DIAG_UI_H
#define GAUGEBOY_DIAG_UI_H

/*
 * DIAG screens' layout, touch zones and top-screen text, mockup v2
 * (docs/mockups/diag-v2.html). Platform-free so it's unit-tested; drawing is
 * in diag_draw.c (citro2d).
 *
 *   DIAG (bottom):                 A page (Stored, Pending, Permanent):
 *       [   Stored    ]              [P0171 System Too Lean ...]  |grip|
 *       [   Pending   ]              [P0174 System Too Lean ...]  |    |
 *       [  Permanent  ]              ...
 *       [  Readiness  ]
 *   [B Back]        [X Scan]       [B Back]          [X Clear]
 *
 * The top screen shows what each button holds (DIAG), a page's summary, or
 * the code tapped (its text and freeze frame). Readiness has Back only.
 */

#include <stdbool.h>
#include <stddef.h>

#include "data/diag_view.h"
#include "obd/obd_diag.h"
#include "obd/obd_freeze.h"
#include "obd/obd_pid.h"

#define DIAG_SCREEN_W 320
#define DIAG_SCREEN_H 240
#define DIAG_LIST_H   198     /* a page's list: its visible height, above the tabs */

typedef struct {
    int x, y, w, h;
} diag_rect_t;

/* ---------- DIAG's first page ---------- */

typedef enum {
    DIAG_HIT_NONE = 0,
    DIAG_HIT_BACK,        /* the left tab, (B) Back */
    DIAG_HIT_RIGHT,       /* the right tab: (X) Scan on DIAG, (X) Clear on a code page */
    DIAG_HIT_STORED,      /* DIAG's buttons: DIAG_HIT_STORED + diag_tab_t */
    DIAG_HIT_PENDING,
    DIAG_HIT_PERMANENT,
    DIAG_HIT_READINESS,
} diag_hit_t;

#define DIAG_HOME_BUTTONS 4

/* Button i (a diag_tab_t) on DIAG's first page: centred above the tabs. */
diag_rect_t diag_home_button(int i);
const char *diag_home_label(int i);    /* "Stored", "Pending", "Permanent", "Readiness" */

/* The tabs along the bottom edge (System Settings' left and right tabs). */
diag_rect_t diag_tab(bool right);

/* What a touch on DIAG's first page is on. */
diag_hit_t diag_home_hit(int px, int py);
/* On a page: Back, or the right tab when the page has one (right_tab). */
diag_hit_t diag_page_tab_hit(int px, int py, bool right_tab);

/* ---------- a page's list ---------- */

diag_rect_t diag_list_row(int i, int scroll);
int diag_list_max_scroll(int n);
int diag_list_clamp(int scroll, int n);
/* The row under a touch, or -1. */
int diag_list_hit(int px, int py, int scroll, int n);
/* The scroll bar (as LIVE's view list): track, grip, touch strip. */
diag_rect_t diag_list_track(void);
#define DIAG_GRIP_W 20
#define DIAG_GRIP_H 34
int diag_list_grip_y(int scroll, int n);
bool diag_list_bar_hit(int px, int py);
int diag_list_grip_grab(int py, int scroll, int n);
int diag_list_scroll_for_grip(int grip_top, int n);

/* ---------- the clear-codes question ---------- */

diag_rect_t diag_dialog_button(bool clear);
/* 0 none, 1 Cancel, 2 Clear Codes. */
int diag_dialog_hit(int px, int py);

/* ---------- top-screen text ---------- */

typedef enum {
    DIAG_INK_WHITE = 0,
    DIAG_INK_RED,
    DIAG_INK_AMBER,
    DIAG_INK_GREEN,
    DIAG_INK_GREY,
} diag_ink_t;

/* One row of a facts panel: a grey tag, a value and a smaller detail. */
typedef struct {
    char tag[16];
    char value[40];
    char detail[64];
    diag_ink_t ink;          /* the value's colour */
    bool cel_icon;           /* the CEL lamp before the value */
} diag_fact_t;

#define DIAG_FACTS_MAX 8

/* DIAG's first page: the car, the CEL and what each button holds. nickname is the
 * Garage's name for the car ("" none). note gets a line for under the rows ("" none). */
int diag_home_facts(const obd_diag_t *d, const char *nickname, obd_units_t units,
                    diag_fact_t *out, int max, char *note, size_t note_len);

/* A page's title: "STORED CODES", "PENDING CODES", "PERMANENT CODES", "READINESS". */
const char *diag_page_title(diag_tab_t tab);

/* A page's summary for the top screen before a code is tapped (or Readiness's). */
int diag_page_facts(const obd_diag_t *d, diag_tab_t tab, diag_fact_t *out, int max, char *note,
                    size_t note_len);

/* A page's list rows: a code and its description, or a monitor and its state. */
typedef struct {
    char name[24];           /* "P0171" (bold) or "Catalyst" */
    char right[96];          /* the description, or "Complete" / "Not ready" */
    bool code;               /* a code row: name bold, the text after it */
    diag_ink_t ink;          /* right's colour */
} diag_row_t;

int diag_page_rows(const obd_diag_t *d, diag_tab_t tab, diag_row_t *out, int max);

/* The code tapped: its text, where it came from and its freeze frame. */
typedef struct {
    char code[8];
    char kind[12];           /* "Stored", "Pending", "Permanent" */
    char module[24];         /* "Engine", or "" when not known */
    char text[160];
    bool have_freeze;        /* the freeze frame is this code's */
    int freeze_count;
    char freeze_label[OBD_FREEZE_MAX][24];
    char freeze_value[OBD_FREEZE_MAX][24];
} diag_code_t;

bool diag_code_detail(const obd_diag_t *d, diag_tab_t tab, int i, obd_units_t units,
                      diag_code_t *out);

/* A monitor's short name for lists of names: "Evap", "O2 sensor". */
const char *diag_monitor_short(const char *name);

/* Whether the car would likely pass an emissions inspection's OBD check (US):
 * CEL off and at most 1 monitor not ready (2 for 1996-2000 cars). detail gets why. */
bool diag_inspection(const obd_diag_t *d, char *detail, size_t len);

#endif
