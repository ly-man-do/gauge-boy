#ifndef GAUGEBOY_BARO_STORE_H
#define GAUGEBOY_BARO_STORE_H

/*
 * Saved engine-off MAP readings per car (sdmc:/3ds/gauge-boy/baro.csv), so
 * cars without a barometric pressure PID (33) show vacuum from the start of
 * a session. One line per car, newest last:
 *
 *   car,kpa,date
 *   2HJYK...,98.0,2026-11-17
 *   Geo/Tracker/1996,97.5,2026-11-15
 *
 * The car is its VIN, or "make/model/year" from SETUP for cars with no VIN
 * over OBD. Platform-free text handling; the app does the file I/O.
 */

#include <stdbool.h>
#include <stddef.h>

#include "obd/obd_diag.h"

#define BARO_STORE_FILE  "baro.csv"
#define BARO_STORE_MAX   16      /* cars kept; the oldest go first */

/* The key for d's car ("" if nothing identifies it). setup_make /
 * setup_model / setup_year: the SETUP values (may be "" / 0). */
void baro_store_key(const obd_diag_t *d, const char *setup_make, const char *setup_model,
                    int setup_year, char *out, size_t len);

/* The saved reading for key; false if there is none. date may be NULL. */
bool baro_store_get(const char *text, const char *key, float *kpa, char *date, size_t date_len);

/* text with key's reading set (replacing an older one, dropping the
 * oldest past BARO_STORE_MAX) into out. Returns its length (0 if out is
 * too small). */
size_t baro_store_set(const char *text, const char *key, float kpa, const char *date, char *out,
                      size_t len);

#endif
