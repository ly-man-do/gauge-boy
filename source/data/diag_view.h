#ifndef GAUGEBOY_DIAG_VIEW_H
#define GAUGEBOY_DIAG_VIEW_H

/*
 * Text for the DIAG screen's top-screen panel: a short summary (car,
 * protocol, check engine light) and the chosen tab (stored / pending /
 * permanent codes with descriptions, or the readiness monitors), wrapped
 * to the screen width. Platform-free.
 */

#include "obd/obd_diag.h"

#define DIAG_VIEW_COLS 49   /* top-screen text width */
#define DIAG_VIEW_MAX  160  /* lines kept (the panel scrolls) */

typedef enum {
    DIAG_TAB_STORED = 0,
    DIAG_TAB_PENDING,
    DIAG_TAB_PERMANENT,
    DIAG_TAB_READINESS,
    DIAG_TAB_COUNT
} diag_tab_t;

/* Fills lines with the panel for d and tab; each line has at most cols
 * (<= DIAG_VIEW_COLS) characters. Returns the number of lines. */
int diag_view_lines(const obd_diag_t *d, diag_tab_t tab, int cols,
                    char lines[][DIAG_VIEW_COLS + 1], int max);

const char *diag_tab_name(diag_tab_t tab);

/* ---------- pieces for the paged DIAG screen ---------- */

#define DIAG_ROW_COLS   37   /* one bottom-screen list row */
#define DIAG_ITEMS_MAX  64

typedef struct {
    char row[DIAG_ROW_COLS + 1];  /* "P0171 System Too Lean Bank 1" (cut to fit) */
    char detail[200];             /* full text for the top screen */
} diag_item_t;

/* Rows for a tab: codes with their descriptions, or the supported
 * readiness monitors. Returns the count (0 = empty tab). */
int diag_items(const obd_diag_t *d, diag_tab_t tab, diag_item_t *items, int max);

/* One line telling why a tab is empty ("No stored codes.", "Not read",
 * "Not supported by this car (normal before 2010)."). */
const char *diag_empty_text(const obd_diag_t *d, diag_tab_t tab);

/* True if a lean or rich code (P0171, P0172, P0174, P0175) is stored or
 * pending: DIAG then offers a TRIM button (brief section 5d). */
bool diag_has_trim_codes(const obd_diag_t *d);

/* Fact rows for the top screen: label column then value, e.g.
 * "Car       2008 Honda", "CEL       ON (2 stored codes)". */
int diag_facts(const obd_diag_t *d, int cols, char lines[][DIAG_VIEW_COLS + 1], int max);

/* Wraps text into lines of at most cols characters (word boundaries,
 * continuation lines indented by indent). Returns lines written. */
int diag_wrap(const char *text, int cols, int indent, char lines[][DIAG_VIEW_COLS + 1], int max);

/* The car in a few words, "2023 Subaru Ascent" or "1996 Geo Tracker (set
 * in SETUP)"; "" when nothing is known. */
void diag_car_text(const obd_diag_t *d, char *out, size_t len);

#endif
