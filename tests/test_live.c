/* Tests for source/obd/obd_live: the live data poller, on the fake adapter. */
#include <string.h>

#include "harness.h"
#include "fake_io.h"
#include "obd/obd_live.h"
#include "obd/obd_pid.h"
#include "data/live_view.h"

static fake_io_t f;
static elm_t elm;
static obd_link_t link;
static obd_live_t lv;

#define CONFIG_MAX_TEST 20

#define CHECK_NEAR(actual, expected)                                           \
    CHECK((actual) - (expected) < 0.01f && (expected) - (actual) < 0.01f)

/* ISO 9141-2 link supporting the given PIDs (0 ends the list). */
static void setup(const uint8_t *pids)
{
    fake_io_init(&f);
    elm_io_t io = fake_io_iface(&f);
    elm_bind(&elm, &io);
    memset(&link, 0, sizeof link);
    link.protocol = 3;
    link.ranges_read = 3;
    for (; *pids != 0; pids++) {
        int bit = (*pids - 1) % 0x20;
        link.supported[(*pids - 1) / 0x20][bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
    }
}

static void test_live_init_keeps_supported_known_pids(void)
{
    static const uint8_t car[] = { 0x05, 0x0B, 0x0C, 0x0D, 0x33, 0 };
    setup(car);
    /* 08 not supported, 99 unknown, 0C twice, 01 not a live PID. */
    static const uint8_t want[] = { 0x0C, 0x0D, 0x08, 0x05, 0x0C, 0x99, 0x01, 0x0B };
    CHECK_EQ_INT(obd_live_init(&lv, &link, want, (int)sizeof want), 4);
    CHECK_EQ_INT(lv.pid[0].pid, 0x0C);
    CHECK_EQ_INT(lv.pid[1].pid, 0x0D);
    CHECK_EQ_INT(lv.pid[2].pid, 0x05);
    CHECK_EQ_INT(lv.pid[3].pid, 0x0B);
    CHECK(obd_live_get(&lv, 0x08) == NULL);
    CHECK(!obd_live_value(&lv, 0x0C, NULL));      /* nothing read yet */
}

static void test_live_round_robin_and_volts(void)
{
    static const uint8_t car[] = { 0x05, 0x0C, 0x0D, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C, 0x0D, 0x05 };
    obd_live_init(&lv, &link, want, 3);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 100, 0);
    fake_io_reply(&f, "010C", "41 0C 1A F8 \r\r>", 300, 0);
    fake_io_reply(&f, "010D", "41 0D 00 \r\r>", 300, 0);
    fake_io_reply(&f, "0105", "41 05 5A \r\r>", 300, 0);
    fake_io_reply(&f, "010C", "41 0C 0B B8 \r\r>", 300, 0);

    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);   /* ATRV first */
    CHECK(lv.have_volts);
    CHECK_NEAR(lv.volts, 12.6f);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    }
    float v = 0.0f;
    CHECK(obd_live_value(&lv, 0x0C, &v));
    CHECK_NEAR(v, 750.0f);                       /* the second read */
    CHECK(obd_live_value(&lv, 0x0D, &v) && v == 0.0f);
    CHECK(obd_live_value(&lv, 0x05, &v));
    CHECK_NEAR(v, 50.0f);
    const obd_live_pid_t *rpm = obd_live_get(&lv, 0x0C);
    CHECK_EQ_INT((int)rpm->reads, 2);
    CHECK(rpm->hz > 1.0f && rpm->hz < 1.2f);     /* 3 requests of ~300 ms apart */
    CHECK_EQ_INT((int)lv.requests, 5);
    CHECK_EQ_INT((int)lv.failures, 0);
    CHECK_EQ_INT(f.unexpected_writes, 0);
    float rate = obd_live_rate(&lv, f.now);
    CHECK(rate > 3.0f && rate < 4.0f);           /* 5 requests in ~1.3 s */
}

static void test_live_volts_again_after_5_s(void)
{
    static const uint8_t car[] = { 0x0C, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C };
    obd_live_init(&lv, &link, want, 1);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    for (int i = 0; i < 4; i++) {   /* 1.5 s each: ATRV is due after the 4th */
        fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 1500, 0);
    }
    fake_io_reply(&f, "ATRV", "13.9V\r\r>", 10, 0);
    for (int i = 0; i < 6; i++) {
        CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    }
    CHECK_NEAR(lv.volts, 13.9f);
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static void test_live_drops_no_data_pids(void)
{
    static const uint8_t car[] = { 0x0C, 0x0D, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0D, 0x0C };
    obd_live_init(&lv, &link, want, 2);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010D", "NO DATA\r\r>", 50, 0);
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);
    fake_io_reply(&f, "010D", "41 0D\r\r>", 50, 0);        /* cut short: unusable */
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);
    fake_io_reply(&f, "010D", "7F 01 12\r\r>", 50, 0);     /* refused */
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);  /* 0D dropped: 0C only */

    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_MISSED);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_MISSED);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_MISSED);
    CHECK(obd_live_get(&lv, 0x0D)->dropped);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    CHECK_EQ_INT((int)lv.failures, 3);
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static void test_live_a_good_read_resets_misses(void)
{
    static const uint8_t car[] = { 0x0D, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0D };
    obd_live_init(&lv, &link, want, 1);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010D", "NO DATA\r\r>", 50, 0);
    fake_io_reply(&f, "010D", "NO DATA\r\r>", 50, 0);
    fake_io_reply(&f, "010D", "41 0D 05\r\r>", 50, 0);
    fake_io_reply(&f, "010D", "NO DATA\r\r>", 50, 0);
    fake_io_reply(&f, "010D", "NO DATA\r\r>", 50, 0);
    for (int i = 0; i < 6; i++) {
        obd_live_step(&lv, &elm, NULL, NULL);
    }
    CHECK(!obd_live_get(&lv, 0x0D)->dropped);    /* never 3 in a row */
    CHECK_EQ_INT(obd_live_get(&lv, 0x0D)->misses, 2);
}

static void test_live_all_dropped_is_idle(void)
{
    static const uint8_t car[] = { 0x0D, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0D };
    obd_live_init(&lv, &link, want, 1);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    for (int i = 0; i < 3; i++) {
        fake_io_reply(&f, "010D", "UNABLE TO CONNECT\r\r>", 50, 0);
    }
    for (int i = 0; i < 4; i++) {
        obd_live_step(&lv, &elm, NULL, NULL);
    }
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_IDLE);

    static const uint8_t none[] = { 0x99 };
    obd_live_init(&lv, &link, none, 1);
    lv.volts_failed = true;
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_IDLE);
}

static void test_live_lost_and_refused_volts(void)
{
    static const uint8_t car[] = { 0x0C, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C };
    obd_live_init(&lv, &link, want, 1);
    fake_io_reply(&f, "ATRV", "?\r\r>", 10, 0);             /* clone without ATRV */
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_MISSED);
    CHECK(lv.volts_failed && !lv.have_volts);
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);   /* no more ATRV */
    /* No reply at all: the adapter is gone. */
    CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_LOST);
}

static void test_live_vacuum_from_engine_off_map(void)
{
    static const uint8_t car[] = { 0x0B, 0x0C, 0 };   /* no PID 33: the Ridgeline */
    setup(car);
    static const uint8_t want[] = { 0x0B, 0x0C };
    obd_live_init(&lv, &link, want, 2);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010B", "41 0B 64\r\r>", 50, 0);      /* key on, engine off: 100 kPa */
    fake_io_reply(&f, "010C", "41 0C 00 00\r\r>", 50, 0);
    fake_io_reply(&f, "010B", "41 0B 64\r\r>", 50, 0);      /* between two RPM 0 reads */
    fake_io_reply(&f, "010C", "41 0C 00 00\r\r>", 50, 0);
    fake_io_reply(&f, "010B", "41 0B 21\r\r>", 50, 0);      /* running: 33 kPa */
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);
    float vac = 0.0f;
    for (int i = 0; i < 3; i++) {
        obd_live_step(&lv, &elm, NULL, NULL);
    }
    CHECK(!lv.have_engine_off_map);                        /* first MAP came before any RPM */
    CHECK(!obd_live_vacuum(&lv, &vac));
    obd_live_step(&lv, &elm, NULL, NULL);
    obd_live_step(&lv, &elm, NULL, NULL);
    CHECK(lv.have_engine_off_map);
    CHECK_NEAR(lv.engine_off_map_kpa, 100.0f);
    obd_live_step(&lv, &elm, NULL, NULL);
    obd_live_step(&lv, &elm, NULL, NULL);
    CHECK(obd_live_vacuum(&lv, &vac));
    CHECK_NEAR(vac, 67.0f);
    CHECK_NEAR(lv.engine_off_map_kpa, 100.0f);             /* kept while running */
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static void test_live_engine_starting_is_not_baro(void)
{
    static const uint8_t car[] = { 0x0B, 0x0C, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C, 0x0B };
    obd_live_init(&lv, &link, want, 2);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010C", "41 0C 00 00\r\r>", 50, 0);   /* off */
    fake_io_reply(&f, "010B", "41 0B 21\r\r>", 50, 0);      /* engine just started */
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);   /* running */
    for (int i = 0; i < 4; i++) {
        obd_live_step(&lv, &elm, NULL, NULL);
    }
    CHECK(!lv.have_engine_off_map);
    CHECK(!obd_live_vacuum(&lv, NULL));
}

static void test_live_vacuum_from_baro_pid(void)
{
    static const uint8_t car[] = { 0x0B, 0x33, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0B, 0x33 };
    obd_live_init(&lv, &link, want, 2);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010B", "41 0B 21\r\r>", 50, 0);
    fake_io_reply(&f, "0133", "41 33 65\r\r>", 50, 0);
    float vac = 0.0f;
    for (int i = 0; i < 3; i++) {
        obd_live_step(&lv, &elm, NULL, NULL);
    }
    CHECK(obd_live_vacuum(&lv, &vac));
    CHECK_NEAR(vac, 68.0f);
}

static void test_live_response_count(void)
{
    static const uint8_t car[] = { 0x0C, 0x0D, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C, 0x0D };
    obd_live_init(&lv, &link, want, 2);
    lv.use_count = true;
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010C1", "41 0C 0B B8\r\r>", 100, 0);
    fake_io_reply(&f, "010D1", "41 0D 05\r\r>", 100, 0);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    }
    float v = 0.0f;
    CHECK(obd_live_value(&lv, 0x0D, &v) && v == 5.0f);
    CHECK(!lv.count_refused);
    CHECK_EQ_INT(f.unexpected_writes, 0);

    /* An adapter that answers "?" gets the same PID plainly, then plain ones. */
    setup(car);
    obd_live_init(&lv, &link, want, 2);
    lv.use_count = true;
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010C1", "?\r\r>", 50, 0);
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 100, 0);
    fake_io_reply(&f, "010D", "41 0D 05\r\r>", 100, 0);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    }
    CHECK(lv.count_refused);
    CHECK(obd_live_value(&lv, 0x0C, &v) && v == 750.0f);
    CHECK_EQ_INT((int)lv.failures, 0);
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static void test_live_priority_refresh_times(void)
{
    /* RPM as often as possible, coolant every 15 s (the table's times). */
    static const uint8_t car[] = { 0x05, 0x0C, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C, 0x05 };
    obd_live_init(&lv, &link, want, 2);
    CHECK_EQ_INT((int)obd_live_get(&lv, 0x0C)->period_ms, 0);
    CHECK_EQ_INT((int)obd_live_get(&lv, 0x05)->period_ms, 15000);
    lv.volts_failed = true;   /* no ATRV in this script */
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 1500, 0);   /* both due: list order */
    fake_io_reply(&f, "0105", "41 05 7B\r\r>", 1500, 0);      /* read at 3 s, due at 18 s */
    for (int i = 0; i < 11; i++) {
        fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 1500, 0);
    }
    fake_io_reply(&f, "0105", "41 05 7C\r\r>", 1500, 0);      /* overdue again */
    for (int i = 0; i < 14; i++) {
        CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    }
    CHECK_EQ_INT((int)obd_live_get(&lv, 0x0C)->reads, 12);
    CHECK_EQ_INT((int)obd_live_get(&lv, 0x05)->reads, 2);
    CHECK_EQ_INT(f.unexpected_writes, 0);

    /* A refresh time of 0 for both: they take turns. */
    setup(car);
    obd_live_init(&lv, &link, want, 2);
    obd_live_set_period(&lv, 0x05, 0);
    lv.volts_failed = true;
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 100, 0);
    fake_io_reply(&f, "0105", "41 05 7B\r\r>", 100, 0);
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 100, 0);
    fake_io_reply(&f, "0105", "41 05 7B\r\r>", 100, 0);
    for (int i = 0; i < 4; i++) {
        CHECK_EQ_INT(obd_live_step(&lv, &elm, NULL, NULL), OBD_LIVE_READ);
    }
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static int traced;
static void count_trace(void *ctx, const char *cmd, const elm_response_t *resp, unsigned ms)
{
    (void)ctx;
    (void)cmd;
    (void)resp;
    (void)ms;
    traced++;
}

static void test_live_trace(void)
{
    static const uint8_t car[] = { 0x0C, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C };
    obd_live_init(&lv, &link, want, 1);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "010C", "41 0C 0B B8\r\r>", 50, 0);
    traced = 0;
    obd_live_step(&lv, &elm, count_trace, NULL);
    obd_live_step(&lv, &elm, count_trace, NULL);
    CHECK_EQ_INT(traced, 2);
}

static char lines[LIVE_VIEW_MAX][LIVE_VIEW_COLS + 1];

static bool has_line(int n, const char *text)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(lines[i], text) == 0) {
            return true;
        }
    }
    return false;
}

static void test_live_view_lines(void)
{
    int n = live_view_lines(NULL, 0, 0.0f, OBD_UNITS_US, LIVE_VIEW_COLS, lines, LIVE_VIEW_MAX);
    CHECK_EQ_STR(lines[0], "Not polling");
    CHECK(has_line(n, "No readings yet."));

    static const uint8_t car[] = { 0x05, 0x0B, 0x0C, 0x0D, 0 };
    setup(car);
    static const uint8_t want[] = { 0x0C, 0x0B, 0x0D, 0x05 };
    obd_live_init(&lv, &link, want, 4);
    fake_io_reply(&f, "ATRV", "13.2V\r\r>", 10, 0);
    fake_io_reply(&f, "010C", "41 0C 0A F0\r\r>", 500, 0);
    fake_io_reply(&f, "010B", "41 0B 1E\r\r>", 500, 0);
    fake_io_reply(&f, "010D", "NO DATA\r\r>", 500, 0);
    fake_io_reply(&f, "0105", "41 05 7B\r\r>", 500, 0);
    fake_io_reply(&f, "010C", "41 0C 0B 18\r\r>", 500, 0);
    for (int i = 0; i < 6; i++) {
        obd_live_step(&lv, &elm, NULL, NULL);
    }
    lv.pid[2].dropped = true;   /* as after 3 NO DATA */
    n = live_view_lines(&lv, 3, 1.9f, OBD_UNITS_US, LIVE_VIEW_COLS, lines, LIVE_VIEW_MAX);
    CHECK_EQ_STR(lines[0], "ISO 9141-2 (5 baud init), 1.9 requests/s");
    CHECK(has_line(n, "RPM         710 rpm          every 2 s"));
    CHECK(has_line(n, "MAP         8.9 inHg"));
    CHECK(has_line(n, "Speed       -                no data"));
    CHECK(has_line(n, "Coolant     181 F"));
    CHECK(has_line(n, "Battery     13.2 V           adapter"));
    CHECK(has_line(n, "Vacuum      -                needs engine-off MAP"));
    for (int i = 0; i < n; i++) {
        CHECK((int)strlen(lines[i]) <= LIVE_VIEW_COLS);
    }

    obd_live_init(&lv, &link, want, 0);
    n = live_view_lines(&lv, 3, 0.0f, OBD_UNITS_US, LIVE_VIEW_COLS, lines, LIVE_VIEW_MAX);
    CHECK(has_line(n, "No PIDs chosen: tap some below."));
}

static void test_live_choices_and_toggle(void)
{
    static const uint8_t car[] = { 0x0D, 0x0C, 0x05, 0x01, 0x03, 0 };
    setup(car);
    uint8_t out[CONFIG_MAX_TEST];
    int n = live_choices(&link, out, CONFIG_MAX_TEST);
    CHECK_EQ_INT(n, 3);                          /* 01 and 03 aren't live data */
    CHECK_EQ_INT(out[0], 0x05);                  /* table order */
    CHECK_EQ_INT(out[2], 0x0D);
    CHECK_EQ_INT(live_choices(&link, out, 2), 2);

    uint8_t pids[3] = { 0x0C, 0x0D };
    int count = 2;
    CHECK(live_pid_chosen(pids, count, 0x0D) && !live_pid_chosen(pids, count, 0x05));
    count = live_toggle(pids, count, 3, 0x05);   /* add at the end */
    CHECK_EQ_INT(count, 3);
    CHECK_EQ_INT(pids[2], 0x05);
    CHECK_EQ_INT(live_toggle(pids, count, 3, 0x11), 3);   /* full */
    count = live_toggle(pids, count, 3, 0x0C);   /* remove, keep order */
    CHECK_EQ_INT(count, 2);
    CHECK_EQ_INT(pids[0], 0x0D);
    CHECK_EQ_INT(pids[1], 0x05);
}

void suite_live(void)
{
    RUN_TEST(test_live_view_lines);
    RUN_TEST(test_live_choices_and_toggle);
    RUN_TEST(test_live_init_keeps_supported_known_pids);
    RUN_TEST(test_live_round_robin_and_volts);
    RUN_TEST(test_live_volts_again_after_5_s);
    RUN_TEST(test_live_drops_no_data_pids);
    RUN_TEST(test_live_a_good_read_resets_misses);
    RUN_TEST(test_live_all_dropped_is_idle);
    RUN_TEST(test_live_lost_and_refused_volts);
    RUN_TEST(test_live_vacuum_from_engine_off_map);
    RUN_TEST(test_live_engine_starting_is_not_baro);
    RUN_TEST(test_live_vacuum_from_baro_pid);
    RUN_TEST(test_live_trace);
    RUN_TEST(test_live_response_count);
    RUN_TEST(test_live_priority_refresh_times);
}
