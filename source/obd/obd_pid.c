#include "obd_pid.h"

#include <stdio.h>

/* How the data bytes become a value (SAE J1979). */
typedef enum {
    F_PCT255,    /* A * 100 / 255 */
    F_TEMP,      /* A - 40 */
    F_TRIM,      /* (A - 128) * 100 / 128 */
    F_RAW,       /* A */
    F_RPM,       /* (256A + B) / 4 */
    F_TIMING,    /* A / 2 - 64 */
    F_MAF,       /* (256A + B) / 100 */
    F_WORD,      /* 256A + B */
    F_MILLI,     /* (256A + B) / 1000 */
} formula_t;

typedef struct {
    obd_pid_info_t info;
    formula_t formula;
} pid_entry_t;

static const pid_entry_t table[] = {
    { { 0x04, 1, "Calculated engine load", "Load", OBD_UNIT_PERCENT, 0.0f, 100.0f }, F_PCT255 },
    { { 0x05, 1, "Coolant temperature", "Coolant", OBD_UNIT_CELSIUS, -40.0f, 215.0f }, F_TEMP },
    { { 0x06, 1, "Short-term fuel trim, bank 1", "STFT B1", OBD_UNIT_PERCENT, -100.0f, 99.2f }, F_TRIM },
    { { 0x07, 1, "Long-term fuel trim, bank 1", "LTFT B1", OBD_UNIT_PERCENT, -100.0f, 99.2f }, F_TRIM },
    { { 0x08, 1, "Short-term fuel trim, bank 2", "STFT B2", OBD_UNIT_PERCENT, -100.0f, 99.2f }, F_TRIM },
    { { 0x09, 1, "Long-term fuel trim, bank 2", "LTFT B2", OBD_UNIT_PERCENT, -100.0f, 99.2f }, F_TRIM },
    { { 0x0B, 1, "Intake manifold pressure", "MAP", OBD_UNIT_KPA, 0.0f, 255.0f }, F_RAW },
    { { 0x0C, 2, "Engine speed", "RPM", OBD_UNIT_RPM, 0.0f, 16383.75f }, F_RPM },
    { { 0x0D, 1, "Vehicle speed", "Speed", OBD_UNIT_KMH, 0.0f, 255.0f }, F_RAW },
    { { 0x0E, 1, "Ignition timing advance", "Timing", OBD_UNIT_DEGREES, -64.0f, 63.5f }, F_TIMING },
    { { 0x0F, 1, "Intake air temperature", "Intake air", OBD_UNIT_CELSIUS, -40.0f, 215.0f }, F_TEMP },
    { { 0x10, 2, "Mass air flow", "MAF", OBD_UNIT_GRAMS_S, 0.0f, 655.35f }, F_MAF },
    { { 0x11, 1, "Throttle position", "Throttle", OBD_UNIT_PERCENT, 0.0f, 100.0f }, F_PCT255 },
    { { 0x1F, 2, "Run time since engine start", "Run time", OBD_UNIT_SECONDS, 0.0f, 65535.0f }, F_WORD },
    { { 0x2F, 1, "Fuel tank level", "Fuel", OBD_UNIT_PERCENT, 0.0f, 100.0f }, F_PCT255 },
    { { 0x33, 1, "Barometric pressure", "Baro", OBD_UNIT_KPA, 0.0f, 255.0f }, F_RAW },
    { { 0x42, 2, "Control module voltage", "Module", OBD_UNIT_VOLTS, 0.0f, 65.535f }, F_MILLI },
    { { 0x46, 1, "Ambient air temperature", "Ambient", OBD_UNIT_CELSIUS, -40.0f, 215.0f }, F_TEMP },
    { { 0x5C, 1, "Engine oil temperature", "Oil", OBD_UNIT_CELSIUS, -40.0f, 210.0f }, F_TEMP },
};

#define TABLE_COUNT ((int)(sizeof table / sizeof table[0]))

static const pid_entry_t *find(uint8_t pid)
{
    for (int i = 0; i < TABLE_COUNT; i++) {
        if (table[i].info.pid == pid) {
            return &table[i];
        }
    }
    return NULL;
}

const obd_pid_info_t *obd_pid_info(uint8_t pid)
{
    const pid_entry_t *e = find(pid);
    return e != NULL ? &e->info : NULL;
}

int obd_pid_count(void)
{
    return TABLE_COUNT;
}

const obd_pid_info_t *obd_pid_at(int index)
{
    return index >= 0 && index < TABLE_COUNT ? &table[index].info : NULL;
}

obd_pid_result_t obd_pid_decode(uint8_t pid, const uint8_t *data, size_t len, float *value)
{
    const pid_entry_t *e = find(pid);
    if (e == NULL) {
        return OBD_PID_UNKNOWN;
    }
    if (data == NULL || len < e->info.bytes) {
        return OBD_PID_SHORT;
    }
    float a = (float)data[0];
    float word = e->info.bytes >= 2 ? (float)(data[0] * 256u + data[1]) : a;
    float v = 0.0f;
    switch (e->formula) {
    case F_PCT255: v = a * 100.0f / 255.0f; break;
    case F_TEMP:   v = a - 40.0f; break;
    case F_TRIM:   v = (a - 128.0f) * 100.0f / 128.0f; break;
    case F_RAW:    v = a; break;
    case F_RPM:    v = word / 4.0f; break;
    case F_TIMING: v = a / 2.0f - 64.0f; break;
    case F_MAF:    v = word / 100.0f; break;
    case F_WORD:   v = word; break;
    case F_MILLI:  v = word / 1000.0f; break;
    }
    if (value != NULL) {
        *value = v;
    }
    return OBD_PID_OK;
}

obd_pid_result_t obd_pid_from_reply(const obd_reply_t *reply, uint8_t pid, float *value)
{
    if (find(pid) == NULL) {
        return OBD_PID_UNKNOWN;
    }
    obd_pid_result_t result = OBD_PID_NO_REPLY;
    if (reply == NULL) {
        return result;
    }
    for (int e = 0; e < reply->ecu_count; e++) {
        const obd_ecu_t *ecu = &reply->ecu[e];
        for (int m = 0; m < ecu->msg_count; m++) {
            const obd_msg_t *msg = &ecu->msg[m];
            if (msg->len >= 2 && msg->data[0] == 0x41 && msg->data[1] == pid) {
                result = obd_pid_decode(pid, msg->data + 2, (size_t)msg->len - 2, value);
                if (result == OBD_PID_OK) {
                    return result;
                }
            }
        }
    }
    return result;
}

/* ---------- display ---------- */

float obd_unit_convert(obd_unit_t unit, float metric, obd_units_t units)
{
    if (units != OBD_UNITS_US) {
        return metric;
    }
    switch (unit) {
    case OBD_UNIT_KMH:     return metric * 0.621371f;           /* mph */
    case OBD_UNIT_CELSIUS: return metric * 9.0f / 5.0f + 32.0f; /* F */
    case OBD_UNIT_KPA:     return metric * 0.2953f;             /* inHg */
    default:               return metric;
    }
}

const char *obd_unit_label(obd_unit_t unit, obd_units_t units)
{
    bool us = units == OBD_UNITS_US;
    switch (unit) {
    case OBD_UNIT_PERCENT: return "%";
    case OBD_UNIT_CELSIUS: return us ? "F" : "C";
    case OBD_UNIT_KPA:     return us ? "inHg" : "kPa";
    case OBD_UNIT_RPM:     return "rpm";
    case OBD_UNIT_KMH:     return us ? "mph" : "km/h";
    case OBD_UNIT_DEGREES: return "deg";
    case OBD_UNIT_GRAMS_S: return "g/s";
    case OBD_UNIT_VOLTS:   return "V";
    case OBD_UNIT_SECONDS: return "s";
    default:               return "";
    }
}

/* "%.*f" without "-0" for values that round to zero; a sign on request
 * (none on zero). */
static void number(char *out, size_t out_len, float v, int decimals, bool sign)
{
    float half = decimals == 0 ? 0.5f : decimals == 1 ? 0.05f : 0.005f;
    if (v > -half && v < half) {
        v = 0.0f;
    }
    snprintf(out, out_len, sign && v != 0.0f ? "%+.*f" : "%.*f", decimals, (double)v);
}

static bool is_trim(uint8_t pid)
{
    return pid >= OBD_PID_STFT_B1 && pid <= OBD_PID_LTFT_B2;
}

char *obd_pid_format(uint8_t pid, float value, obd_units_t units, char *out, size_t out_len)
{
    const obd_pid_info_t *info = obd_pid_info(pid);
    if (out_len == 0) {
        return out;
    }
    if (info == NULL) {
        snprintf(out, out_len, "?");
        return out;
    }
    float v = obd_unit_convert(info->unit, value, units);
    char num[24];
    int decimals = 0;
    switch (info->unit) {
    case OBD_UNIT_KPA:     decimals = units == OBD_UNITS_US ? 1 : 0; break;
    case OBD_UNIT_DEGREES: decimals = 1; break;
    case OBD_UNIT_GRAMS_S: decimals = 1; break;
    case OBD_UNIT_VOLTS:   decimals = 2; break;
    case OBD_UNIT_PERCENT: decimals = is_trim(pid) ? 1 : 0; break;
    default:               decimals = 0; break;
    }
    if (info->unit == OBD_UNIT_SECONDS) {
        unsigned s = v > 0.0f ? (unsigned)(v + 0.5f) : 0u;
        snprintf(out, out_len, "%u:%02u", s / 60u, s % 60u);
        return out;
    }
    number(num, sizeof num, v, decimals, is_trim(pid));
    snprintf(out, out_len, "%s %s", num, obd_unit_label(info->unit, units));
    return out;
}

/* ---------- derived values ---------- */

float obd_trim_total(float short_term, float long_term)
{
    return short_term + long_term;
}

float obd_vacuum_kpa(float baro_kpa, float map_kpa)
{
    return baro_kpa - map_kpa;
}

char *obd_vacuum_format(float vacuum_kpa, obd_units_t units, char *out, size_t out_len)
{
    char num[24];
    bool us = units == OBD_UNITS_US;
    number(num, sizeof num, obd_unit_convert(OBD_UNIT_KPA, vacuum_kpa, units), us ? 1 : 0, false);
    snprintf(out, out_len, "%s %s", num, us ? "inHg" : "kPa");
    return out;
}
