#ifndef GAUGEBOY_MILEAGE_H
#define GAUGEBOY_MILEAGE_H

/*
 * The garage card's trip meter and mileage log.
 *
 * Trip meter: per car, the odometer reading when the trip was reset
 * (trip.csv, "car,km,date", read and written with baro_store_get() /
 * keyval_store_set()); the trip is the odometer now minus that.
 *
 * Mileage log (mileage.csv): per car, entries the user types: the
 * odometer at the start and end and the fuel used, giving the economy:
 *
 *   car,date,start_km,end_km,fuel_l
 *   Geo/Tracker/1996,2026-11-18,77140.2,77502.6,30.28
 *
 * Kept in km and litres; shown in the units setting (mi, gal, mpg or km,
 * L, L/100 km). Platform-free: text in, text out.
 */

#include <stdbool.h>
#include <stddef.h>

#define MILEAGE_FILE    "mileage.csv"
#define TRIP_FILE       "trip.csv"
#define TRIP_HEADER     "car,km,date\n"
#define MILEAGE_MAX     64          /* entries read per car (the newest) */
#define MILEAGE_KM_MI   1.609344
#define MILEAGE_L_GAL   3.785411784

typedef struct {
    char date[16];
    double start_km, end_km, fuel_l;
} mileage_entry_t;

typedef struct {
    mileage_entry_t e[MILEAGE_MAX];   /* oldest first */
    int n;
    double km, fuel_l;                /* totals */
} mileage_list_t;

/* car's entries from mileage.csv's text. Returns how many. */
int mileage_read(const char *text, const char *car, mileage_list_t *out);

/* text with a new entry for car appended (header added if missing) into
 * out. Returns the length, 0 if out is too small. */
size_t mileage_add(const char *text, const char *car, const mileage_entry_t *e, char *out,
                   size_t len);

/* text without car's newest entry. Returns the length (0 if out is too
 * small); *removed says whether there was one. */
size_t mileage_delete_last(const char *text, const char *car, bool *removed, char *out,
                           size_t len);

/* Litres per 100 km for a distance and fuel; false if either is 0 or less. */
bool mileage_l100(double km, double fuel_l, double *l100);

/* mpg (US) for a distance and fuel; false if either is 0 or less. */
bool mileage_mpg(double km, double fuel_l, double *mpg);

/* A number as typed: "29,958", "29958.4", " 14.2 ". False if it isn't one
 * (or is negative). */
bool mileage_parse(const char *text, double *v);

/* What's wrong with an entry (in km and litres), or NULL if it's fine. */
const char *mileage_check(double start_km, double end_km, double fuel_l);

#endif
