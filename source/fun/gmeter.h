#ifndef GAUGEBOY_GMETER_H
#define GAUGEBOY_GMETER_H

/*
 * G-meter (brief section 5e): the car's acceleration from the 3DS's own
 * accelerometer, as a ball on a friction circle with peak hold.
 *
 * The 3DS is mounted at any angle. Calibrate (parked, the 3DS still)
 * measures gravity: its size is the sensor's scale (raw units per g) and
 * its direction is "up". After that, a reading minus the calibration is
 * the car's acceleration in g. Its part along "up" is ignored (bumps);
 * across it, the 3DS's left-right axis gives the side axis and the axis
 * at right angles to both the forward one.
 *
 * Which way along that axis is forward isn't known from gravity, so it is
 * learned: on the first launch (OBD speed from 0 to GM_LEARN_KMH within
 * GM_LEARN_MS) the average acceleration along the axis must be forward.
 * Until then the 3DS's own guess is used (screen facing the driver).
 * Right then follows from forward and up.
 *
 * Platform-free: the app reads the sensor (hidAccelRead) and the speed.
 */

#include <stdbool.h>
#include <stdint.h>

#define GM_CAL_SAMPLES    30        /* about half a second at 60 frames a second */
#define GM_CAL_STILL      0.04f     /* each sample within 4 % of the average */
#define GM_TAU_MS         150.0f    /* smoothing time constant */
#define GM_LEARN_KMH      10.0f     /* a launch: 0 to this ... */
#define GM_LEARN_MS       4000u     /* ... within this */
#define GM_LEARN_MIN_G    0.05f     /* and at least this much acceleration on average */

typedef struct {
    float x, y, z;
} gm_vec_t;

typedef enum {
    GM_CAL_NONE = 0,     /* not calibrated */
    GM_CAL_RUNNING,      /* collecting samples */
    GM_CAL_OK,
    GM_CAL_MOVED,        /* the 3DS moved during it: try again */
    GM_CAL_NO_SENSOR,    /* readings near zero: the accelerometer isn't on */
} gm_cal_t;

typedef struct {
    gm_cal_t cal;
    int cal_n;
    gm_vec_t cal_samples[GM_CAL_SAMPLES];
    gm_vec_t g0;         /* gravity at calibration, raw */
    float scale;         /* raw units per g */
    gm_vec_t up, fwd0;   /* unit vectors (raw axes) */
    int forward_sign;    /* +1 or -1: fwd0 or its opposite is forward */
    bool forward_learned;

    bool have;
    float lat, lon;      /* smoothed, g: lat + = to the right, lon + = speeding up */
    float peak_left, peak_right, peak_accel, peak_brake, peak_total;

    /* Learning forward on a launch. */
    bool learning;
    uint64_t learn_t0;
    double learn_sum;    /* acceleration along fwd0 */
    int learn_n;
    bool have_speed;
    float last_kmh;
} gmeter_t;

void gm_init(gmeter_t *gm);

/* Starts a calibration (keeps the learned forward if the mounting is the
 * same: it is re-learned only if up changes much). */
void gm_cal_start(gmeter_t *gm);

/* One raw sample while calibrating. Returns the state: RUNNING until it
 * has GM_CAL_SAMPLES, then OK, MOVED or NO_SENSOR. */
gm_cal_t gm_cal_add(gmeter_t *gm, gm_vec_t raw);

/* One raw sample dt_ms after the last (calibrated only): smoothing, peaks,
 * learning. */
void gm_sample(gmeter_t *gm, gm_vec_t raw, float dt_ms);

/* A vehicle speed reading (km/h) at t_ms, for learning forward. */
void gm_speed(gmeter_t *gm, uint64_t t_ms, float kmh);

void gm_reset_peaks(gmeter_t *gm);

/* Total (lateral and longitudinal together), g. */
float gm_total(const gmeter_t *gm);

#endif
