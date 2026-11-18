#include "gmeter.h"

#include <math.h>
#include <string.h>

static gm_vec_t vsub(gm_vec_t a, gm_vec_t b)
{
    gm_vec_t r = { a.x - b.x, a.y - b.y, a.z - b.z };
    return r;
}

static gm_vec_t vscale(gm_vec_t a, float k)
{
    gm_vec_t r = { a.x * k, a.y * k, a.z * k };
    return r;
}

static float vdot(gm_vec_t a, gm_vec_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static gm_vec_t vcross(gm_vec_t a, gm_vec_t b)
{
    gm_vec_t r = { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    return r;
}

static float vlen(gm_vec_t a)
{
    return sqrtf(vdot(a, a));
}

void gm_init(gmeter_t *gm)
{
    memset(gm, 0, sizeof *gm);
    gm->forward_sign = 1;
}

void gm_cal_start(gmeter_t *gm)
{
    gm->cal = GM_CAL_RUNNING;
    gm->cal_n = 0;
}

void gm_reset_peaks(gmeter_t *gm)
{
    gm->peak_left = gm->peak_right = gm->peak_accel = gm->peak_brake = gm->peak_total = 0.0f;
}

gm_cal_t gm_cal_add(gmeter_t *gm, gm_vec_t raw)
{
    if (gm->cal != GM_CAL_RUNNING) {
        return gm->cal;
    }
    gm->cal_samples[gm->cal_n++] = raw;
    if (gm->cal_n < GM_CAL_SAMPLES) {
        return GM_CAL_RUNNING;
    }
    gm_vec_t sum = { 0, 0, 0 };
    for (int i = 0; i < GM_CAL_SAMPLES; i++) {
        sum.x += gm->cal_samples[i].x;
        sum.y += gm->cal_samples[i].y;
        sum.z += gm->cal_samples[i].z;
    }
    gm_vec_t mean = vscale(sum, 1.0f / GM_CAL_SAMPLES);
    float g = vlen(mean);
    if (g < 20.0f) {
        gm->cal = GM_CAL_NO_SENSOR;
        return gm->cal;
    }
    for (int i = 0; i < GM_CAL_SAMPLES; i++) {
        if (vlen(vsub(gm->cal_samples[i], mean)) > GM_CAL_STILL * g) {
            gm->cal = GM_CAL_MOVED;
            return gm->cal;
        }
    }
    gm_vec_t up = vscale(mean, 1.0f / g);
    if (gm->forward_learned && vdot(up, gm->up) < 0.95f) {
        gm->forward_learned = false;   /* mounted differently: learn again */
        gm->forward_sign = 1;
    }
    /* The side axis: the 3DS's left-right (X) axis across "up"; if X points
     * almost straight up (the 3DS on its side), its Y axis instead. */
    gm_vec_t ax = { 1, 0, 0 };
    gm_vec_t side = vsub(ax, vscale(up, vdot(ax, up)));
    if (vlen(side) < 0.3f) {
        gm_vec_t ay = { 0, 1, 0 };
        side = vsub(ay, vscale(up, vdot(ay, up)));
    }
    side = vscale(side, 1.0f / vlen(side));
    gm->g0 = mean;
    gm->scale = g;
    gm->up = up;
    gm->fwd0 = vcross(up, side);   /* up x right = forward (right-handed) */
    gm->have = false;
    gm->lat = gm->lon = 0.0f;
    gm->learning = false;
    gm_reset_peaks(gm);
    gm->cal = GM_CAL_OK;
    return gm->cal;
}

void gm_sample(gmeter_t *gm, gm_vec_t raw, float dt_ms)
{
    if (gm->cal != GM_CAL_OK) {
        return;
    }
    gm_vec_t a = vscale(vsub(raw, gm->g0), 1.0f / gm->scale);   /* the car's acceleration, g */
    float along = vdot(a, gm->fwd0);
    gm_vec_t fwd = vscale(gm->fwd0, (float)gm->forward_sign);
    gm_vec_t right = vcross(fwd, gm->up);
    float lon = along * (float)gm->forward_sign;
    float lat = vdot(a, right);
    if (!gm->have) {
        gm->lat = lat;
        gm->lon = lon;
        gm->have = true;
    } else {
        float k = dt_ms > 0.0f ? dt_ms / (GM_TAU_MS + dt_ms) : 1.0f;
        gm->lat += k * (lat - gm->lat);
        gm->lon += k * (lon - gm->lon);
    }
    if (gm->learning) {
        gm->learn_sum += along;
        gm->learn_n++;
    }
    gm->peak_right = fmaxf(gm->peak_right, gm->lat);
    gm->peak_left = fmaxf(gm->peak_left, -gm->lat);
    gm->peak_accel = fmaxf(gm->peak_accel, gm->lon);
    gm->peak_brake = fmaxf(gm->peak_brake, -gm->lon);
    gm->peak_total = fmaxf(gm->peak_total, gm_total(gm));
}

void gm_speed(gmeter_t *gm, uint64_t t_ms, float kmh)
{
    bool was_still = gm->have_speed && gm->last_kmh < 1.0f;
    gm->have_speed = true;
    gm->last_kmh = kmh;
    if (gm->cal != GM_CAL_OK || gm->forward_learned) {
        gm->learning = false;
        return;
    }
    if (!gm->learning) {
        if (was_still && kmh >= 1.0f) {   /* moving off: watch this launch */
            gm->learning = true;
            gm->learn_t0 = t_ms;
            gm->learn_sum = 0.0;
            gm->learn_n = 0;
        }
        return;
    }
    if (kmh < 1.0f || t_ms - gm->learn_t0 > GM_LEARN_MS) {
        gm->learning = false;   /* stopped again, or too gentle: wait for the next */
        return;
    }
    if (kmh >= GM_LEARN_KMH && gm->learn_n > 0) {
        float avg = (float)(gm->learn_sum / gm->learn_n);
        gm->learning = false;
        if (fabsf(avg) >= GM_LEARN_MIN_G) {
            int sign = avg > 0.0f ? 1 : -1;
            if (sign != gm->forward_sign) {
                gm->forward_sign = sign;
                gm_reset_peaks(gm);   /* the old ones were mirrored */
                gm->have = false;
            }
            gm->forward_learned = true;
        }
    }
}

float gm_total(const gmeter_t *gm)
{
    return sqrtf(gm->lat * gm->lat + gm->lon * gm->lon);
}
