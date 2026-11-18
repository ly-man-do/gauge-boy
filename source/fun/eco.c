#include "eco.h"

#include <math.h>
#include <string.h>

void eco_reset(eco_t *eco)
{
    memset(eco, 0, sizeof *eco);
}

bool eco_fuel_lph(const eco_input_t *in, float engine_l, float *lph)
{
    float air_gs;
    if (in->have_maf) {
        air_gs = in->maf_gs;
    } else if (in->have_map && in->have_rpm && engine_l > 0.0f) {
        float t_k = (in->have_iat ? in->iat_c : ECO_IAT_DEFAULT_C) + 273.15f;
        /* Air per second: pressure x volume swept per second x VE / (R T);
         * a four-stroke fills its displacement every two turns. */
        float swept_m3_s = engine_l / 1000.0f * in->rpm / 120.0f;
        air_gs = in->map_kpa * 1000.0f * swept_m3_s * ECO_VE / (287.05f * t_k) * 1000.0f;
    } else {
        return false;
    }
    if (air_gs < 0.0f || isnan(air_gs)) {
        return false;
    }
    *lph = air_gs / ECO_AFR * 3600.0f / ECO_FUEL_G_PER_L;
    return true;
}

static void tip(eco_t *eco, eco_tip_t t, uint64_t at)
{
    eco->tip = t;
    eco->tip_ms = at;
}

void eco_step(eco_t *eco, uint64_t t, const eco_input_t *in, float engine_l)
{
    if (!in->have_speed || isnan(in->kmh)) {
        return;
    }
    float lph = 0.0f;
    eco->have_lph = eco_fuel_lph(in, engine_l, &lph);
    eco->lph = lph;
    eco->kmh = in->kmh;
    if (!eco->started || t <= eco->last_ms || t - eco->last_ms > ECO_GAP_MS) {
        eco->started = true;   /* the first reading, or a gap: start from here */
        eco->last_ms = t;
        eco->last_kmh = in->kmh;
        eco->acc_ms = t;
        eco->acc_kmh = in->kmh;
        eco->accel = 0.0f;
        return;
    }
    double dt = (double)(t - eco->last_ms) / 1000.0;
    float v0 = eco->last_kmh, v1 = in->kmh;
    eco->last_ms = t;
    eco->last_kmh = v1;

    if (eco->have_lph) {
        eco->fuel_l += lph * dt / 3600.0;
    }
    eco->dist_km += (v0 + v1) / 2.0 * dt / 3600.0;

    double pts;
    bool moving = v0 >= 1.0f || v1 >= 1.0f;
    if (moving) {
        eco->moving_s += dt;
        eco->stopped_s = 0.0;
        if (t - eco->acc_ms >= ECO_ACCEL_MS) {
            eco->accel = (v1 - eco->acc_kmh) / 3.6f / ((float)(t - eco->acc_ms) / 1000.0f);
            eco->acc_ms = t;
            eco->acc_kmh = v1;
        }
        double a = eco->accel;   /* m/s^2 */
        pts = 100.0;
        if (a > 2.5) {
            pts = 20.0;
        } else if (a > 1.5) {
            pts = 60.0;
        } else if (a < -3.0) {
            pts = 20.0;
        } else if (a < -2.0) {
            pts = 60.0;
        }
        bool hard_accel = a > 1.5, hard_brake = a < -2.0;
        if (hard_accel && !eco->in_accel) {
            eco->harsh_accel++;
            tip(eco, ECO_TIP_ACCEL, t);
        }
        if (hard_brake && !eco->in_brake) {
            eco->harsh_brake++;
            tip(eco, ECO_TIP_BRAKE, t);
        }
        eco->in_accel = hard_accel;
        eco->in_brake = hard_brake;
        if (in->have_rpm && in->rpm > 2800.0f) {
            pts = fmin(pts, in->rpm > 3500.0f ? 40.0 : 70.0);
            if (eco->tip != ECO_TIP_RPM || t - eco->tip_ms > 10000) {
                tip(eco, ECO_TIP_RPM, t);
            }
        }
    } else {
        eco->in_accel = eco->in_brake = false;
        eco->accel = 0.0f;
        eco->acc_ms = t;
        eco->acc_kmh = v1;
        bool running = !in->have_rpm || in->rpm > 300.0f;
        if (!running) {
            return;   /* engine off: nothing to score */
        }
        eco->idle_s += dt;
        eco->stopped_s += dt;
        if (eco->stopped_s <= ECO_IDLE_FREE_S) {
            pts = 100.0;
        } else {
            pts = ECO_IDLE_POINTS;
            if (eco->tip != ECO_TIP_IDLE) {
                tip(eco, ECO_TIP_IDLE, t);
            }
        }
    }
    eco->points_sum += pts * dt;
    eco->points_s += dt;
}

int eco_score(const eco_t *eco)
{
    if (eco->points_s <= 0.0) {
        return 100;
    }
    return (int)lround(eco->points_sum / eco->points_s);
}

bool eco_ranked(const eco_t *eco)
{
    return eco->moving_s >= ECO_RANK_MOVING_S && eco->dist_km >= ECO_RANK_KM;
}

const char *eco_rank(int score)
{
    return score >= 90 ? "S" : score >= 80 ? "A" : score >= 65 ? "B" : "C";
}

bool eco_avg_l100(const eco_t *eco, float *l100)
{
    if (eco->dist_km < 0.1 || eco->fuel_l <= 0.0) {
        return false;
    }
    *l100 = (float)(eco->fuel_l / eco->dist_km * 100.0);
    return true;
}

bool eco_now_l100(const eco_t *eco, float *l100)
{
    if (!eco->have_lph || eco->kmh < 5.0f) {
        return false;
    }
    *l100 = eco->lph / eco->kmh * 100.0f;
    return true;
}

const char *eco_tip_text(eco_tip_t t)
{
    switch (t) {
    case ECO_TIP_ACCEL: return "Ease into the throttle.";
    case ECO_TIP_BRAKE: return "Brake earlier and softer.";
    case ECO_TIP_RPM:   return "Shift up sooner: keep under 2800 rpm.";
    case ECO_TIP_IDLE:  return "Long idle: switch off when parked.";
    default:            return "";
    }
}
