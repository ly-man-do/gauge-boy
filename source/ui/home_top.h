#ifndef GAUGEBOY_HOME_TOP_H
#define GAUGEBOY_HOME_TOP_H

/*
 * HOME top screen (400x240): what it says. Platform-free so the text is
 * unit-tested; drawing is in home_draw.c (citro2d).
 *
 *   [signal] [Adapter connected]          11/18 (Wed) 21:12 [battery]
 *               GAUGE BOY: OBD-II DELUXE
 *   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 *   | My Ridgeline                         [CEL off] [0 codes]       |
 *   | 2008 Honda Ridgeline                 ISO 9141-2 - ELM327 v2.3  |
 *   | ODOMETER            |  | LAST DRIVE                            |
 *   |  123,456 mi         |  |  12.4 mi today                        |
 *   |                     |  |  17.8 mpg average                     |
 *            Not for use by the driver while driving
 *
 * Mockup v4 (docs/mockups/home-v4.html), owner-approved 2026-11-18.
 */

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool connected;
    bool have_car;
    const char *nickname;      /* "" if none */
    const char *title;         /* "2008 Honda Ridgeline" */
    bool cel_known, cel_on;
    int codes;                 /* stored codes, -1 if not read */
    int protocol;              /* ELM327 protocol 1-12, 0 if unknown */
    const char *adapter_id;    /* "ELM327 V2.3", "" if unknown */
    const char *adapter_host;  /* "192.168.0.10", shown when not connected */
    bool metric;
    bool have_odo;
    double odo_km;
    bool have_last;
    double last_km;
    const char *last_name;     /* "drive-20261118-141344.csv" */
    const char *today;         /* "20261118" */
    bool have_economy;
    double economy;            /* mpg (US) or L/100 km (metric) */
} home_top_t;

/* The car card's two lines: nickname / title, title / "", or the no-car
 * text. */
void home_top_car(const home_top_t *t, char *line1, size_t len1, char *line2, size_t len2);

/* "CEL off", "CEL ON", "CEL --". */
const char *home_top_cel(const home_top_t *t);

/* "0 codes", "1 code", "3 codes", "-- codes". */
void home_top_codes(const home_top_t *t, char *out, size_t len);

/* "ISO 9141-2 - ELM327 v2.3" when connected (either part may be missing),
 * else "Adapter 192.168.0.10". */
void home_top_detail(const home_top_t *t, char *out, size_t len);

/* Short protocol name for ELM327 protocol numbers 1-12, "" otherwise. */
const char *home_protocol_name(int protocol);

/* The odometer in the display unit as "123,456" (rounded down, groups of
 * three), "" if none. */
void home_top_odometer(const home_top_t *t, char *out, size_t len);

/* "mi" or "km". */
const char *home_top_dist_unit(const home_top_t *t);

/* The last drive's distance "12.4" (one decimal, at most 999.9) and when:
 * "today" or "11/16". Both "" if none. */
void home_top_last(const home_top_t *t, char *dist, size_t dlen, char *when, size_t wlen);

/* "17.8" and "mpg average" / "L/100 km average"; "" and "" if none. */
void home_top_economy(const home_top_t *t, char *value, size_t vlen, char *label, size_t llen);

/* Digits with thousands commas: 123456 -> "123,456". */
void home_group_digits(long v, char *out, size_t len);

#endif
