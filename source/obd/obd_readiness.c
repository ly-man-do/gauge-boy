#include "obd_readiness.h"

#include <string.h>

/* Names of byte C/D bits 0-7 (SAE J1979). NULL = reserved. */
static const char *const spark_names[8] = {
    "Catalyst", "Heated catalyst", "Evaporative system", "Secondary air system",
    "A/C refrigerant", "Oxygen sensor", "Oxygen sensor heater", "EGR / VVT system",
};
static const char *const compression_names[8] = {
    "NMHC catalyst", "NOx / SCR aftertreatment", NULL, "Boost pressure",
    NULL, "Exhaust gas sensor", "Particulate filter", "EGR / VVT system",
};
static const char *const continuous_names[3] = {
    "Misfire", "Fuel system", "Components",
};

static obd_monitor_state_t state_of(bool supported, bool incomplete)
{
    if (!supported) {
        return OBD_MONITOR_NOT_SUPPORTED;
    }
    return incomplete ? OBD_MONITOR_INCOMPLETE : OBD_MONITOR_COMPLETE;
}

void obd_readiness_from_bytes(const uint8_t abcd[4], obd_readiness_t *out)
{
    uint8_t a = abcd[0], b = abcd[1], c = abcd[2], d = abcd[3];
    memset(out, 0, sizeof *out);
    out->valid = true;
    out->ecus = 1;
    out->mil_on = (a & 0x80) != 0;
    out->dtc_count = a & 0x7F;
    out->compression = (b & 0x08) != 0;

    for (int i = 0; i < 3; i++) {
        obd_monitor_t *m = &out->monitor[i];
        m->name = continuous_names[i];
        m->continuous = true;
        m->state = state_of((b >> i) & 1, (b >> (i + 4)) & 1);
    }
    const char *const *names = out->compression ? compression_names : spark_names;
    for (int i = 0; i < 8; i++) {
        obd_monitor_t *m = &out->monitor[3 + i];
        m->name = names[i];
        m->continuous = false;
        m->state = names[i] == NULL ? OBD_MONITOR_NOT_SUPPORTED
                                    : state_of((c >> i) & 1, (d >> i) & 1);
    }
}

/* Adds one ECU's status into the combined one. */
static void merge(obd_readiness_t *all, const obd_readiness_t *one)
{
    if (!all->valid) {
        *all = *one;
        return;
    }
    all->ecus++;
    all->mil_on = all->mil_on || one->mil_on;
    all->dtc_count += one->dtc_count;
    /* Monitor names follow the first ECU (the engine answers first on
     * nearly every car); a second ECU only adds support / incomplete. */
    for (int i = 0; i < OBD_MONITOR_COUNT; i++) {
        obd_monitor_t *m = &all->monitor[i];
        obd_monitor_state_t s = one->monitor[i].state;
        if (m->name == NULL || s == OBD_MONITOR_NOT_SUPPORTED) {
            continue;
        }
        if (m->state == OBD_MONITOR_NOT_SUPPORTED || s == OBD_MONITOR_INCOMPLETE) {
            m->state = s;
        }
    }
}

bool obd_decode_readiness(const obd_reply_t *reply, obd_readiness_t *out)
{
    memset(out, 0, sizeof *out);
    if (reply == NULL) {
        return false;
    }
    for (int e = 0; e < reply->ecu_count; e++) {
        const obd_ecu_t *ecu = &reply->ecu[e];
        for (int m = 0; m < ecu->msg_count; m++) {
            const obd_msg_t *msg = &ecu->msg[m];
            if (msg->len >= 6 && msg->data[0] == 0x41 && msg->data[1] == 0x01) {
                obd_readiness_t one;
                obd_readiness_from_bytes(msg->data + 2, &one);
                merge(out, &one);
                break; /* one status per ECU */
            }
        }
    }
    return out->valid;
}

int obd_readiness_ready_count(const obd_readiness_t *st)
{
    int n = 0;
    for (int i = 0; i < OBD_MONITOR_COUNT; i++) {
        n += st->monitor[i].state == OBD_MONITOR_COMPLETE;
    }
    return n;
}

int obd_readiness_not_ready_count(const obd_readiness_t *st)
{
    int n = 0;
    for (int i = 0; i < OBD_MONITOR_COUNT; i++) {
        n += st->monitor[i].state == OBD_MONITOR_INCOMPLETE;
    }
    return n;
}

const char *obd_monitor_state_str(obd_monitor_state_t state)
{
    switch (state) {
    case OBD_MONITOR_NOT_SUPPORTED: return "Not supported";
    case OBD_MONITOR_COMPLETE:      return "Ready";
    case OBD_MONITOR_INCOMPLETE:    return "Not ready";
    }
    return "?";
}
