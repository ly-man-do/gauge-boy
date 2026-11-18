/* Time Attack timers and personal bests (source/fun). */
#include <math.h>
#include <string.h>

#include "harness.h"
#include "fun/achieve.h"
#include "fun/eco.h"
#include "fun/gmeter.h"
#include "fun/rev_sync.h"
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

    /* Scores: higher wins. */
    track_bests_offer_high(b, "Geo/Tracker/1996", "revsync", 3300.0f, "2026-11-20", &rec, a,
                           sizeof a);
    CHECK(rec);
    track_bests_offer_high(a, "Geo/Tracker/1996", "revsync", 2900.0f, "2026-11-21", &rec, b,
                           sizeof b);
    CHECK(!rec);
    track_bests_offer_high(a, "Geo/Tracker/1996", "revsync", 4100.0f, "2026-11-21", &rec, b,
                           sizeof b);
    CHECK(rec);
    CHECK(track_bests_get(b, "Geo/Tracker/1996", "revsync", &best) && best.seconds == 4100.0f);

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

static rev_sync_t rs;

/* One round: idle readings while READY, then a blip peaking at peak (0 =
 * no blip), every 100 ms with speed 0; returns the time after it. */
static uint64_t rs_round(uint64_t t, float peak)
{
    for (; rs.state == RS_READY; t += 100) {
        rs_speed(&rs, t, 0.0f);
        rs_rpm(&rs, t, 750.0f);
        rs_tick(&rs, t);
    }
    CHECK_EQ_INT(rs.state, RS_GO);
    float shape[] = { 900, 1600, 2400, 0, 0, 1800, 1100, 800 };   /* 0: the peak */
    for (int i = 0; i < 8 && rs.state == RS_GO && peak > 0.0f; i++, t += 100) {
        rs_speed(&rs, t, 0.0f);
        rs_rpm(&rs, t, shape[i] == 0 ? (i == 3 ? peak - 30.0f : peak) : fminf(shape[i], peak));
        rs_tick(&rs, t);
    }
    for (; rs.state == RS_GO; t += 100) {                 /* no blip: time runs out */
        rs_speed(&rs, t, 0.0f);
        rs_rpm(&rs, t, 760.0f);
        rs_tick(&rs, t);
    }
    CHECK_EQ_INT(rs.state, RS_JUDGE);
    for (; rs.state == RS_JUDGE; t += 100) {
        rs_speed(&rs, t, 0.0f);
        rs_tick(&rs, t);
    }
    return t;
}

static void test_rev_sync_game(void)
{
    rs_init(&rs, 6000, 42);
    CHECK_EQ_INT(rs.state, RS_LOCKED);                    /* no speed read yet */
    CHECK(rs.note == NULL);
    rs_speed(&rs, 50, 40.0f);                             /* already moving: says so */
    CHECK(rs.state == RS_LOCKED && rs.note != NULL);
    CHECK(!rs_start(&rs, 0));
    rs_speed(&rs, 100, 0.0f);
    CHECK_EQ_INT(rs.state, RS_IDLE);
    CHECK(rs_start(&rs, 100));
    for (int i = 0; i < RS_ROUNDS; i++) {
        CHECK(rs.target[i] >= RS_TARGET_MIN && rs.target[i] <= RS_TARGET_MAX &&
              rs.target[i] % 100 == 0);
    }
    uint64_t t = 100;
    float off[RS_ROUNDS] = { 20, 30, -150, 0, 500 };      /* perfect, perfect, great, miss, ok */
    for (int r = 0; r < RS_ROUNDS; r++) {
        CHECK_EQ_INT(rs.round, r);
        t = rs_round(t, r == 3 ? 0.0f : (float)rs.target[r] + off[r]);
    }
    CHECK_EQ_INT(rs.state, RS_DONE);
    CHECK_EQ_INT(rs.grade[0], RS_GRADE_PERFECT);
    CHECK_EQ_INT(rs.grade[1], RS_GRADE_PERFECT);
    CHECK_EQ_INT(rs.grade[2], RS_GRADE_GREAT);
    CHECK_EQ_INT(rs.grade[3], RS_GRADE_MISS);
    CHECK_EQ_INT(rs.grade[4], RS_GRADE_OK);
    CHECK_EQ_INT(rs.peak_of[0], rs.target[0] + 20);
    CHECK_EQ_INT(rs.peak_of[3], 0);
    /* 1000 + (1000 + 200) + (600 + 400) + 0 + 100 */
    CHECK_EQ_INT(rs.score, 3300);
    CHECK_EQ_STR(rs_rank(rs.score), "B");
    CHECK(rs_start(&rs, t));                              /* again */
    CHECK_EQ_INT(rs.score, 0);
}

static void test_rev_sync_locks(void)
{
    rs_init(&rs, 6000, 7);
    rs_speed(&rs, 0, 0.0f);
    CHECK(rs_start(&rs, 0));
    rs_speed(&rs, 500, 3.0f);                             /* rolled */
    CHECK_EQ_INT(rs.state, RS_LOCKED);
    CHECK(rs.note != NULL);
    CHECK(!rs_start(&rs, 600));
    rs_speed(&rs, 1000, 0.0f);
    CHECK_EQ_INT(rs.state, RS_IDLE);                      /* parked again: START anew */
    CHECK(rs_start(&rs, 1000));
    CHECK(rs_tick(&rs, 1000 + RS_SPEED_STALE_MS + 1));    /* speed readings stopped */
    CHECK_EQ_INT(rs.state, RS_LOCKED);

    /* Targets stay 1000 under a low shift point, and never under the minimum span. */
    rs_init(&rs, 4000, 3);
    CHECK_EQ_INT(rs.max_target, 3000);
    rs_init(&rs, 2000, 3);
    CHECK_EQ_INT(rs.max_target, RS_TARGET_MIN + 500);

    CHECK_EQ_INT(rs_grade_for(3000, 3075.0f), RS_GRADE_PERFECT);
    CHECK_EQ_INT(rs_grade_for(3000, 2799.0f), RS_GRADE_GOOD);
    CHECK_EQ_INT(rs_grade_for(3000, 3701.0f), RS_GRADE_MISS);
    CHECK_EQ_STR(rs_grade_name(RS_GRADE_GREAT), "GREAT");
    CHECK_EQ_STR(rs_rank(6000), "S");
    CHECK_EQ_STR(rs_rank(999), "D");
}

static eco_t eco;

/* Drives a speed profile: readings every 500 ms from t, speed(k) for
 * k = 0 .. n-1; returns the time after. */
static uint64_t eco_drive(uint64_t t, int n, float (*kmh)(int), float rpm, float maf)
{
    for (int k = 0; k < n; k++, t += 500) {
        eco_input_t in = { true, true, true, false, false, kmh(k), rpm, maf, 0, 0 };
        eco_step(&eco, t, &in, 0.0f);
    }
    return t;
}

static float eco_stopped(int k) { (void)k; return 0.0f; }
static float eco_gentle_up(int k) { return fminf(k * 1.8f, 80.0f); }       /* 1 m/s^2 */
static float eco_cruise(int k) { (void)k; return 80.0f; }
static float eco_gentle_down(int k) { return fmaxf(80.0f - k * 1.8f, 0.0f); }
static float eco_hard_up(int k) { return fminf(k * 5.4f, 80.0f); }         /* 3 m/s^2 */
static float eco_hard_down(int k) { return fmaxf(80.0f - k * 6.3f, 0.0f); } /* 3.5 m/s^2 */

static void test_eco_fuel(void)
{
    float lph = 0.0f;
    eco_input_t maf = { true, true, true, false, false, 50, 2000, 10.0f, 0, 0 };
    CHECK(eco_fuel_lph(&maf, 0.0f, &lph));
    CHECK(fabsf(lph - 3.287f) < 0.01f);                   /* 10 g/s of air */
    /* Speed-density: a 3.5 L at 700 rpm, 30 kPa, 25 C: about 6.1 g/s of air. */
    eco_input_t sd = { true, true, false, true, true, 0, 700, 0, 30.0f, 25.0f };
    CHECK(eco_fuel_lph(&sd, 3.5f, &lph));
    CHECK(fabsf(lph - 2.00f) < 0.05f);
    CHECK(!eco_fuel_lph(&sd, 0.0f, &lph));                /* no engine size */
    sd.have_iat = false;
    CHECK(eco_fuel_lph(&sd, 3.5f, &lph));                 /* 25 C assumed */
    CHECK(fabsf(lph - 2.00f) < 0.05f);
}

static void test_eco_smooth_trip(void)
{
    eco_reset(&eco);
    CHECK_EQ_INT(eco_score(&eco), 100);
    uint64_t t = eco_drive(1000, 40, eco_stopped, 750, 4.0f);          /* 20 s idle */
    t = eco_drive(t, 50, eco_gentle_up, 2000, 12.0f);
    t = eco_drive(t, 240, eco_cruise, 1900, 15.0f);                  /* 2 minutes */
    t = eco_drive(t, 50, eco_gentle_down, 1000, 3.0f);
    CHECK_EQ_INT(eco_score(&eco), 100);
    CHECK(eco_ranked(&eco));
    CHECK_EQ_STR(eco_rank(eco_score(&eco)), "S");
    CHECK_EQ_INT(eco.harsh_accel, 0);
    CHECK_EQ_INT(eco.harsh_brake, 0);
    CHECK(eco.dist_km > 3.0 && eco.dist_km < 3.4);        /* 2 min at 80 plus the ends */
    float avg = 0.0f, now = 0.0f;
    CHECK(eco_avg_l100(&eco, &avg) && avg > 5.0f && avg < 7.0f);
    CHECK(!eco_now_l100(&eco, &now));                     /* stopped now */
    (void)t;
}

static void test_eco_harsh_and_idle(void)
{
    eco_reset(&eco);
    uint64_t t = eco_drive(1000, 2, eco_stopped, 750, 4.0f);
    t = eco_drive(t, 20, eco_hard_up, 3600, 25.0f);       /* hard, and over 3500 rpm */
    CHECK_EQ_INT(eco.harsh_accel, 1);                     /* one event, not one per reading */
    t = eco_drive(t, 20, eco_cruise, 2000, 15.0f);
    float now = 0.0f;
    CHECK(eco_now_l100(&eco, &now) && now > 6.0f && now < 6.4f);   /* 4.93 L/h at 80 */
    t = eco_drive(t, 20, eco_hard_down, 1500, 2.0f);
    CHECK_EQ_INT(eco.harsh_brake, 1);
    CHECK(eco.tip == ECO_TIP_BRAKE);
    CHECK(eco_score(&eco) < 80);
    CHECK(!eco_ranked(&eco));                             /* too short */

    /* Many readings a second in whole km/h (a fast CAN car) are not jolts:
     * a gentle 1 m/s^2 read every 50 ms still scores 100. */
    eco_reset(&eco);
    for (int k = 0; k <= 440; k++) {
        eco_input_t in = { true, true, true, false, false, floorf(k * 0.05f * 3.6f), 2000, 10, 0, 0 };
        eco_step(&eco, 1000 + (uint64_t)k * 50, &in, 0.0f);
    }
    CHECK_EQ_INT(eco.harsh_accel, 0);
    CHECK_EQ_INT(eco_score(&eco), 100);

    /* Two minutes stopped, engine running: the first one is free. */
    eco_reset(&eco);
    eco_drive(1000, 241, eco_stopped, 750, 4.0f);
    CHECK_EQ_INT(eco_score(&eco), 65);
    CHECK(eco.tip == ECO_TIP_IDLE);
    CHECK(eco.fuel_l > 0.0);

    /* A gap in the readings isn't counted; engine off isn't scored. */
    eco_reset(&eco);
    eco_input_t in = { true, true, true, false, false, 0, 0, 0, 0, 0 };
    eco_step(&eco, 1000, &in, 0.0f);
    eco_step(&eco, 1500, &in, 0.0f);
    CHECK(eco.points_s == 0.0);
    in.rpm = 750;
    eco_step(&eco, 9000, &in, 0.0f);
    CHECK(eco.points_s == 0.0 && eco.idle_s == 0.0);
    CHECK_EQ_STR(eco_rank(89), "A");
    CHECK_EQ_STR(eco_rank(64), "C");
}

static void test_achievements(void)
{
    static char a[1024], b[1024];
    bool is_new = false;
    CHECK(!achieve_unlocked("", ACH_FIRST_SCAN, NULL, 0));
    size_t n = achieve_unlock("", ACH_FIRST_SCAN, "2026-11-18", &is_new, a, sizeof a);
    CHECK(n > 0 && is_new);
    CHECK_EQ_STR(a, "id,date\nfirst_scan,2026-11-18\n");
    CHECK_EQ_INT(achieve_unlock(a, ACH_FIRST_SCAN, "2026-11-19", &is_new, b, sizeof b), 0);
    CHECK(!is_new);                                       /* once only */
    achieve_unlock(a, ACH_REDLINE, "2026-11-19", &is_new, b, sizeof b);
    CHECK(is_new);
    char date[16];
    CHECK(achieve_unlocked(b, ACH_REDLINE, date, sizeof date));
    CHECK_EQ_STR(date, "2026-11-19");
    CHECK(!achieve_unlocked(b, ACH_REV_S, NULL, 0));
    CHECK_EQ_INT(achieve_count(b), 2);
    CHECK_EQ_INT(achieve_unlock(b, ACH_REV_S, "x", &is_new, a, 10), 0);   /* too small */
    achieve_unlock("first_scan,2026-11-18", ACH_STOPWATCH, "d", &is_new, a, sizeof a);
    CHECK_EQ_STR(a, "id,date\nfirst_scan,2026-11-18\nstopwatch,d\n");   /* header, line break added */
    for (int i = 0; i < ACH_COUNT; i++) {
        CHECK(achieve_info((achieve_id_t)i) != NULL && achieve_info((achieve_id_t)i)->name != NULL);
    }
    CHECK(achieve_info(ACH_COUNT) == NULL);

    /* Scans. */
    static obd_diag_t d;
    memset(&d, 0, sizeof d);
    CHECK(!achieve_scan_ok(&d));
    d.result = OBD_LINK_OK;
    d.stopped_at = OBD_DIAG_STEP_DONE;
    d.have_readiness = true;
    d.readiness.valid = true;
    d.have_stored = d.have_pending = true;
    d.readiness.monitor[0].name = "Misfire";
    d.readiness.monitor[0].state = OBD_MONITOR_COMPLETE;
    d.readiness.monitor[4].name = "Catalyst";
    d.readiness.monitor[4].state = OBD_MONITOR_COMPLETE;
    CHECK(achieve_scan_ok(&d) && achieve_scan_clean(&d));
    d.readiness.monitor[4].state = OBD_MONITOR_INCOMPLETE;
    CHECK(!achieve_scan_clean(&d));                       /* a monitor not ready */
    d.readiness.monitor[4].state = OBD_MONITOR_COMPLETE;
    d.pending.count = 1;
    CHECK(!achieve_scan_clean(&d));                       /* a pending code */
    d.pending.count = 0;
    d.readiness.mil_on = true;
    CHECK(!achieve_scan_clean(&d));
}

void suite_track(void)
{
    RUN_TEST(test_achievements);
    RUN_TEST(test_eco_fuel);
    RUN_TEST(test_eco_smooth_trip);
    RUN_TEST(test_eco_harsh_and_idle);
    RUN_TEST(test_rev_sync_game);
    RUN_TEST(test_rev_sync_locks);
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
