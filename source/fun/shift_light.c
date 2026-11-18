#include "shift_light.h"

#include <math.h>
#include <string.h>

void shift_init(shift_light_t *sl, int shift_rpm)
{
    memset(sl, 0, sizeof *sl);
    sl->shift_rpm = shift_step(shift_rpm, 0);
}

void shift_reading(shift_light_t *sl, uint64_t t_ms, float rpm)
{
    if (isnan(rpm) || rpm < 0.0f || (sl->have && t_ms <= sl->t1)) {
        return;   /* NaN, negative or out of order */
    }
    sl->have_two = sl->have;
    sl->t0 = sl->t1;
    sl->rpm0 = sl->rpm1;
    sl->t1 = t_ms;
    sl->rpm1 = rpm;
    sl->have = true;
}

float shift_rpm_now(const shift_light_t *sl, uint64_t now)
{
    if (!sl->have) {
        return 0.0f;
    }
    if (!sl->have_two || now <= sl->t1 || sl->t1 <= sl->t0) {
        return sl->rpm1;
    }
    uint64_t ahead = now - sl->t1;
    if (ahead > SHIFT_PREDICT_MS) {
        ahead = SHIFT_PREDICT_MS;
    }
    float slope = (sl->rpm1 - sl->rpm0) / (float)(sl->t1 - sl->t0);   /* rpm per ms */
    float est = sl->rpm1 + slope * (float)ahead;
    return est < 0.0f ? 0.0f : est;
}

int shift_update(shift_light_t *sl, float rpm)
{
    if (rpm >= (float)sl->shift_rpm) {
        sl->flashing = true;
    } else if (rpm < (float)(sl->shift_rpm - SHIFT_HYST_RPM)) {
        sl->flashing = false;
    }
    if (sl->flashing) {
        return SHIFT_LEDS;
    }
    float start = (float)sl->shift_rpm * SHIFT_START_PCT / 100.0f;
    if (rpm < start) {
        return 0;
    }
    /* The first LED at start, the last just under the shift point. */
    int lit = 1 + (int)((rpm - start) / ((float)sl->shift_rpm - start) * (SHIFT_LEDS - 1));
    return lit > SHIFT_LEDS ? SHIFT_LEDS : lit;
}

bool shift_flash_on(uint64_t now)
{
    return (now / SHIFT_FLASH_MS) % 2 == 0;
}

shift_color_t shift_led_color(int i)
{
    if (i >= SHIFT_LEDS - 2) {
        return SHIFT_RED;
    }
    return i >= SHIFT_LEDS / 2 ? SHIFT_AMBER : SHIFT_GREEN;
}

int shift_step(int shift_rpm, int delta)
{
    int r = shift_rpm + delta;
    r = (r + 50) / 100 * 100;
    return r < SHIFT_RPM_MIN ? SHIFT_RPM_MIN : r > SHIFT_RPM_MAX ? SHIFT_RPM_MAX : r;
}
