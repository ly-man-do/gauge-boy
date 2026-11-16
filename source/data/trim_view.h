#ifndef GAUGEBOY_TRIM_VIEW_H
#define GAUGEBOY_TRIM_VIEW_H

/*
 * TRIM screen logic (brief section 5d): fuel trims per bank as center-zero
 * bars, color zones and plain-language hints (hints, not diagnoses).
 * Positive trim = the computer adds fuel (the engine runs lean without it);
 * negative = it takes fuel away (rich). Platform-free.
 */

#include <stdbool.h>

#include "obd/obd_live.h"

#define TRIM_BAR_WIDTH 25      /* odd: the middle is 0 % */
#define TRIM_BAR_RANGE 25.0f   /* -25 % .. +25 % */
#define TRIM_HINTS_MAX 4

typedef enum {
    TRIM_ZONE_GREEN = 0,   /* within +-10 % */
    TRIM_ZONE_AMBER,       /* 10-20 % */
    TRIM_ZONE_RED,         /* beyond 20 % */
} trim_zone_t;

trim_zone_t trim_zone(float percent);

/*
 * A center-zero bar: '|' in the middle, '#' from the middle toward the
 * value, '.' elsewhere, one cell = 2 %; beyond the range the end cell is
 * '<' or '>'. out needs width + 1 chars.
 */
void trim_bar(float percent, int width, char *out);

typedef struct {
    bool have_short, have_long;
    float short_term, long_term;   /* % */
} trim_bank_t;

typedef struct {
    int banks;                     /* 1 or 2 (0 = no trims supported) */
    trim_bank_t bank[2];
    bool have_rpm, have_speed;
    float rpm, speed_kmh;
} trim_state_t;

/* The banks and engine state from the poller (PIDs 06-09, 0C, 0D). */
void trim_state_from_live(const obd_live_t *lv, trim_state_t *out);

/* Short + long when both are known (else whichever is), and whether any is. */
bool trim_total(const trim_bank_t *b, float *total);

/* Engine running at idle: RPM 400-1100 and not moving. */
bool trim_at_idle(const trim_state_t *s);

/* Up to max plain-language hints for the current trims. Returns the count. */
int trim_hints(const trim_state_t *s, const char **out, int max);

#endif
