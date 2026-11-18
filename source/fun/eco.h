#ifndef GAUGEBOY_ECO_H
#define GAUGEBOY_ECO_H

/*
 * Eco Challenge (brief section 5e): a live fuel economy estimate, and a
 * score and rank (C to S) for smooth driving over a trip.
 *
 * Fuel flow comes from the air the engine takes in, at the stoichiometric
 * ratio (ECO_AFR) and petrol's density:
 *   - MAF (PID 10), grams of air a second, when the car has it;
 *   - else speed-density: MAP (0B or 87), RPM, intake air temperature (0F,
 *     ECO_IAT_DEFAULT_C if missing) and the engine size from SETUP, with an
 *     assumed volumetric efficiency (ECO_VE). Without an engine size there
 *     is no estimate (the score still works).
 * It's an estimate: closed-loop trims, enrichment and fuel cut aren't seen.
 *
 * The score: every stretch between speed readings earns points, weighted by
 * its length, and the score is their average (0-100):
 *   moving    100, less for hard acceleration (over 1.5 / 2.5 m/s^2,
 *             measured over at least ECO_ACCEL_MS),
 *             hard braking (over 2.0 / 3.0 m/s^2) or high RPM (2800 / 3500);
 *   stopped   100 for the first ECO_IDLE_FREE_S of a stop (lights,
 *             traffic), then ECO_IDLE_POINTS (idling for nothing).
 * A trip is ranked once it has ECO_RANK_MOVING_S of driving and
 * ECO_RANK_KM: S 90+, A 80+, B 65+, else C.
 *
 * Platform-free: feed it each new speed reading with the latest other
 * values.
 */

#include <stdbool.h>
#include <stdint.h>

#define ECO_AFR             14.7f    /* petrol, stoichiometric */
#define ECO_FUEL_G_PER_L    745.0f
#define ECO_VE              0.85f
#define ECO_IAT_DEFAULT_C   25.0f
#define ECO_GAP_MS          5000u    /* readings further apart aren't counted */
#define ECO_ACCEL_MS        1000u    /* acceleration measured over at least this */
#define ECO_IDLE_FREE_S     60.0
#define ECO_IDLE_POINTS     30.0
#define ECO_RANK_MOVING_S   60.0
#define ECO_RANK_KM         0.5
#define ECO_L100_PER_MPG    235.215f /* L/100 km = this / mpg (US) */
#define ECO_L_PER_GAL       3.78541f

typedef struct {
    bool have_speed, have_rpm, have_maf, have_map, have_iat;
    float kmh, rpm, maf_gs, map_kpa, iat_c;
} eco_input_t;

typedef enum {
    ECO_TIP_NONE = 0,
    ECO_TIP_ACCEL,       /* hard acceleration */
    ECO_TIP_BRAKE,       /* hard braking */
    ECO_TIP_RPM,         /* high RPM */
    ECO_TIP_IDLE,        /* a long idle */
} eco_tip_t;

typedef struct {
    bool started;
    uint64_t last_ms;
    float last_kmh;
    double stopped_s;    /* this stop so far */
    uint64_t acc_ms;     /* the start of the acceleration window */
    float acc_kmh;
    float accel;         /* m/s^2 over the last full window (whole km/h readings
                          * many times a second would look like jolts) */

    double fuel_l, dist_km, moving_s, idle_s;
    double points_sum, points_s;
    int harsh_accel, harsh_brake;
    bool in_accel, in_brake;     /* for counting each event once */

    bool have_lph;
    float lph;           /* the latest fuel flow, L/h */
    float kmh;           /* the latest speed */
    eco_tip_t tip;       /* the latest advice */
    uint64_t tip_ms;
} eco_t;

void eco_reset(eco_t *eco);

/* Fuel flow in L/h from in (engine_l: litres, 0 = unknown); false if it
 * can't be estimated. */
bool eco_fuel_lph(const eco_input_t *in, float engine_l, float *lph);

/* A new speed reading at t_ms, with the latest other values. */
void eco_step(eco_t *eco, uint64_t t_ms, const eco_input_t *in, float engine_l);

/* The trip's score, 0-100 (100 before anything is counted). */
int eco_score(const eco_t *eco);

/* Enough driving for a rank. */
bool eco_ranked(const eco_t *eco);

/* "S", "A", "B" or "C". */
const char *eco_rank(int score);

/* The trip's average, L/100 km; false under 0.1 km or without fuel data. */
bool eco_avg_l100(const eco_t *eco, float *l100);

/* The latest instant economy, L/100 km; false when stopped or unknown. */
bool eco_now_l100(const eco_t *eco, float *l100);

const char *eco_tip_text(eco_tip_t tip);

#endif
