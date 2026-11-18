#include "obd_pid.h"

#include <stdio.h>
#include <string.h>

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
    F_MAP_WIDE,  /* PID 87: A = sensors present, (256B + C) / 32 or (256D + E) / 32 */
    F_GEAR,      /* PID A4: A bit 1 = supported, (256C + D) / 1000 = actual gear ratio */
    F_ODO,       /* PID A6: (A 2^24 + B 2^16 + C 2^8 + D) / 10 km */
    F_O2_VOLTS,  /* PIDs 14-1B: A / 200 V (B is the sensor's trim, FF = not used) */
    F_LAMBDA,    /* PIDs 24-2B, 34-3B: (256A + B) * 2 / 65536 (C, D: voltage or current) */
} formula_t;

typedef struct {
    obd_pid_info_t info;
    formula_t formula;
} pid_entry_t;

/* Refresh times (last field): fast-changing values as often as possible,
 * short-term trims, load and timing twice a second, slow ones (temperatures,
 * long-term trims, fuel level, pressure outside) every 5-60 s, so on a slow
 * bus like ISO 9141-2 RPM and speed stay live (gauge-boy-3n6.10). */
static const pid_entry_t table[] = {
    { { 0x04, 1, "Calculated engine load", "Load", OBD_UNIT_PERCENT, 0.0f, 100.0f, 500u }, F_PCT255 },
    { { 0x05, 1, "Coolant temperature", "Coolant", OBD_UNIT_CELSIUS, -40.0f, 215.0f, 15000u }, F_TEMP },
    { { 0x06, 1, "Short-term fuel trim, bank 1", "STFT B1", OBD_UNIT_PERCENT, -100.0f, 99.2f, 500u }, F_TRIM },
    { { 0x07, 1, "Long-term fuel trim, bank 1", "LTFT B1", OBD_UNIT_PERCENT, -100.0f, 99.2f, 5000u }, F_TRIM },
    { { 0x08, 1, "Short-term fuel trim, bank 2", "STFT B2", OBD_UNIT_PERCENT, -100.0f, 99.2f, 500u }, F_TRIM },
    { { 0x09, 1, "Long-term fuel trim, bank 2", "LTFT B2", OBD_UNIT_PERCENT, -100.0f, 99.2f, 5000u }, F_TRIM },
    { { 0x0B, 1, "Intake manifold pressure", "MAP", OBD_UNIT_KPA, 0.0f, 255.0f, 0u }, F_RAW },
    { { 0x0C, 2, "Engine speed", "RPM", OBD_UNIT_RPM, 0.0f, 16383.75f, 0u }, F_RPM },
    { { 0x0D, 1, "Vehicle speed", "Speed", OBD_UNIT_KMH, 0.0f, 255.0f, 0u }, F_RAW },
    { { 0x0E, 1, "Ignition timing advance", "Timing", OBD_UNIT_DEGREES, -64.0f, 63.5f, 500u }, F_TIMING },
    { { 0x0F, 1, "Intake air temperature", "Intake air", OBD_UNIT_CELSIUS, -40.0f, 215.0f, 10000u }, F_TEMP },
    { { 0x10, 2, "Mass air flow", "MAF", OBD_UNIT_GRAMS_S, 0.0f, 655.35f, 0u }, F_MAF },
    { { 0x11, 1, "Throttle position", "Throttle", OBD_UNIT_PERCENT, 0.0f, 100.0f, 0u }, F_PCT255 },
    /* Oxygen sensors, numbered as PID 13 lays them out (banks 1 and 2,
     * sensors 1-4; sensor 1 is before the catalytic converter). Narrow
     * band (14-1B): a voltage switching between about 0.1 V (lean) and
     * 0.9 V (rich). Wide band (24-2B by voltage, 34-3B by current): lambda,
     * 1.000 the ideal mixture, below rich, above lean. */
    { { 0x14, 2, "O2 sensor voltage, bank 1 sensor 1", "O2 B1S1", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x15, 2, "O2 sensor voltage, bank 1 sensor 2", "O2 B1S2", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x16, 2, "O2 sensor voltage, bank 1 sensor 3", "O2 B1S3", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x17, 2, "O2 sensor voltage, bank 1 sensor 4", "O2 B1S4", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x18, 2, "O2 sensor voltage, bank 2 sensor 1", "O2 B2S1", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x19, 2, "O2 sensor voltage, bank 2 sensor 2", "O2 B2S2", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x1A, 2, "O2 sensor voltage, bank 2 sensor 3", "O2 B2S3", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x1B, 2, "O2 sensor voltage, bank 2 sensor 4", "O2 B2S4", OBD_UNIT_VOLTS, 0.0f, 1.275f, 500u }, F_O2_VOLTS },
    { { 0x24, 4, "O2 sensor lambda (wide band, voltage), bank 1 sensor 1", "O2 B1S1", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x25, 4, "O2 sensor lambda (wide band, voltage), bank 1 sensor 2", "O2 B1S2", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x26, 4, "O2 sensor lambda (wide band, voltage), bank 1 sensor 3", "O2 B1S3", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x27, 4, "O2 sensor lambda (wide band, voltage), bank 1 sensor 4", "O2 B1S4", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x28, 4, "O2 sensor lambda (wide band, voltage), bank 2 sensor 1", "O2 B2S1", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x29, 4, "O2 sensor lambda (wide band, voltage), bank 2 sensor 2", "O2 B2S2", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x2A, 4, "O2 sensor lambda (wide band, voltage), bank 2 sensor 3", "O2 B2S3", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x2B, 4, "O2 sensor lambda (wide band, voltage), bank 2 sensor 4", "O2 B2S4", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x34, 4, "O2 sensor lambda (wide band, current), bank 1 sensor 1", "O2 B1S1", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x35, 4, "O2 sensor lambda (wide band, current), bank 1 sensor 2", "O2 B1S2", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x36, 4, "O2 sensor lambda (wide band, current), bank 1 sensor 3", "O2 B1S3", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x37, 4, "O2 sensor lambda (wide band, current), bank 1 sensor 4", "O2 B1S4", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x38, 4, "O2 sensor lambda (wide band, current), bank 2 sensor 1", "O2 B2S1", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x39, 4, "O2 sensor lambda (wide band, current), bank 2 sensor 2", "O2 B2S2", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x3A, 4, "O2 sensor lambda (wide band, current), bank 2 sensor 3", "O2 B2S3", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x3B, 4, "O2 sensor lambda (wide band, current), bank 2 sensor 4", "O2 B2S4", OBD_UNIT_LAMBDA, 0.0f, 2.0f, 500u }, F_LAMBDA },
    { { 0x1F, 2, "Run time since engine start", "Run time", OBD_UNIT_SECONDS, 0.0f, 65535.0f, 5000u }, F_WORD },
    { { 0x2F, 1, "Fuel tank level", "Fuel", OBD_UNIT_PERCENT, 0.0f, 100.0f, 30000u }, F_PCT255 },
    { { 0x33, 1, "Barometric pressure", "Baro", OBD_UNIT_KPA, 0.0f, 255.0f, 60000u }, F_RAW },
    { { 0x42, 2, "Control module voltage", "Module", OBD_UNIT_VOLTS, 0.0f, 65.535f, 5000u }, F_MILLI },
    { { 0x46, 1, "Ambient air temperature", "Ambient", OBD_UNIT_CELSIUS, -40.0f, 215.0f, 60000u }, F_TEMP },
    { { 0x5C, 1, "Engine oil temperature", "Oil", OBD_UNIT_CELSIUS, -40.0f, 210.0f, 15000u }, F_TEMP },
    { { 0x87, 5, "Intake manifold pressure (wide range)", "MAP", OBD_UNIT_KPA, 0.0f, 2048.0f, 0u }, F_MAP_WIDE },
    { { 0xA4, 4, "Transmission gear ratio", "Gear ratio", OBD_UNIT_RATIO, 0.0f, 65.535f, 500u }, F_GEAR },
    { { 0xA6, 4, "Odometer", "Odometer", OBD_UNIT_KM, 0.0f, 429496729.5f, 60000u }, F_ODO },
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
    case F_MAP_WIDE: {
        /* Sensor A if the car says it has one, else sensor B. */
        bool b_only = (data[0] & 0x01) == 0 && (data[0] & 0x02) != 0;
        const uint8_t *x = data + (b_only ? 3 : 1);
        v = (float)(x[0] * 256u + x[1]) / 32.0f;
        break;
    }
    case F_GEAR:
        if ((data[0] & 0x02) == 0) {
            return OBD_PID_NO_REPLY;   /* the car says it doesn't report it */
        }
        v = (float)(data[2] * 256u + data[3]) / 1000.0f;
        break;
    case F_O2_VOLTS: v = a / 200.0f; break;
    case F_LAMBDA:   v = word * 2.0f / 65536.0f; break;
    case F_ODO: {
        uint32_t raw = (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 |
                       data[3];
        v = (float)((double)raw / 10.0);
        break;
    }
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

int obd_pid_parse_multi(const uint8_t *msg, size_t len, uint8_t *pids, float *values, int max)
{
    int n = 0;
    if (msg == NULL || len < 2 || msg[0] != 0x41) {
        return 0;
    }
    size_t i = 1;
    while (i < len && n < max) {
        const pid_entry_t *e = find(msg[i]);
        if (e == NULL || i + 1 + e->info.bytes > len) {
            break;
        }
        float v = 0.0f;
        obd_pid_decode(msg[i], msg + i + 1, e->info.bytes, &v);
        pids[n] = msg[i];
        values[n++] = v;
        i += 1 + e->info.bytes;
    }
    return n;
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
    case OBD_UNIT_KM:      return metric * 0.621371f;           /* miles */
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
    case OBD_UNIT_KM:      return us ? "mi" : "km";
    case OBD_UNIT_RATIO:   return ":1";
    case OBD_UNIT_LAMBDA:  return "lambda";
    default:               return "";
    }
}

/* "%.*f" without "-0" for values that round to zero; a sign on request
 * (none on zero). */
static void number(char *out, size_t out_len, float v, int decimals, bool sign)
{
    float half = decimals == 0 ? 0.5f : decimals == 1 ? 0.05f : decimals == 2 ? 0.005f : 0.0005f;
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
    case OBD_UNIT_RATIO:   decimals = 2; break;
    case OBD_UNIT_LAMBDA:  decimals = 3; break;
    case OBD_UNIT_PERCENT: decimals = is_trim(pid) ? 1 : 0; break;
    default:               decimals = 0; break;
    }
    if (info->unit == OBD_UNIT_SECONDS) {
        unsigned s = v > 0.0f ? (unsigned)(v + 0.5f) : 0u;
        snprintf(out, out_len, "%u:%02u", s / 60u, s % 60u);
        return out;
    }
    if (info->unit == OBD_UNIT_KM) {   /* odometer: 29,958 mi */
        obd_format_grouped((long long)(v + 0.5f), num, sizeof num);
        snprintf(out, out_len, "%s %s", num, obd_unit_label(info->unit, units));
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

char *obd_boost_format(float boost_kpa, obd_units_t units, char *out, size_t out_len)
{
    char num[24];
    bool us = units == OBD_UNITS_US;
    number(num, sizeof num, us ? boost_kpa * 0.145038f : boost_kpa, us ? 1 : 0, false);
    snprintf(out, out_len, "%s %s", num, us ? "psi" : "kPa");
    return out;
}

char *obd_vacuum_format(float vacuum_kpa, obd_units_t units, char *out, size_t out_len)
{
    char num[24];
    bool us = units == OBD_UNITS_US;
    number(num, sizeof num, obd_unit_convert(OBD_UNIT_KPA, vacuum_kpa, units), us ? 1 : 0, false);
    snprintf(out, out_len, "%s %s", num, us ? "inHg" : "kPa");
    return out;
}

char *obd_format_grouped(long long n, char *out, size_t len)
{
    char digits[32];
    bool neg = n < 0;
    unsigned long long u = neg ? (unsigned long long)(-(n + 1)) + 1u : (unsigned long long)n;
    snprintf(digits, sizeof digits, "%llu", u);
    size_t d = strlen(digits), o = 0;
    if (len == 0) {
        return out;
    }
    if (neg && o + 1 < len) {
        out[o++] = '-';
    }
    for (size_t i = 0; i < d && o + 1 < len; i++) {
        if (i > 0 && (d - i) % 3 == 0) {
            if (o + 2 >= len) {
                break;
            }
            out[o++] = ',';
        }
        out[o++] = digits[i];
    }
    out[o] = '\0';
    return out;
}
