#ifndef GAUGEBOY_GARAGE_H
#define GAUGEBOY_GARAGE_H

/*
 * The garage card (brief section 5e): one card per car, with what the VIN
 * (or SETUP) says it is, a nickname, the distance logged in recorded
 * drives and the personal bests (bests.csv, track_bests.h).
 *
 * A car is known by the same key as bests.csv and baro.csv use
 * (baro_store_key(): its VIN, or "make/model/year" from SETUP). The cards
 * are every car those files and garage.csv name, the current one, and any
 * recorded drive's car with a VIN.
 * garage.csv keeps the nicknames:
 *
 *   car,nickname
 *   Geo/Tracker/1996,Little Teal
 *
 * Logged distance: each drive log's Speed column over time (trapezoids;
 * gaps over GARAGE_GAP_S, such as a lost connection, aren't counted). A
 * drive belongs to the card with its VIN, or, without one, whose title
 * matches its "car" line. Results are cached per file (garage-drives.csv:
 * name,size,km,vin,car) so only new drives are read.
 *
 * Platform-free: text in, text out; the app reads and writes the files.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GARAGE_FILE        "garage.csv"
#define GARAGE_DRIVES_FILE "garage-drives.csv"
#define GARAGE_MAX         16
#define GARAGE_NICK_MAX    24
#define GARAGE_GAP_S       5.0

typedef struct {
    char key[32];                 /* VIN or "make/model/year" */
    char title[48];               /* "2008 Honda Ridgeline" */
    char vin[18];                 /* "" if the key isn't a VIN */
    char nickname[GARAGE_NICK_MAX + 1];
    double km;                    /* logged in recorded drives */
    bool have_odo;                /* odometer.csv has a reading (PID A6) */
    double odo_km;
    char odo_date[16];
    int drives;
} garage_car_t;

typedef struct {
    garage_car_t car[GARAGE_MAX];
    int count;
} garage_t;

/* The card title for a key: "year make model" from the VIN (maker from
 * the WMI, model from vPIC) or from "make/model/year". current_year picks
 * the VIN year cycle (0 = the North American rule). */
void garage_title(const char *key, int current_year, char *out, size_t len);

/* Adds a car (no repeats; "" ignored). Returns its index, or -1 if full
 * or empty. */
int garage_add(garage_t *g, const char *key, int current_year);

/* Adds every car named in the first column of a CSV text (bests.csv,
 * baro.csv, garage.csv), skipping the header. */
void garage_add_from_csv(garage_t *g, const char *text, int current_year);

/* Sets the cars' nicknames from garage.csv's text. */
void garage_read_nicknames(garage_t *g, const char *text);

/* garage.csv's text with key's nickname set ("" removes it) into out.
 * Returns the length, 0 if out is too small. */
size_t garage_set_nickname(const char *text, const char *key, const char *nickname, char *out,
                           size_t len);

/* The card a drive belongs to: by VIN, else by its car line (a trailing
 * " (set in SETUP)" ignored) against the titles. -1 if none. */
int garage_find_drive(const garage_t *g, const char *vin, const char *car);

/* ---------- distance in a drive log ---------- */

typedef struct {
    int col;                      /* the Speed column (0 = first after time), -1 if none */
    float to_kmh;                 /* 1 for km/h, 1.609344 for mph */
    bool have;
    double t;
    float kmh;
    double km;
} drive_dist_t;

/* Finds the Speed column in the start of a drive log. False if there is
 * none (the drive adds no distance). */
bool drive_dist_begin(drive_dist_t *dd, const char *head, size_t len);

/* One line of the file (any line: headers and comments are skipped). */
void drive_dist_line(drive_dist_t *dd, const char *line);

/* ---------- the per-file cache ---------- */

/* A cached entry for name with this size; false if none (or the size
 * changed). vin / car get the drive's (may be NULL). */
bool garage_cache_get(const char *text, const char *name, uint32_t size, double *km, char *vin,
                      size_t vin_len, char *car, size_t car_len);

/* Appends a cache line to out (used so far: *used, room len). False if it
 * doesn't fit. Commas in car are dropped. */
bool garage_cache_add(char *out, size_t len, size_t *used, const char *name, uint32_t size,
                      double km, const char *vin, const char *car);

#endif
