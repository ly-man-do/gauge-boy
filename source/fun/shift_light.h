#ifndef GAUGEBOY_SHIFT_LIGHT_H
#define GAUGEBOY_SHIFT_LIGHT_H

/*
 * Shift light (brief section 5e): a row of LEDs that fills as RPM rises
 * and flashes at the shift point, like an aftermarket shift light.
 *
 * The LEDs start lighting at SHIFT_START_PCT of the shift point: green,
 * then amber, then red for the last ones. At the shift point every LED
 * flashes, until RPM falls SHIFT_HYST_RPM below it (so it doesn't flicker
 * on and off around the point).
 *
 * RPM comes a few times a second on older buses, so between readings the
 * light runs on an estimate: the last reading plus its rate of change,
 * for at most SHIFT_PREDICT_MS, so the light rises smoothly and reaches
 * the shift point a little sooner instead of up to a reading late.
 *
 * Platform-free; the app draws it.
 */

#include <stdbool.h>
#include <stdint.h>

#define SHIFT_LEDS          10
#define SHIFT_START_PCT     60      /* first LED at 60 % of the shift point */
#define SHIFT_HYST_RPM      200
#define SHIFT_FLASH_MS      125     /* on 125 ms, off 125 ms: 4 flashes a second */
#define SHIFT_PREDICT_MS    300u    /* estimate at most this far past a reading */
#define SHIFT_RPM_MIN       2000
#define SHIFT_RPM_MAX       9900
#define SHIFT_RPM_DEFAULT   6000

typedef enum { SHIFT_GREEN = 0, SHIFT_AMBER, SHIFT_RED } shift_color_t;

typedef struct {
    int shift_rpm;
    bool flashing;           /* at the shift point (with the hysteresis) */
    bool have;               /* a reading has come */
    uint64_t t0, t1;         /* the last two readings */
    float rpm0, rpm1;
    bool have_two;
} shift_light_t;

void shift_init(shift_light_t *sl, int shift_rpm);

/* A new RPM reading read at t_ms. */
void shift_reading(shift_light_t *sl, uint64_t t_ms, float rpm);

/* RPM estimated at now (the last reading, carried forward along its
 * trend for at most SHIFT_PREDICT_MS; never below 0). 0 without readings. */
float shift_rpm_now(const shift_light_t *sl, uint64_t now_ms);

/* Updates the flashing state for rpm; returns how many LEDs are lit
 * (0..SHIFT_LEDS; SHIFT_LEDS while flashing). */
int shift_update(shift_light_t *sl, float rpm);

/* While flashing: whether the LEDs are on at now. */
bool shift_flash_on(uint64_t now_ms);

/* LED i's color (0 = leftmost). */
shift_color_t shift_led_color(int i);

/* The shift point stepped by delta, kept in SHIFT_RPM_MIN..MAX, on a
 * multiple of 100. */
int shift_step(int shift_rpm, int delta);

#endif
