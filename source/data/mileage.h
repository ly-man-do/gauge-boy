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
 * odometer at the start and end, the fuel used and (optionally) its price,
 * giving the economy and the cost:
 *
 *   car,date,start_km,end_km,fuel_l,price_l
 *   Geo/Tracker/1996,2026-11-18,77140.2,77502.6,30.28,0.9139
 *
 * price_l is per litre, 0 when not given; lines written before it existed
 * have five fields and read as 0. Currency isn't stored (US shows "$").
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
    double price_l;                   /* per litre, 0 = not given */
} mileage_entry_t;

typedef struct {
    mileage_entry_t e[MILEAGE_MAX];   /* oldest first */
    int n;
    double km, fuel_l;                /* totals */
    double cost;                      /* spent on the entries with a price */
    double cost_km;                   /* the distance those entries cover */
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

/* An entry's cost (fuel x price); 0 without a price. */
double mileage_cost(const mileage_entry_t *e);

/* What's wrong with an entry (in km and litres), or NULL if it's fine. */
const char *mileage_check(double start_km, double end_km, double fuel_l);

#endif
