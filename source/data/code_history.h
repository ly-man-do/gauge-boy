#ifndef GAUGEBOY_CODE_HISTORY_H
#define GAUGEBOY_CODE_HISTORY_H

/*
 * Code history: every scan and every clear is appended to a CSV file on
 * the SD card (sdmc:/3ds/gaugeboy/code-history.csv), so the codes are
 * kept after they're cleared (brief section 5d). One row per code; a scan
 * with no codes writes one "none" row. Platform-free text formatting; the
 * app does the file I/O on its worker thread.
 *
 *   date,vin,car,cel,type,code,description
 *   2026-11-15 15:56:02,2HJ...,2008 Honda,off,none,,
 *   2026-11-15 16:10:40,2HJ...,2008 Honda,ON,stored,P0171,System Too Lean Bank 1
 *   2026-11-15 16:12:05,2HJ...,2008 Honda,,cleared,,Codes cleared
 */

#include <stddef.h>

#include "obd/obd_diag.h"

#define CODE_HISTORY_FILE   "code-history.csv"
#define CODE_HISTORY_HEADER "date,vin,car,cel,type,code,description\n"

/* Rows for one scan (when = "YYYY-MM-DD HH:MM:SS"). Writes at most len
 * bytes (NUL included) to out; returns the length written, or 0 if the
 * scan read nothing worth keeping (no codes read at all) or out is too
 * small for a whole row. */
size_t code_history_scan_rows(const obd_diag_t *d, const char *when, char *out, size_t len);

/* One "cleared" row with the result text. */
size_t code_history_clear_row(const obd_diag_t *d, const char *when, const char *result,
                              char *out, size_t len);

#endif
