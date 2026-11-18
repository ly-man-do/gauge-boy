#include "rev_sync.h"

#include <math.h>
#include <string.h>

static void set_state(rev_sync_t *rs, rs_state_t st, uint64_t at)
{
    rs->state = st;
    rs->state_ms = at;
}

static uint32_t next_rand(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;   /* LCG: plenty for picking targets */
    return *s >> 8;
}

void rs_init(rev_sync_t *rs, int shift_rpm, uint32_t seed)
{
    memset(rs, 0, sizeof *rs);
    rs->seed = seed != 0 ? seed : 1u;
    int cap = shift_rpm - 1000;
    rs->max_target = cap < RS_TARGET_MAX ? cap : RS_TARGET_MAX;
    if (rs->max_target < RS_TARGET_MIN + 500) {
        rs->max_target = RS_TARGET_MIN + 500;
    }
    set_state(rs, RS_LOCKED, 0);
}

/* Ends any game and locks. */
static void lock(rev_sync_t *rs, const char *why, uint64_t at)
{
    if (rs->state != RS_LOCKED) {
        rs->note = why;
        set_state(rs, RS_LOCKED, at);
    }
}

void rs_speed(rev_sync_t *rs, uint64_t t, float kmh)
{
    rs->have_speed = true;
    rs->speed_ms = t;
    rs->stopped = kmh < 0.5f;
    if (!rs->stopped) {
        lock(rs, NULL, t);
        rs->note = "The car is moving: Rev Sync is for parked only.";
    } else if (rs->state == RS_LOCKED) {
        set_state(rs, RS_IDLE, t);
    }
}

bool rs_start(rev_sync_t *rs, uint64_t now)
{
    if (rs->state != RS_IDLE && rs->state != RS_DONE) {
        return false;
    }
    for (int i = 0; i < RS_ROUNDS; i++) {
        int span = (rs->max_target - RS_TARGET_MIN) / 100;
        rs->target[i] = RS_TARGET_MIN + (int)(next_rand(&rs->seed) % (uint32_t)(span + 1)) * 100;
        rs->grade[i] = RS_GRADE_MISS;
        rs->peak_of[i] = 0;
    }
    rs->round = 0;
    rs->score = 0;
    rs->combo = 0;
    rs->points = 0;
    rs->note = NULL;
    rs->have_idle = false;
    set_state(rs, RS_READY, now);
    return true;
}

rs_grade_t rs_grade_for(int target, float peak)
{
    float d = fabsf(peak - (float)target);
    return d <= 75.0f ? RS_GRADE_PERFECT : d <= 200.0f ? RS_GRADE_GREAT
         : d <= 400.0f ? RS_GRADE_GOOD : d <= 700.0f ? RS_GRADE_OK : RS_GRADE_MISS;
}

int rs_grade_points(rs_grade_t g)
{
    static const int pts[] = { 0, 100, 300, 600, 1000 };
    return pts[g];
}

const char *rs_grade_name(rs_grade_t g)
{
    static const char *const names[] = { "MISS", "OK", "GOOD", "GREAT", "PERFECT" };
    return names[g];
}

const char *rs_rank(int score)
{
    return score >= 6000 ? "S" : score >= 4000 ? "A" : score >= 2500 ? "B"
         : score >= 1000 ? "C" : "D";
}

/* The round's blip is over (or the time ran out): grade it. */
static void judge(rev_sync_t *rs, uint64_t at)
{
    int r = rs->round;
    rs_grade_t g = rs->rose ? rs_grade_for(rs->target[r], rs->peak) : RS_GRADE_MISS;
    rs->grade[r] = g;
    rs->peak_of[r] = rs->rose ? (int)lroundf(rs->peak) : 0;
    rs->points = rs_grade_points(g);
    if (g >= RS_GRADE_GREAT) {
        if (rs->combo > 0) {
            rs->points += 200 * rs->combo;
        }
        rs->combo++;
    } else {
        rs->combo = 0;
    }
    rs->score += rs->points;
    set_state(rs, RS_JUDGE, at);
}

void rs_rpm(rev_sync_t *rs, uint64_t t, float rpm)
{
    if (isnan(rpm) || rpm < 0.0f) {
        return;
    }
    rs->rpm = rpm;
    rs->have_rpm = true;
    if (rs->state == RS_READY) {
        if (!rs->have_idle || rpm < rs->idle_rpm) {
            rs->idle_rpm = rpm;   /* revving early doesn't raise the baseline */
            rs->have_idle = true;
        }
    } else if (rs->state == RS_GO) {
        if (!rs->have_idle) {
            rs->idle_rpm = rpm;
            rs->have_idle = true;
        }
        if (rpm > rs->peak) {
            rs->peak = rpm;
        }
        if (rs->peak >= rs->idle_rpm + RS_RISE_RPM) {
            rs->rose = true;
        }
        if (rs->rose && rpm <= rs->peak - RS_FALL_RPM) {
            judge(rs, t);   /* the blip is over */
        }
    }
}

bool rs_tick(rev_sync_t *rs, uint64_t now)
{
    rs_state_t before = rs->state;
    if (rs->state != RS_LOCKED &&
        (!rs->have_speed || now - rs->speed_ms > RS_SPEED_STALE_MS)) {
        lock(rs, "No speed reading: Rev Sync needs the car's speed to be 0.", now);
        return true;
    }
    uint64_t gone = now - rs->state_ms;
    switch (rs->state) {
    case RS_READY:
        if (gone >= RS_READY_MS) {
            rs->peak = 0.0f;
            rs->rose = false;
            set_state(rs, RS_GO, now);
        }
        break;
    case RS_GO:
        if (gone >= RS_BLIP_MS) {
            judge(rs, now);
        }
        break;
    case RS_JUDGE:
        if (gone >= RS_JUDGE_MS) {
            if (++rs->round >= RS_ROUNDS) {
                rs->round = RS_ROUNDS - 1;
                set_state(rs, RS_DONE, now);
            } else {
                rs->have_idle = false;
                set_state(rs, RS_READY, now);
            }
        }
        break;
    default:
        break;
    }
    return rs->state != before;
}
