/* Tests for source/obd/obd_pid: Mode 01 live data decoding and display. */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "obd/obd_live.h"
#include "obd/obd_pid.h"

static obd_reply_t reply; /* large: keep off the stack */
static elm_response_t resp;
static char text[32];

#define CHECK_NEAR(actual, expected)                                           \
    CHECK((actual) - (expected) < 0.001f && (expected) - (actual) < 0.001f)

static float dec(uint8_t pid, uint8_t a, uint8_t b)
{
    uint8_t data[2] = { a, b };
    float v = -12345.0f;
    CHECK_EQ_INT(obd_pid_decode(pid, data, 2, &v), OBD_PID_OK);
    return v;
}

static obd_pid_result_t from(const char *const *lines, int protocol, bool headers, uint8_t pid,
                             float *v)
{
    int n = 0;
    memset(&resp, 0, sizeof resp);
    for (; lines[n] != NULL; n++) {
        snprintf(resp.lines[n], ELM_LINE_MAX, "%s", lines[n]);
    }
    resp.line_count = n;
    obd_assemble(&resp, protocol, headers, &reply);
    return obd_pid_from_reply(&reply, pid, v);
}

static void test_pid_table(void)
{
    CHECK(obd_pid_count() >= 20);
    CHECK(obd_pid_count() <= OBD_LIVE_MAX);   /* LIVE's All view polls every one */
    for (int i = 0; i < obd_pid_count(); i++) {
        const obd_pid_info_t *p = obd_pid_at(i);
        CHECK(p != NULL && p->name[0] != '\0' && p->label[0] != '\0');
        CHECK(strlen(p->label) <= 10);
        CHECK(p->bytes >= 1 && p->bytes <= 5);
        CHECK(p->min < p->max);
        CHECK(obd_pid_info(p->pid) == p);
        for (int j = i + 1; j < obd_pid_count(); j++) {
            CHECK(obd_pid_at(j)->pid != p->pid);
        }
    }
    CHECK(obd_pid_info(0x99) == NULL);
    CHECK(obd_pid_at(-1) == NULL && obd_pid_at(obd_pid_count()) == NULL);
}

static void test_pid_formulas(void)
{
    CHECK_NEAR(dec(0x04, 0xFF, 0), 100.0f);          /* load */
    CHECK_NEAR(dec(0x05, 0x5A, 0), 50.0f);           /* coolant 90 - 40 */
    CHECK_NEAR(dec(0x06, 0x80, 0), 0.0f);            /* trim: centre */
    CHECK_NEAR(dec(0x07, 0x00, 0), -100.0f);
    CHECK_NEAR(dec(0x08, 0xFF, 0), 99.21875f);
    CHECK_NEAR(dec(0x09, 0x84, 0), 3.125f);
    CHECK_NEAR(dec(0x0B, 0x21, 0), 33.0f);           /* MAP kPa */
    CHECK_NEAR(dec(0x0C, 0x1A, 0xF8), 1726.0f);      /* RPM: 0x1AF8 / 4 */
    CHECK_NEAR(dec(0x0C, 0xFF, 0xFF), 16383.75f);
    CHECK_NEAR(dec(0x0D, 0x64, 0), 100.0f);          /* km/h */
    CHECK_NEAR(dec(0x0E, 0x8C, 0), 6.0f);            /* timing 140 / 2 - 64 */
    CHECK_NEAR(dec(0x0E, 0x00, 0), -64.0f);
    CHECK_NEAR(dec(0x0F, 0x00, 0), -40.0f);
    CHECK_NEAR(dec(0x10, 0x01, 0xF4), 5.0f);         /* MAF 500 / 100 */
    CHECK_NEAR(dec(0x11, 0x00, 0), 0.0f);
    CHECK_NEAR(dec(0x1F, 0x02, 0x58), 600.0f);       /* run time s */
    CHECK_NEAR(dec(0x33, 0x65, 0), 101.0f);          /* baro */
    CHECK_NEAR(dec(0x42, 0x35, 0xF0), 13.808f);      /* module V */
    CHECK_NEAR(dec(0x5C, 0x82, 0), 90.0f);           /* oil */
}

static void test_pid_bad_input(void)
{
    uint8_t one[1] = { 0x1A };
    float v = 7.0f;
    CHECK_EQ_INT(obd_pid_decode(0x0C, one, 1, &v), OBD_PID_SHORT);   /* RPM needs 2 */
    CHECK_EQ_INT(obd_pid_decode(0x05, one, 0, &v), OBD_PID_SHORT);
    CHECK_EQ_INT(obd_pid_decode(0x05, NULL, 1, &v), OBD_PID_SHORT);
    CHECK_EQ_INT(obd_pid_decode(0x99, one, 1, &v), OBD_PID_UNKNOWN);
    CHECK_NEAR(v, 7.0f);                                             /* untouched */
    CHECK_EQ_INT(obd_pid_decode(0x05, one, 1, NULL), OBD_PID_OK);    /* value optional */
    CHECK_EQ_INT(obd_pid_from_reply(NULL, 0x05, &v), OBD_PID_NO_REPLY);
}

static void test_pid_from_reply(void)
{
    float v = 0.0f;
    /* ISO 9141-2, headers off (the Ridgeline's layout). */
    static const char *const rpm[] = { "41 0C 1A F8", NULL };
    CHECK_EQ_INT(from(rpm, 3, false, 0x0C, &v), OBD_PID_OK);
    CHECK_NEAR(v, 1726.0f);

    /* CAN headers on: engine and transmission both answer speed. */
    static const char *const two[] = { "7E8 03 41 0D 2A", "7E9 03 41 0D 2B", NULL };
    CHECK_EQ_INT(from(two, 6, true, 0x0D, &v), OBD_PID_OK);
    CHECK_NEAR(v, 42.0f);

    /* CAN headers off, trailing spaces as the adapter sends them. */
    static const char *const can[] = { "41 05 5A ", NULL };
    CHECK_EQ_INT(from(can, 6, false, 0x05, &v), OBD_PID_OK);
    CHECK_NEAR(v, 50.0f);

    /* Another PID's reply, a refusal, a cut-short reply. */
    static const char *const other[] = { "41 0D 00", NULL };
    CHECK_EQ_INT(from(other, 3, false, 0x0C, &v), OBD_PID_NO_REPLY);
    static const char *const refused[] = { "7F 01 12", NULL };
    CHECK_EQ_INT(from(refused, 3, false, 0x0C, &v), OBD_PID_NO_REPLY);
    static const char *const cut[] = { "41 0C 1A", NULL };
    CHECK_EQ_INT(from(cut, 3, false, 0x0C, &v), OBD_PID_SHORT);

    /* A short reply from one ECU, a good one from the next. */
    static const char *const mixed[] = { "7E8 02 41 0C", "7E9 04 41 0C 0B B8", NULL };
    CHECK_EQ_INT(from(mixed, 6, true, 0x0C, &v), OBD_PID_OK);
    CHECK_NEAR(v, 750.0f);

    CHECK_EQ_INT(from(rpm, 3, false, 0x99, &v), OBD_PID_UNKNOWN);
}

static void test_pid_format(void)
{
    CHECK_EQ_STR(obd_pid_format(0x0C, 1726.0f, OBD_UNITS_US, text, sizeof text), "1726 rpm");
    CHECK_EQ_STR(obd_pid_format(0x0D, 100.0f, OBD_UNITS_US, text, sizeof text), "62 mph");
    CHECK_EQ_STR(obd_pid_format(0x0D, 100.0f, OBD_UNITS_METRIC, text, sizeof text), "100 km/h");
    CHECK_EQ_STR(obd_pid_format(0x05, 90.0f, OBD_UNITS_US, text, sizeof text), "194 F");
    CHECK_EQ_STR(obd_pid_format(0x05, 0.0f, OBD_UNITS_US, text, sizeof text), "32 F");
    CHECK_EQ_STR(obd_pid_format(0x05, 90.0f, OBD_UNITS_METRIC, text, sizeof text), "90 C");
    CHECK_EQ_STR(obd_pid_format(0x0F, -0.3f, OBD_UNITS_METRIC, text, sizeof text), "0 C");
    CHECK_EQ_STR(obd_pid_format(0x06, 3.125f, OBD_UNITS_US, text, sizeof text), "+3.1 %");
    CHECK_EQ_STR(obd_pid_format(0x07, -100.0f, OBD_UNITS_US, text, sizeof text), "-100.0 %");
    CHECK_EQ_STR(obd_pid_format(0x08, 0.0f, OBD_UNITS_US, text, sizeof text), "0.0 %");
    CHECK_EQ_STR(obd_pid_format(0x09, -0.02f, OBD_UNITS_US, text, sizeof text), "0.0 %");
    CHECK_EQ_STR(obd_pid_format(0x04, 33.3f, OBD_UNITS_US, text, sizeof text), "33 %");
    CHECK_EQ_STR(obd_pid_format(0x0B, 33.0f, OBD_UNITS_US, text, sizeof text), "9.7 inHg");
    CHECK_EQ_STR(obd_pid_format(0x0B, 33.0f, OBD_UNITS_METRIC, text, sizeof text), "33 kPa");
    CHECK_EQ_STR(obd_pid_format(0x0E, 6.0f, OBD_UNITS_US, text, sizeof text), "6.0 deg");
    CHECK_EQ_STR(obd_pid_format(0x0E, -0.5f, OBD_UNITS_US, text, sizeof text), "-0.5 deg");
    CHECK_EQ_STR(obd_pid_format(0x10, 5.0f, OBD_UNITS_US, text, sizeof text), "5.0 g/s");
    CHECK_EQ_STR(obd_pid_format(0x42, 13.808f, OBD_UNITS_US, text, sizeof text), "13.81 V");
    CHECK_EQ_STR(obd_pid_format(0x1F, 605.0f, OBD_UNITS_US, text, sizeof text), "10:05");
    CHECK_EQ_STR(obd_pid_format(0x99, 1.0f, OBD_UNITS_US, text, sizeof text), "?");
    char tiny[5];
    obd_pid_format(0x0C, 1726.0f, OBD_UNITS_US, tiny, sizeof tiny);
    CHECK_EQ_STR(tiny, "1726");                      /* cut, still terminated */
    CHECK_EQ_STR(obd_unit_label(OBD_UNIT_KMH, OBD_UNITS_US), "mph");
}

static void test_pid_derived(void)
{
    CHECK_NEAR(obd_trim_total(3.125f, -1.5625f), 1.5625f);
    /* Ridgeline idle: baro 101 kPa (or key-on MAP), MAP 33 kPa. */
    float vac = obd_vacuum_kpa(101.0f, 33.0f);
    CHECK_NEAR(vac, 68.0f);
    CHECK_EQ_STR(obd_vacuum_format(vac, OBD_UNITS_US, text, sizeof text), "20.1 inHg");
    CHECK_EQ_STR(obd_vacuum_format(vac, OBD_UNITS_METRIC, text, sizeof text), "68 kPa");
    CHECK_NEAR(obd_vacuum_kpa(100.0f, 120.0f), -20.0f);      /* boost */
}

static void test_pid_parse_multi(void)
{
    uint8_t pids[6];
    float v[6];
    /* RPM 0x1AF8 / 4, speed 0, coolant 0x7B - 40. */
    static const uint8_t three[] = { 0x41, 0x0C, 0x1A, 0xF8, 0x0D, 0x00, 0x05, 0x7B };
    CHECK_EQ_INT(obd_pid_parse_multi(three, sizeof three, pids, v, 6), 3);
    CHECK_EQ_INT(pids[0], 0x0C);
    CHECK_NEAR(v[0], 1726.0f);
    CHECK_EQ_INT(pids[1], 0x0D);
    CHECK_NEAR(v[1], 0.0f);
    CHECK_EQ_INT(pids[2], 0x05);
    CHECK_NEAR(v[2], 83.0f);
    CHECK_EQ_INT(obd_pid_parse_multi(three, sizeof three, pids, v, 2), 2);   /* max */
    /* Cut short in the middle of RPM's second byte: only what's whole. */
    static const uint8_t cut[] = { 0x41, 0x0D, 0x05, 0x0C, 0x1A };
    CHECK_EQ_INT(obd_pid_parse_multi(cut, sizeof cut, pids, v, 6), 1);
    CHECK_EQ_INT(pids[0], 0x0D);
    /* A PID the table can't size stops the walk; not a Mode 01 reply: 0. */
    static const uint8_t odd[] = { 0x41, 0x05, 0x7B, 0x99, 0x01, 0x0D, 0x05 };
    CHECK_EQ_INT(obd_pid_parse_multi(odd, sizeof odd, pids, v, 6), 1);
    static const uint8_t other[] = { 0x49, 0x02, 0x01 };
    CHECK_EQ_INT(obd_pid_parse_multi(other, sizeof other, pids, v, 6), 0);
    CHECK_EQ_INT(obd_pid_parse_multi(NULL, 0, pids, v, 6), 0);
}

static void test_pid_map_wide(void)
{
    /* Sensor A present: 0x0C80 / 32 = 100 kPa. */
    uint8_t a[5] = { 0x01, 0x0C, 0x80, 0x00, 0x00 };
    float v = 0.0f;
    CHECK_EQ_INT(obd_pid_decode(0x87, a, 5, &v), OBD_PID_OK);
    CHECK_NEAR(v, 100.0f);
    /* Only sensor B: 0x1900 / 32 = 200 kPa (boost). */
    uint8_t b[5] = { 0x02, 0xFF, 0xFF, 0x19, 0x00 };
    CHECK_EQ_INT(obd_pid_decode(0x87, b, 5, &v), OBD_PID_OK);
    CHECK_NEAR(v, 200.0f);
    CHECK_EQ_INT(obd_pid_decode(0x87, a, 4, &v), OBD_PID_SHORT);
    CHECK_EQ_STR(obd_pid_format(0x87, 100.0f, OBD_UNITS_US, text, sizeof text), "29.5 inHg");
    /* In a combined CAN reply. */
    static const uint8_t multi[] = { 0x41, 0x87, 0x01, 0x0C, 0x80, 0x00, 0x00, 0x0C, 0x0B, 0xB8 };
    uint8_t pids[4];
    float vals[4];
    CHECK_EQ_INT(obd_pid_parse_multi(multi, sizeof multi, pids, vals, 4), 2);
    CHECK_NEAR(vals[0], 100.0f);
    CHECK_NEAR(vals[1], 750.0f);
    CHECK_EQ_STR(obd_boost_format(50.0f, OBD_UNITS_US, text, sizeof text), "7.3 psi");
    CHECK_EQ_STR(obd_boost_format(50.0f, OBD_UNITS_METRIC, text, sizeof text), "50 kPa");
}

static void test_pid_odometer_gear(void)
{
    /* Odometer: 0x0007A120 = 500000 tenths = 50000.0 km = 31069 mi. */
    uint8_t odo[4] = { 0x00, 0x07, 0xA1, 0x20 };
    float v = 0.0f;
    CHECK_EQ_INT(obd_pid_decode(0xA6, odo, 4, &v), OBD_PID_OK);
    CHECK_NEAR(v, 50000.0f);
    CHECK_EQ_STR(obd_pid_format(0xA6, v, OBD_UNITS_US, text, sizeof text), "31,069 mi");
    CHECK_EQ_STR(obd_pid_format(0xA6, v, OBD_UNITS_METRIC, text, sizeof text), "50,000 km");
    char g[16];
    CHECK_EQ_STR(obd_format_grouped(0, g, sizeof g), "0");
    CHECK_EQ_STR(obd_format_grouped(999, g, sizeof g), "999");
    CHECK_EQ_STR(obd_format_grouped(1000, g, sizeof g), "1,000");
    CHECK_EQ_STR(obd_format_grouped(1234567, g, sizeof g), "1,234,567");
    CHECK_EQ_STR(obd_format_grouped(-45210, g, sizeof g), "-45,210");
    CHECK_EQ_STR(obd_format_grouped(1234567, g, 6), "1,234");                /* cut, no overflow */
    CHECK_EQ_INT(obd_pid_decode(0xA6, odo, 3, &v), OBD_PID_SHORT);
    /* Gear ratio: supported bit A1, 0x0D7A = 3450 -> 3.45. */
    uint8_t gear[4] = { 0x02, 0x00, 0x0D, 0x7A };
    CHECK_EQ_INT(obd_pid_decode(0xA4, gear, 4, &v), OBD_PID_OK);
    CHECK_NEAR(v, 3.45f);
    CHECK_EQ_STR(obd_pid_format(0xA4, v, OBD_UNITS_US, text, sizeof text), "3.45 :1");
    gear[0] = 0x00;                                       /* not reported */
    CHECK_EQ_INT(obd_pid_decode(0xA4, gear, 4, &v), OBD_PID_NO_REPLY);
}

void suite_pid(void)
{
    RUN_TEST(test_pid_odometer_gear);
    RUN_TEST(test_pid_map_wide);
    RUN_TEST(test_pid_parse_multi);
    RUN_TEST(test_pid_table);
    RUN_TEST(test_pid_formulas);
    RUN_TEST(test_pid_bad_input);
    RUN_TEST(test_pid_from_reply);
    RUN_TEST(test_pid_format);
    RUN_TEST(test_pid_derived);
}
