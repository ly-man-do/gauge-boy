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

#endif
