#ifndef GAUGEBOY_OBD_PID_H
#define GAUGEBOY_OBD_PID_H

/*
 * Mode 01 live data: what each PID means, how to decode its bytes, and
 * how to show the value (SAE J1979 formulas).
 *
 * Values are decoded to metric (km/h, degC, kPa, g/s, V) and converted for
 * display, so the units toggle (M3) only changes formatting. Text is ASCII
 * only (the HOME font has no degree sign): "195 F", "34 mph".
 *
 * Platform-free, no allocation.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "obd_reply.h"

typedef enum {
    OBD_UNIT_NONE = 0,
    OBD_UNIT_PERCENT,
    OBD_UNIT_CELSIUS,
    OBD_UNIT_KPA,        /* manifold / barometric pressure */
    OBD_UNIT_RPM,
    OBD_UNIT_KMH,
    OBD_UNIT_DEGREES,    /* ignition timing, before TDC */
    OBD_UNIT_GRAMS_S,    /* mass air flow */
    OBD_UNIT_VOLTS,
    OBD_UNIT_SECONDS,
} obd_unit_t;

typedef enum {
    OBD_UNITS_US = 0,    /* mph, F, inHg */
    OBD_UNITS_METRIC,    /* km/h, C, kPa */
} obd_units_t;

typedef struct {
    uint8_t pid;
    uint8_t bytes;           /* data bytes after "41 pid" */
    const char *name;        /* "Engine speed" */
    const char *label;       /* short, for the dashboard: "RPM" */
    obd_unit_t unit;
    float min, max;          /* decoded range (metric) */
    uint32_t refresh_ms;     /* desired refresh time; 0 = as often as possible */
} obd_pid_info_t;

/* PIDs GaugeBoy decodes (fuel trims, live data). NULL for others. */
const obd_pid_info_t *obd_pid_info(uint8_t pid);
int obd_pid_count(void);
const obd_pid_info_t *obd_pid_at(int index);

typedef enum {
    OBD_PID_OK = 0,
    OBD_PID_UNKNOWN,         /* not a PID this table decodes */
    OBD_PID_NO_REPLY,        /* no "41 pid ..." message in the reply */
    OBD_PID_SHORT,           /* fewer data bytes than the PID has */
} obd_pid_result_t;

/* Decode data bytes A, B, ... (after "41 pid"). */
obd_pid_result_t obd_pid_decode(uint8_t pid, const uint8_t *data, size_t len, float *value);

/* Decode the first "41 pid ..." message in an assembled reply (the first
 * ECU that answered, e.g. the engine when the transmission also answers
 * vehicle speed). */
obd_pid_result_t obd_pid_from_reply(const obd_reply_t *reply, uint8_t pid, float *value);

/*
 * A Mode 01 reply to a request for several PIDs (CAN only, up to 6):
 * "41 0C A B 0D C 05 D ..." gives (0C, rpm), (0D, speed), (05, coolant).
 * msg starts at the 0x41. Stops at a PID this table can't size or a cut-
 * short value. Writes up to max pairs; returns how many.
 */
int obd_pid_parse_multi(const uint8_t *msg, size_t len, uint8_t *pids, float *values, int max);

/* ---------- display ---------- */

/* A metric value in the display units, and the unit's label ("mph"). */
float obd_unit_convert(obd_unit_t unit, float metric, obd_units_t units);
const char *obd_unit_label(obd_unit_t unit, obd_units_t units);

/* "2150 rpm", "34 mph", "195 F", "+3.1 %", "13.82 V". Fuel trims get a
 * sign; pressures in inHg (US) get one decimal. Returns out. */
char *obd_pid_format(uint8_t pid, float value, obd_units_t units, char *out, size_t out_len);

/* ---------- derived values ---------- */

/* Fuel trim PIDs: 06 / 07 bank 1 short / long, 08 / 09 bank 2. */
#define OBD_PID_STFT_B1 0x06
#define OBD_PID_LTFT_B1 0x07
#define OBD_PID_STFT_B2 0x08
#define OBD_PID_LTFT_B2 0x09
#define OBD_PID_MAP     0x0B
#define OBD_PID_BARO    0x33
#define OBD_PID_MAP_WIDE 0x87   /* MAP on cars without 0B, e.g. turbo Subarus */

/* Total trim = short + long, in %. */
float obd_trim_total(float short_term, float long_term);

/* Manifold vacuum = barometric pressure - MAP, in kPa (negative = boost).
 * The baro comes from PID 33, or, without it, the MAP read at key-on,
 * engine off (brief section 7). */
float obd_vacuum_kpa(float baro_kpa, float map_kpa);

/* "7.3 psi" / "50 kPa" boost text (pressure above outside air). */
char *obd_boost_format(float boost_kpa, obd_units_t units, char *out, size_t out_len);

/* "18.2 inHg" / "62 kPa" vacuum text. Returns out. */
char *obd_vacuum_format(float vacuum_kpa, obd_units_t units, char *out, size_t out_len);

#endif
