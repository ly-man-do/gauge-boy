#ifndef GAUGEBOY_TIME_ATTACK_H
#define GAUGEBOY_TIME_ATTACK_H

/*
 * Time Attack timers (brief section 5e): 0-60 mph (0-100 km/h), 60-0
 * (100-0) braking and a 1/4-mile estimate, from vehicle speed (PID 0D).
 *
 * Never needs a touch while moving (safety rule): pick a run while parked;
 * it waits for the car to stand still, counts down 3, 2, 1, GO, then
 * starts by itself on the first speed above 0 (braking: when the speed
 * drops back under 60 after driving faster). Results stay on screen, and
 * the timer arms itself again once the car has stood still for a moment.
 *
 * OBD speed is whole km/h a few times a second, so times are estimates:
 *   - launch: the moment the car left 0 is extrapolated back from the
 *     first two moving readings (steady acceleration), never earlier than
 *     the last 0 reading or later than the first moving one;
 *   - crossing a speed: interpolated between the readings either side;
 *   - stopping (braking): extrapolated forward from the last two moving
 *     readings, within the last moving and the first 0 reading;
 *   - distance: the area under the speed readings (trapezoids).
 *
 * Platform-free: feed it each new speed reading (ta_speed) and the clock
 * (ta_tick); the app draws it and saves the bests.
 */

#include <stdbool.h>
#include <stdint.h>

#define TA_COUNTDOWN_MS     3000u   /* 3, 2, 1, then GO */
#define TA_GO_SHOW_MS       1000u   /* "GO!" shown this long */
#define TA_STILL_MS         1000u   /* stood still this long: arm */
#define TA_REARM_MS         4000u   /* a result stays at least this long before re-arming */
#define TA_GAP_MS           2500u   /* no reading this long in a run: stop it */
#define TA_LAUNCH_MAX_MS    60000u  /* a launch run longer than this is given up */
#define TA_BRAKE_MAX_MS     15000u  /* a braking run longer than this is given up */
#define TA_QUARTER_M        402.336 /* 1/4 mile in metres */
#define TA_MPH_KMH          1.609344f

typedef enum {
    TA_RUN_LAUNCH = 0,   /* 0-60 mph or 0-100 km/h */
    TA_RUN_BRAKE,        /* 60-0 mph or 100-0 km/h */
    TA_RUN_QUARTER,      /* 1/4 mile, with the trap speed and the 0-60 split */
    TA_RUN_COUNT
} ta_run_t;

typedef enum {
    TA_OFF = 0,
    TA_STOP_FIRST,       /* armed, waiting for the car to stand still */
    TA_COUNTDOWN,        /* 3, 2, 1 (the car must stay still) */
    TA_GO,               /* launch runs: waiting for the first movement */
    TA_BUILD_UP,         /* braking: drive above the target speed */
    TA_BRAKE_READY,      /* braking: above the target, waiting to slow under it */
    TA_RUNNING,
    TA_DONE,             /* a result (kept until the next one) */
} ta_state_t;

typedef struct {
    bool have;
    ta_run_t run;
    bool metric;
    float seconds;       /* the time */
    float trap_kmh;      /* quarter: speed at the line */
    float split_s;       /* quarter: 0-60 (0-100) on the way, 0 if not reached */
    float distance_m;    /* braking: stopping distance; quarter: 402.3 */
    float hz;            /* speed readings a second during the run */
} ta_result_t;

typedef struct {
    ta_run_t run;
    bool metric;
    float target_kmh;    /* 96.56 (60 mph) or 100 */
    ta_state_t state;
    uint64_t state_ms;   /* when the state began */
    const char *note;    /* why the last run stopped ("False start" ...), or NULL */

    bool have_last;
    uint64_t last_ms;    /* the latest reading */
    float last_kmh;
    bool have_prev;      /* the reading before it */
    uint64_t prev_ms;
    float prev_kmh;
    bool have_pprev;     /* and the one before that (braking: the deceleration) */
    uint64_t pprev_ms;
    float pprev_kmh;
    uint64_t still_since;   /* first of the 0 readings in a row (valid if last_kmh == 0) */

    /* The run. */
    double start_ms;     /* estimated start (launch: fixed after 2 moving readings) */
    bool start_fixed;
    uint64_t zero_ms;    /* launch: the last 0 reading before moving */
    uint64_t move1_ms;   /* launch: the first moving reading */
    float move1_kmh;
    double dist_m;       /* from the first moving reading (launch) or the start (braking) */
    bool split_done;
    float split_s;
    int samples;

    ta_result_t result;  /* the latest result (kept while re-arming) */
} ta_t;

/* Arms a run: waits for the car to stand still, then counts down. */
void ta_arm(ta_t *ta, ta_run_t run, bool metric, uint64_t now_ms);

/* Stops timing (leaving the screen). */
void ta_off(ta_t *ta);

/* A new speed reading (km/h) read at t_ms. Returns true when it finished
 * a run (ta->result is new). */
bool ta_speed(ta_t *ta, uint64_t t_ms, float kmh);

/* The clock: countdown, lost readings, re-arming. Returns true if the
 * state changed. */
bool ta_tick(ta_t *ta, uint64_t now_ms);

/* During the countdown: 3, 2 or 1; 0 otherwise. */
int ta_countdown(const ta_t *ta, uint64_t now_ms);

/* "0-60 mph", "100-0 km/h", "1/4 mile". */
const char *ta_run_name(ta_run_t run, bool metric);

/* Short key for the bests file: "0-60", "0-100", "60-0", "100-0", "quarter". */
const char *ta_run_key(ta_run_t run, bool metric);

/* The target speed in km/h: 60 mph or 100 km/h. */
float ta_target_kmh(bool metric);

#endif
