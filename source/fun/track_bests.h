#ifndef GAUGEBOY_TRACK_BESTS_H
#define GAUGEBOY_TRACK_BESTS_H

/*
 * Personal bests per car (sdmc:/3ds/gauge-boy/bests.csv), one line per car
 * and run, lowest time wins:
 *
 *   car,run,seconds,extra,date
 *   Geo/Tracker/1996,0-60,14.62,,2026-11-18
 *   Geo/Tracker/1996,quarter,19.81,112.4,2026-11-18
 *   Geo/Tracker/1996,revsync,4300.00,,2026-11-18   (a score: highest wins)
 *
 * car is the key from baro_store_key() (VIN, or make/model/year); run is
 * ta_run_key(); extra is the quarter's trap speed (km/h) or the braking
 * distance (m), empty otherwise. Platform-free text in, text out; the app
 * does the file I/O.
 */

#include <stdbool.h>
#include <stddef.h>

#define TRACK_BESTS_FILE "bests.csv"
#define TRACK_BESTS_MAX  64       /* lines kept; the oldest go first */

typedef struct {
    float seconds;
    float extra;          /* 0 if none */
    char date[16];
} track_best_t;

/* The best for car and run; false if none. */
bool track_bests_get(const char *text, const char *car, const char *run, track_best_t *out);

/* Offers a time: if it beats the saved best (or there is none), writes
 * text with it into out (returns its length, 0 if out is too small) and
 * sets *record. Otherwise *record is false and out gets text unchanged. */
size_t track_bests_offer(const char *text, const char *car, const char *run, float seconds,
                         float extra, const char *date, bool *record, char *out, size_t len);

/* The same for a score, where higher is better (Rev Sync). */
size_t track_bests_offer_high(const char *text, const char *car, const char *run, float score,
                              const char *date, bool *record, char *out, size_t len);

#endif
