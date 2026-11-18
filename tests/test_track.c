/* Time Attack timers and personal bests (source/fun). */
#include <math.h>
#include <string.h>

#include "harness.h"
#include "fun/gmeter.h"
#include "fun/shift_light.h"
#include "fun/time_attack.h"
#include "fun/track_bests.h"

static ta_t ta;

/* Speed as the car reports it: whole km/h. */
static float obd_kmh(double kmh)
{
    return kmh <= 0.0 ? 0.0f : (float)floor(kmh);
}

/* Feeds standing-still readings from t to before `until`, ticking too. */
static uint64_t feed_still(uint64_t t, uint64_t until, uint64_t step)
{
    for (; t < until; t += step) {
        ta_speed(&ta, t, 0.0f);
        ta_tick(&ta, t);
    }
    return t;
}

static void test_ta_launch(void)
{
    ta_arm(&ta, TA_RUN_LAUNCH, false, 0);
    CHECK_EQ_INT(ta.state, TA_STOP_FIRST);
    uint64_t t = feed_still(0, 1200, 200);
    CHECK_EQ_INT(ta.state, TA_COUNTDOWN);                 /* still for 1 s */
    CHECK_EQ_INT(ta_countdown(&ta, ta.state_ms), 3);
    CHECK_EQ_INT(ta_countdown(&ta, ta.state_ms + 2500), 1);
    t = feed_still(t, 5200, 200);
    CHECK_EQ_INT(ta.state, TA_GO);

    /* Leaves at 5150 ms, 10 km/h a second; 60 mph (96.56 km/h) at 9.656 s. */
    const double launch = 5150.0;
    bool done = false;
    for (; t < 30000 && !done; t += 230) {
        done = ta_speed(&ta, t, obd_kmh((t - launch) * 0.01));
    }
    CHECK(done && ta.state == TA_DONE && ta.result.have);
    CHECK(fabsf(ta.result.seconds - 9.656f) < 0.15f);
    CHECK(ta.result.hz > 3.5f && ta.result.hz < 5.0f);

    /* Stays on screen; re-arms after standing still again. */
    uint64_t done_at = ta.state_ms;
    t += 5000;
    t = feed_still(t, t + 1200, 200);
    CHECK(ta.state == TA_COUNTDOWN && ta.result.have);   /* the result is kept */
    CHECK(ta.state_ms - done_at >= TA_REARM_MS);
}

static void test_ta_false_start_and_give_up(void)
{
    ta_arm(&ta, TA_RUN_LAUNCH, true, 0);
    CHECK(fabsf(ta.target_kmh - 100.0f) < 0.01f);
    uint64_t t = feed_still(0, 1400, 200);
    CHECK_EQ_INT(ta.state, TA_COUNTDOWN);
    ta_speed(&ta, t, 3.0f);                               /* moved during 3, 2, 1 */
    CHECK_EQ_INT(ta.state, TA_STOP_FIRST);
    CHECK(ta.note != NULL && strstr(ta.note, "False start") != NULL);

    t = feed_still(t + 200, t + 6000, 200);
    CHECK_EQ_INT(ta.state, TA_GO);
    ta_speed(&ta, t, 5.0f);
    ta_speed(&ta, t + 200, 9.0f);
    CHECK_EQ_INT(ta.state, TA_RUNNING);
    ta_speed(&ta, t + 400, 0.0f);                         /* stopped short */
    CHECK_EQ_INT(ta.state, TA_STOP_FIRST);
    CHECK(!ta.result.have);

    /* Readings stop mid-run. */
    t = feed_still(t + 600, t + 6000, 200);
    ta_speed(&ta, t, 4.0f);
    CHECK_EQ_INT(ta.state, TA_RUNNING);
    CHECK(ta_tick(&ta, t + TA_GAP_MS + 1));
    CHECK_EQ_INT(ta.state, TA_STOP_FIRST);
    CHECK_EQ_STR(ta.note, "Speed readings stopped");

    /* Stale zeros don't count as standing still. */
    CHECK(!ta_tick(&ta, t + 20000));
    CHECK_EQ_INT(ta.state, TA_STOP_FIRST);
}

static void test_ta_quarter(void)
{
    ta_arm(&ta, TA_RUN_QUARTER, false, 0);
    uint64_t t = feed_still(0, 4400, 200);
    CHECK_EQ_INT(ta.state, TA_GO);
    /* 3 m/s^2 from 4520 ms: the 1/4 mile in 16.38 s at 49.1 m/s (176.9 km/h);
     * 60 mph (26.82 m/s) at 8.94 s. */
    const double launch = 4520.0;
    bool done = false;
    for (; t < 40000 && !done; t += 300) {
        done = ta_speed(&ta, t, obd_kmh(t > launch ? (t - launch) / 1000.0 * 3.0 * 3.6 : 0.0));
    }
    CHECK(done && ta.result.have && ta.result.run == TA_RUN_QUARTER);
    CHECK(fabsf(ta.result.seconds - 16.38f) < 0.2f);
    CHECK(fabsf(ta.result.trap_kmh - 176.9f) < 3.0f);
    CHECK(fabsf(ta.result.split_s - 8.94f) < 0.2f);
}

static void test_ta_brake(void)
{
    ta_arm(&ta, TA_RUN_BRAKE, false, 0);
    uint64_t t = feed_still(0, 4400, 200);
    CHECK_EQ_INT(ta.state, TA_BUILD_UP);
    /* Up to 120 km/h, cruise, then brake at 30 km/h a second from 20 s:
     * 60 mph to 0 in 3.219 s and 43.2 m. */
    bool done = false;
    for (; t < 40000 && !done; t += 210) {
        double kmh;
        if (t < 16000) {
            kmh = (t - 4400) / 1000.0 * 12.0;
            kmh = kmh > 120.0 ? 120.0 : kmh;
        } else if (t < 20000) {
            kmh = 120.0;
        } else {
            kmh = 120.0 - (t - 20000) / 1000.0 * 30.0;
        }
        done = ta_speed(&ta, t, obd_kmh(kmh));
        if (t > 15000 && t < 20000) {
            CHECK_EQ_INT(ta.state, TA_BRAKE_READY);
        }
    }
    CHECK(done && ta.result.have && ta.result.run == TA_RUN_BRAKE);
    CHECK(fabsf(ta.result.seconds - 3.219f) < 0.15f);
    CHECK(fabsf(ta.result.distance_m - 43.2f) < 2.5f);
}

static void test_ta_names(void)
{
    CHECK_EQ_STR(ta_run_name(TA_RUN_LAUNCH, false), "0-60 mph");
    CHECK_EQ_STR(ta_run_name(TA_RUN_BRAKE, true), "100-0 km/h");
    CHECK_EQ_STR(ta_run_key(TA_RUN_QUARTER, true), "quarter");
    CHECK_EQ_STR(ta_run_key(TA_RUN_LAUNCH, false), "0-60");
    ta_t off;
    memset(&off, 0, sizeof off);
    CHECK(!ta_speed(&off, 100, 50.0f));                   /* not armed: ignored */
}

static void test_track_bests(void)
{
    static char a[4096], b[4096];
    bool rec = false;
    size_t n = track_bests_offer("", "Geo/Tracker/1996", "0-60", 14.62f, 0.0f, "2026-11-18", &rec,
                                 a, sizeof a);
    CHECK(n > 0 && rec);
    CHECK_EQ_STR(a, "car,run,seconds,extra,date\nGeo/Tracker/1996,0-60,14.62,,2026-11-18\n");

    track_bests_offer(a, "Geo/Tracker/1996", "0-60", 15.1f, 0.0f, "2026-11-19", &rec, b, sizeof b);
    CHECK(!rec);                                          /* slower: unchanged */
    CHECK_EQ_STR(b, a);

    track_bests_offer(a, "Geo/Tracker/1996", "quarter", 19.81f, 112.43f, "2026-11-19", &rec, b,
                      sizeof b);
    CHECK(rec);
    strcpy(a, b);
    track_bests_offer(a, "Geo/Tracker/1996", "0-60", 13.9f, 0.0f, "2026-11-20", &rec, b, sizeof b);
    CHECK(rec);
    CHECK_EQ_STR(b, "car,run,seconds,extra,date\n"
                    "Geo/Tracker/1996,quarter,19.81,112.4,2026-11-19\n"
                    "Geo/Tracker/1996,0-60,13.90,,2026-11-20\n");

    track_best_t best;
    CHECK(track_bests_get(b, "Geo/Tracker/1996", "quarter", &best));
    CHECK(fabsf(best.seconds - 19.81f) < 0.001f && fabsf(best.extra - 112.4f) < 0.01f);
    CHECK_EQ_STR(best.date, "2026-11-19");
    CHECK(!track_bests_get(b, "Geo/Tracker/1996", "60-0", &best));
    CHECK(!track_bests_get(b, "", "0-60", &best));
    CHECK(!track_bests_get("junk\n,,,,\ncar,run,seconds,extra,date\n", "car", "run", &best));

    /* No car: never saved. Too small: 0. */
    CHECK(track_bests_offer(b, "", "0-60", 1.0f, 0.0f, "x", &rec, a, sizeof a) > 0 && !rec);
    CHECK_EQ_INT(track_bests_offer("", "car", "0-60", 1.0f, 0.0f, "x", &rec, a, 10), 0);
}

static void test_shift_light(void)
{
    shift_light_t sl;
    shift_init(&sl, 6000);
    CHECK_EQ_INT(sl.shift_rpm, 6000);
    CHECK_EQ_INT(shift_update(&sl, 800.0f), 0);           /* idle: dark */
    CHECK_EQ_INT(shift_update(&sl, 3599.0f), 0);          /* under 60 % */
    CHECK_EQ_INT(shift_update(&sl, 3600.0f), 1);          /* the first LED */
    CHECK_EQ_INT(shift_update(&sl, 4800.0f), 5);          /* half way */
    CHECK_EQ_INT(shift_update(&sl, 5999.0f), 9);          /* all but the last */
    CHECK(!sl.flashing);
    CHECK_EQ_INT(shift_update(&sl, 6000.0f), SHIFT_LEDS);  /* shift! */
    CHECK(sl.flashing);
    CHECK_EQ_INT(shift_update(&sl, 5850.0f), SHIFT_LEDS);  /* within the hysteresis */
    CHECK(sl.flashing);
    CHECK_EQ_INT(shift_update(&sl, 5700.0f), 8);          /* shifted: back to the bar */
    CHECK(!sl.flashing);

    CHECK(shift_flash_on(0) && !shift_flash_on(SHIFT_FLASH_MS) && shift_flash_on(2 * SHIFT_FLASH_MS));
    CHECK_EQ_INT(shift_led_color(0), SHIFT_GREEN);
    CHECK_EQ_INT(shift_led_color(5), SHIFT_AMBER);
    CHECK_EQ_INT(shift_led_color(SHIFT_LEDS - 1), SHIFT_RED);

    CHECK_EQ_INT(shift_step(6000, 100), 6100);
    CHECK_EQ_INT(shift_step(6000, -10000), SHIFT_RPM_MIN);
    CHECK_EQ_INT(shift_step(9900, 100), SHIFT_RPM_MAX);
    CHECK_EQ_INT(shift_step(6049, 0), 6000);
    shift_init(&sl, 50);
    CHECK_EQ_INT(sl.shift_rpm, SHIFT_RPM_MIN);
}

static void test_shift_predict(void)
{
    shift_light_t sl;
    shift_init(&sl, 6000);
    CHECK(shift_rpm_now(&sl, 100) == 0.0f);
    shift_reading(&sl, 1000, 3000.0f);
    CHECK(shift_rpm_now(&sl, 1100) == 3000.0f);           /* one reading: no trend */
    shift_reading(&sl, 1200, 4000.0f);                    /* +5 rpm a ms */
    CHECK(fabsf(shift_rpm_now(&sl, 1300) - 4500.0f) < 1.0f);
    CHECK(fabsf(shift_rpm_now(&sl, 5000) - 5500.0f) < 1.0f);   /* capped at 300 ms */
    CHECK(shift_rpm_now(&sl, 1200) == 4000.0f);
    shift_reading(&sl, 1100, 9000.0f);                    /* out of order: ignored */
    CHECK(sl.rpm1 == 4000.0f);
    shift_reading(&sl, 1400, 1000.0f);                    /* a fast drop (shifted) */
    CHECK(shift_rpm_now(&sl, 1700) == 0.0f);              /* never negative */
}

static gmeter_t gm;

static void gm_calibrate(gm_vec_t g0)
{
    gm_init(&gm);
    gm_cal_start(&gm);
    for (int i = 0; i < GM_CAL_SAMPLES - 1; i++) {
        CHECK_EQ_INT(gm_cal_add(&gm, g0), GM_CAL_RUNNING);
    }
    CHECK_EQ_INT(gm_cal_add(&gm, g0), GM_CAL_OK);
}

/* Feeds a steady reading long enough for the smoothing to settle. */
static void gm_hold(gm_vec_t raw)
{
    for (int i = 0; i < 120; i++) {
        gm_sample(&gm, raw, 16.7f);
    }
}

#define NEAR(a, b) (fabsf((a) - (b)) < 0.02f)

static void test_gmeter_flat(void)
{
    /* Lying flat: gravity on +z, 512 raw a g; forward guess +y, right +x. */
    gm_vec_t g0 = { 0, 0, 512 };
    gm_calibrate(g0);
    CHECK(NEAR(gm.scale, 512.0f));
    gm_hold(g0);
    CHECK(NEAR(gm.lat, 0.0f) && NEAR(gm.lon, 0.0f));
    gm_vec_t accel = { 0, 0.3f * 512, 512 };
    gm_hold(accel);
    CHECK(NEAR(gm.lon, 0.3f) && NEAR(gm.lat, 0.0f));
    gm_vec_t right_turn = { 0.5f * 512, 0, 512 };
    gm_hold(right_turn);
    CHECK(NEAR(gm.lat, 0.5f) && NEAR(gm.lon, 0.0f));
    gm_vec_t brake_left = { -0.2f * 512, -0.8f * 512, 512 };
    gm_hold(brake_left);
    CHECK(NEAR(gm.lat, -0.2f) && NEAR(gm.lon, -0.8f));
    CHECK(NEAR(gm.peak_accel, 0.3f) && NEAR(gm.peak_right, 0.5f));
    CHECK(NEAR(gm.peak_brake, 0.8f) && NEAR(gm.peak_left, 0.2f));
    CHECK(gm.peak_total > 0.81f && gm.peak_total < 0.84f);
    gm_vec_t bump = { 0, 0, 1.5f * 512 };                 /* straight up: ignored */
    gm_hold(bump);
    CHECK(NEAR(gm.lat, 0.0f) && NEAR(gm.lon, 0.0f));
    gm_reset_peaks(&gm);
    CHECK(gm.peak_brake == 0.0f);

    /* Smoothing: one jolt moves it only a little. */
    gm_hold(g0);
    gm_sample(&gm, accel, 16.7f);
    CHECK(gm.lon > 0.01f && gm.lon < 0.1f);
}

static void test_gmeter_calibration_fails(void)
{
    gm_init(&gm);
    gm_cal_start(&gm);
    gm_vec_t g0 = { 0, 0, 512 }, shaken = { 0, 60, 512 }, zero = { 0, 0, 0 };
    for (int i = 0; i < GM_CAL_SAMPLES; i++) {
        gm_cal_add(&gm, i == 10 ? shaken : g0);
    }
    CHECK_EQ_INT(gm.cal, GM_CAL_MOVED);
    gm_sample(&gm, g0, 16.7f);
    CHECK(!gm.have);                                      /* not calibrated: nothing */
    gm_cal_start(&gm);
    for (int i = 0; i < GM_CAL_SAMPLES; i++) {
        gm_cal_add(&gm, zero);
    }
    CHECK_EQ_INT(gm.cal, GM_CAL_NO_SENSOR);
}

static void test_gmeter_learns_forward(void)
{
    /* Upright, gravity on -y: the forward guess is +z, but the car's
     * forward is -z. A launch teaches it. */
    gm_vec_t g0 = { 0, -512, 0 };
    gm_calibrate(g0);
    gm_vec_t launch = { 0, -512, -0.3f * 512 };
    gm_hold(launch);
    CHECK(NEAR(gm.lon, -0.3f));                           /* guessed wrong */
    gm_speed(&gm, 1000, 0.0f);
    gm_speed(&gm, 1200, 2.0f);                            /* moving off */
    CHECK(gm.learning);
    gm_hold(launch);
    gm_speed(&gm, 2500, 8.0f);
    gm_hold(launch);
    gm_speed(&gm, 3000, 12.0f);
    CHECK(gm.forward_learned && gm.forward_sign == -1 && !gm.learning);
    gm_hold(launch);
    CHECK(NEAR(gm.lon, 0.3f));
    CHECK(gm.peak_brake < 0.01f);                         /* the mirrored peaks went */
    /* Right from forward and up: right-handed, so +x on this mounting is left. */
    gm_vec_t turn = { 0.4f * 512, -512, 0 };
    gm_hold(turn);
    CHECK(NEAR(gm.lat, -0.4f));

    /* Recalibrating at the same mounting keeps what it learned. */
    gm_cal_start(&gm);
    for (int i = 0; i < GM_CAL_SAMPLES; i++) {
        gm_cal_add(&gm, g0);
    }
    CHECK(gm.forward_learned && gm.forward_sign == -1);

    /* A slow roll-off doesn't teach anything. */
    gm_calibrate(g0);
    gm_speed(&gm, 0, 0.0f);
    gm_speed(&gm, 100, 1.0f);
    gm_hold(launch);
    gm_speed(&gm, 9000, 10.0f);                           /* too slow */
    CHECK(!gm.forward_learned && !gm.learning);
}

void suite_track(void)
{
    RUN_TEST(test_gmeter_flat);
    RUN_TEST(test_gmeter_calibration_fails);
    RUN_TEST(test_gmeter_learns_forward);
    RUN_TEST(test_shift_light);
    RUN_TEST(test_shift_predict);
    RUN_TEST(test_ta_launch);
    RUN_TEST(test_ta_false_start_and_give_up);
    RUN_TEST(test_ta_quarter);
    RUN_TEST(test_ta_brake);
    RUN_TEST(test_ta_names);
    RUN_TEST(test_track_bests);
}
