#ifndef GAUGEBOY_REV_SYNC_H
#define GAUGEBOY_REV_SYNC_H

/*
 * Rev Sync (brief section 5e): a parked-only mini game. Each round shows a
 * target RPM; at GO the player blips the throttle, and the blip's peak RPM
 * is scored by how close it came, 2000s arcade style:
 *
 *   PERFECT  within  75 rpm   1000 points
 *   GREAT    within 200 rpm    600
 *   GOOD     within 400 rpm    300
 *   OK       within 700 rpm    100
 *   MISS     further, or no blip in time
 *
 * GREAT or better in a row builds a combo: +200 points for each round of
 * it after the first. RS_ROUNDS rounds make a game, ranked S to D.
 *
 * Safety: the game only runs while the car's speed reads 0. It is locked
 * until a 0 has been read, locks again (ending the game) on any speed
 * above 0, and when speed readings stop for RS_SPEED_STALE_MS.
 *
 * Platform-free: feed it RPM and speed readings and the clock.
 */

#include <stdbool.h>
#include <stdint.h>

#define RS_ROUNDS          5
#define RS_READY_MS        2000u   /* the target shown before GO */
#define RS_BLIP_MS         5000u   /* time to blip after GO */
#define RS_JUDGE_MS        2000u   /* the grade shown */
#define RS_RISE_RPM        400.0f  /* a blip: this far above idle ... */
#define RS_FALL_RPM        400.0f  /* ... and done once this far back down from its peak */
#define RS_SPEED_STALE_MS  3000u
#define RS_TARGET_MIN      2000
#define RS_TARGET_MAX      4500

typedef enum {
    RS_GRADE_MISS = 0, RS_GRADE_OK, RS_GRADE_GOOD, RS_GRADE_GREAT, RS_GRADE_PERFECT
} rs_grade_t;

typedef enum {
    RS_LOCKED = 0,   /* speed not read as 0 (yet), or moving */
    RS_IDLE,         /* parked: START begins a game */
    RS_READY,        /* the target shown; idle measured */
    RS_GO,           /* blip now */
    RS_JUDGE,        /* the round's grade */
    RS_DONE,         /* the game's score and rank */
} rs_state_t;

typedef struct {
    rs_state_t state;
    uint64_t state_ms;
    const char *note;        /* why it locked or stopped, or NULL */

    bool have_speed;
    uint64_t speed_ms;       /* the last speed reading */
    bool stopped;            /* ... was 0 */

    uint32_t seed;
    int max_target;
    int round;               /* 0 .. RS_ROUNDS - 1 */
    int target[RS_ROUNDS];
    float idle_rpm;          /* lowest reading while READY */
    bool have_idle;
    float peak;              /* highest reading since GO */
    bool rose;               /* the blip went RS_RISE_RPM above idle */
    float rpm;               /* the latest reading */
    bool have_rpm;

    rs_grade_t grade[RS_ROUNDS];
    int peak_of[RS_ROUNDS];  /* each round's blip, rpm (0 = none) */
    int points;              /* the last round's points */
    int score;
    int combo;               /* GREAT or better in a row */
} rev_sync_t;

/* shift_rpm caps the targets (1000 under it) so they stay well clear of
 * the shift point; seed picks them. */
void rs_init(rev_sync_t *rs, int shift_rpm, uint32_t seed);

/* A speed reading (km/h) at t_ms: unlocks at 0, locks above it. */
void rs_speed(rev_sync_t *rs, uint64_t t_ms, float kmh);

/* An RPM reading at t_ms. */
void rs_rpm(rev_sync_t *rs, uint64_t t_ms, float rpm);

/* The clock: rounds move on, stale speed locks. True if the state changed. */
bool rs_tick(rev_sync_t *rs, uint64_t now_ms);

/* START (IDLE or DONE only): a new game. False if locked. */
bool rs_start(rev_sync_t *rs, uint64_t now_ms);

rs_grade_t rs_grade_for(int target, float peak);
int rs_grade_points(rs_grade_t g);
const char *rs_grade_name(rs_grade_t g);

/* "S" ... "D" for a final score. */
const char *rs_rank(int score);

#endif
