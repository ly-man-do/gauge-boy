/* A full DIAG scan against a scripted adapter, and the DIAG panel text. */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "fake_io.h"
#include "data/code_history.h"
#include "data/diag_view.h"
#include "data/wifi_guide.h"
#include "obd/obd_diag.h"

static fake_io_t f;
static elm_t elm;
static obd_diag_t diag;
static char lines[DIAG_VIEW_MAX][DIAG_VIEW_COLS + 1];

static void setup(void)
{
    fake_io_init(&f);
    elm_io_t io = fake_io_iface(&f);
    elm_bind(&elm, &io);
}

/* The Ridgeline's layout (ISO 9141-2, headers off) with made-up codes and VIN. */
static void script_ridgeline(void)
{
    fake_io_reply(&f, "ATZ", "ATZ\r\r\rELM327 v2.3\r\r>", 900, 0);
    fake_io_reply(&f, "ATE0", "ATE0\rOK\r\r>", 10, 0);
    fake_io_reply(&f, "ATL0", "OK\r\r>", 10, 0);
    fake_io_reply(&f, "ATSP0", "OK\r\r>", 10, 0);
    fake_io_reply(&f, "0100", "SEARCHING...\r41 00 BF BE A8 91\r\r>", 4500, 0);
    fake_io_reply(&f, "ATDPN", "A3\r\r>", 40, 0);
    fake_io_reply(&f, "0120", "41 20 80 00 00 00\r\r>", 60, 0);
    fake_io_reply(&f, "0101", "41 01 82 07 65 04\r\r>", 60, 0);
    fake_io_reply(&f, "03", "43 01 71 14 56 00 00\r\r>", 80, 0);
    fake_io_reply(&f, "07", "NO DATA\r\r>", 80, 0);
    fake_io_reply(&f, "0A", "NO DATA\r\r>", 80, 0);
    fake_io_reply(&f, "0902",
                  "49 02 01 00 00 00 32\r49 02 02 48 4A 59 4B\r49 02 03 31 36 36 31\r"
                  "49 02 04 38 48 30 30\r49 02 05 30 31 32 33\r\r>", 500, 0);
}

static int steps_seen;
static void count_steps(void *ctx, obd_diag_step_t step)
{
    (void)ctx;
    (void)step;
    steps_seen++;
}

static bool view_has(int n, const char *text)
{
    for (int i = 0; i < n; i++) {
        if (strstr(lines[i], text) != NULL) {
            return true;
        }
    }
    return false;
}

#define CHECK_VIEW(n, text)                                                    \
    do {                                                                       \
        if (!view_has((n), (text))) {                                          \
            char all_[4096] = "";                                              \
            for (int i_ = 0; i_ < (n); i_++) {                                 \
                strncat(all_, lines[i_], sizeof all_ - strlen(all_) - 2);      \
                strcat(all_, "\n");                                            \
            }                                                                  \
            gb_check_failf(__FILE__, __LINE__, "panel lacks \"%s\":\n%s", (text), all_); \
        }                                                                      \
    } while (0)

static void test_scan_ridgeline(void)
{
    setup();
    script_ridgeline();
    steps_seen = 0;
    CHECK_EQ_INT(obd_diag_scan(&elm, true, 2026, &diag, NULL, count_steps, NULL), OBD_LINK_OK);
    CHECK_EQ_INT(diag.stopped_at, OBD_DIAG_STEP_DONE);
    CHECK_EQ_INT(steps_seen, 7);
    CHECK_EQ_INT(f.unexpected_writes, 0);
    CHECK_EQ_INT(diag.link.protocol, 3);
    CHECK(diag.have_readiness && diag.readiness.mil_on);
    CHECK_EQ_INT(diag.readiness.dtc_count, 2);
    CHECK(diag.have_stored);
    CHECK_EQ_INT(diag.stored.count, 2);
    CHECK_EQ_STR(diag.stored.code[0].code, "P0171");
    CHECK_EQ_STR(diag.stored.code[1].code, "P1456");
    CHECK(diag.have_pending && diag.pending.count == 0);
    CHECK(diag.permanent_unsupported && !diag.have_permanent);
    CHECK(diag.have_vin);
    CHECK_EQ_STR(diag.vin, "2HJYK16618H000123");
    CHECK_EQ_INT(diag.model_year, 2008);
    CHECK_EQ_STR(diag.maker, "Honda");
    CHECK(diag.make >= 0);
}

static void test_panel(void)
{
    /* Uses the scan from test_scan_ridgeline. */
    int n = diag_view_lines(&diag, DIAG_TAB_STORED, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "2008 Honda  VIN 2HJYK16618H000123");
    CHECK_VIEW(n, "Protocol: ISO 9141-2 (5 baud init)");
    CHECK_VIEW(n, "Check engine light: ON (2 stored codes)");
    CHECK_VIEW(n, "Stored codes: 2");
    CHECK_VIEW(n, "P0171  System Too Lean Bank 1");
    CHECK_VIEW(n, "P1456  Evaporative Emission Control System Leak");  /* Honda text, wrapped */
    for (int i = 0; i < n; i++) {
        CHECK((int)strlen(lines[i]) <= DIAG_VIEW_COLS);
    }

    n = diag_view_lines(&diag, DIAG_TAB_PENDING, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "No Pending codes.");
    n = diag_view_lines(&diag, DIAG_TAB_PERMANENT, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "not supported by this car");
    n = diag_view_lines(&diag, DIAG_TAB_READINESS, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "Readiness: 6 ready, 1 not ready (spark ignition)");
    CHECK_VIEW(n, "Evaporative system");
    CHECK_VIEW(n, "Not ready");
    CHECK_VIEW(n, "Not supported: Heated catalyst");

    /* Before any scan. */
    static obd_diag_t empty;
    memset(&empty, 0, sizeof empty);
    n = diag_view_lines(&empty, DIAG_TAB_STORED, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "Tap SCAN");

    /* The line limit holds. */
    n = diag_view_lines(&diag, DIAG_TAB_STORED, DIAG_VIEW_COLS, lines, 3);
    CHECK_EQ_INT(n, 3);
}

static void test_scan_engine_off(void)
{
    setup();
    fake_io_reply(&f, "0100", "SEARCHING...\rUNABLE TO CONNECT\r\r>", 5600, 0);
    CHECK_EQ_INT(obd_diag_scan(&elm, false, 2026, &diag, NULL, NULL, NULL), OBD_LINK_NO_VEHICLE);
    CHECK_EQ_INT(diag.stopped_at, OBD_DIAG_STEP_CONNECT);
    CHECK(!diag.have_stored && !diag.have_vin);
}

static void test_scan_lost_midway(void)
{
    /* The adapter stops answering after the stored codes: keep what we have. */
    setup();
    fake_io_reply(&f, "0100", "41 00 BE 3E B8 10\r\r>", 300, 0);
    fake_io_reply(&f, "ATDPN", "A6\r\r>", 20, 0);
    fake_io_reply(&f, "0101", "41 01 00 07 65 00\r\r>", 40, 0);
    fake_io_reply(&f, "03", "43 00\r\r>", 40, 0);
    CHECK_EQ_INT(obd_diag_scan(&elm, false, 2026, &diag, NULL, NULL, NULL), OBD_LINK_LOST);
    CHECK_EQ_INT(diag.stopped_at, OBD_DIAG_STEP_PENDING);
    CHECK(diag.have_stored && diag.stored.count == 0);
    CHECK(diag.have_readiness && !diag.readiness.mil_on);
    int n = diag_view_lines(&diag, DIAG_TAB_STORED, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "Scan stopped");
}

static void test_paged_pieces(void)
{
    /* The scan from test_scan_ridgeline is gone (later tests replaced it):
     * scan again. */
    setup();
    script_ridgeline();
    CHECK_EQ_INT(obd_diag_scan(&elm, true, 2026, &diag, NULL, NULL, NULL), OBD_LINK_OK);

    static diag_item_t items[DIAG_ITEMS_MAX];
    int n = diag_items(&diag, DIAG_TAB_STORED, items, DIAG_ITEMS_MAX);
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_STR(items[0].row, "P0171 System Too Lean Bank 1");
    CHECK_EQ_STR(items[1].row, "P1456 Evaporative Emission Control...");
    CHECK_EQ_INT((int)strlen(items[1].row), DIAG_ROW_COLS);
    CHECK_EQ_STR(items[1].detail,
                 "P1456 (stored): Evaporative Emission Control System Leak Detected Fuel Tank System");

    CHECK_EQ_INT(diag_items(&diag, DIAG_TAB_PENDING, items, DIAG_ITEMS_MAX), 0);
    CHECK_EQ_STR(diag_empty_text(&diag, DIAG_TAB_PENDING), "No pending codes.");
    CHECK_EQ_INT(diag_items(&diag, DIAG_TAB_PERMANENT, items, DIAG_ITEMS_MAX), 0);
    CHECK(strstr(diag_empty_text(&diag, DIAG_TAB_PERMANENT), "Not supported") != NULL);

    n = diag_items(&diag, DIAG_TAB_READINESS, items, DIAG_ITEMS_MAX);
    CHECK_EQ_INT(n, 7);   /* supported monitors only */
    CHECK(strncmp(items[0].row, "Misfire", 7) == 0 && strstr(items[0].row, "Ready") != NULL);
    for (int i = 0; i < n; i++) {
        CHECK((int)strlen(items[i].row) <= DIAG_ROW_COLS);
    }

    n = diag_facts(&diag, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "Car       2008 Honda");
    CHECK_VIEW(n, "VIN       2HJYK16618H000123");
    CHECK_VIEW(n, "CEL       ON (2 stored codes)");
    CHECK_VIEW(n, "Readiness 6 ready, 1 not ready");
    CHECK_VIEW(n, "Codes     2 stored, 0 pending");

    static obd_diag_t empty;
    memset(&empty, 0, sizeof empty);
    CHECK_EQ_STR(diag_empty_text(&empty, DIAG_TAB_STORED), "Not scanned yet: tap SCAN.");
    n = diag_facts(&empty, DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "Not scanned yet");

    n = diag_wrap("P1456 (stored): Evaporative Emission Control System Leak Detected Fuel Tank System",
                  30, 2, lines, 8);
    CHECK(n >= 3);
    for (int i = 0; i < n; i++) {
        CHECK((int)strlen(lines[i]) <= 30);
    }
}

static void link_with_speed(obd_link_t *link, int protocol)
{
    memset(link, 0, sizeof *link);
    link->result = OBD_LINK_OK;
    link->protocol = protocol;
    link->ranges_read = 1;
    link->supported[0][1] = 0x08;   /* PID 0x0D (vehicle speed) */
}

static void test_clear_codes(void)
{
    obd_link_t link;
    uint8_t nrc = 0xFF;

    /* Parked, the car confirms. */
    setup();
    link_with_speed(&link, 3);
    fake_io_reply(&f, "010D", "41 0D 00\r\r>", 50, 0);
    fake_io_reply(&f, "04", "44\r\r>", 200, 0);
    CHECK_EQ_INT(obd_clear_codes(&elm, &link, &nrc, NULL, NULL), OBD_CLEAR_DONE);
    CHECK_EQ_INT(nrc, 0);
    CHECK_EQ_INT(f.unexpected_writes, 0);

    /* Moving: 04 is never sent. */
    setup();
    link_with_speed(&link, 3);
    fake_io_reply(&f, "010D", "41 0D 1E\r\r>", 50, 0);
    CHECK_EQ_INT(obd_clear_codes(&elm, &link, &nrc, NULL, NULL), OBD_CLEAR_MOVING);
    CHECK_EQ_STR(f.written, "010D\r");   /* only the speed request went out */

    /* Refused with "conditions not correct" (engine running). */
    setup();
    link_with_speed(&link, 6);
    fake_io_reply(&f, "010D", "41 0D 00\r\r>", 50, 0);
    fake_io_reply(&f, "04", "7F 04 22\r\r>", 200, 0);
    CHECK_EQ_INT(obd_clear_codes(&elm, &link, &nrc, NULL, NULL), OBD_CLEAR_REFUSED);
    CHECK_EQ_INT(nrc, 0x22);

    /* No speed PID: straight to 04; NO DATA = not confirmed. */
    setup();
    memset(&link, 0, sizeof link);
    link.protocol = 3;
    fake_io_reply(&f, "04", "NO DATA\r\r>", 200, 0);
    CHECK_EQ_INT(obd_clear_codes(&elm, &link, &nrc, NULL, NULL), OBD_CLEAR_NO_REPLY);

    /* Adapter gone. */
    setup();
    memset(&link, 0, sizeof link);
    link.protocol = 3;
    CHECK_EQ_INT(obd_clear_codes(&elm, &link, &nrc, NULL, NULL), OBD_CLEAR_LOST);
    CHECK(strstr(obd_clear_result_str(OBD_CLEAR_MOVING), "moving") != NULL);
}

static void test_code_history(void)
{
    static char csv[4096];
    setup();
    script_ridgeline();
    CHECK_EQ_INT(obd_diag_scan(&elm, true, 2026, &diag, NULL, NULL, NULL), OBD_LINK_OK);

    size_t n = code_history_scan_rows(&diag, "2026-11-15 16:10:40", csv, sizeof csv);
    CHECK(n > 0 && n == strlen(csv));
    CHECK_EQ_STR(csv,
        "2026-11-15 16:10:40,2HJYK16618H000123,2008 Honda,ON,stored,P0171,System Too Lean Bank 1\n"
        "2026-11-15 16:10:40,2HJYK16618H000123,2008 Honda,ON,stored,P1456,"
        "Evaporative Emission Control System Leak Detected Fuel Tank System\n");

    /* No codes: one "none" row. */
    diag.stored.count = 0;
    n = code_history_scan_rows(&diag, "2026-11-15 16:20:00", csv, sizeof csv);
    CHECK_EQ_STR(csv, "2026-11-15 16:20:00,2HJYK16618H000123,2008 Honda,ON,none,,\n");

    /* Nothing read: nothing written. */
    static obd_diag_t empty;
    memset(&empty, 0, sizeof empty);
    CHECK_EQ_INT((int)code_history_scan_rows(&empty, "x", csv, sizeof csv), 0);

    /* Clear row; a comma in the text gets quoted. */
    n = code_history_clear_row(&diag, "2026-11-15 16:21:00", "Not cleared, car refused", csv,
                               sizeof csv);
    CHECK_EQ_STR(csv,
        "2026-11-15 16:21:00,2HJYK16618H000123,2008 Honda,,cleared,,\"Not cleared, car refused\"\n");

    /* Too small for a whole row: nothing half-written. */
    CHECK_EQ_INT((int)code_history_clear_row(&diag, "2026-11-15 16:21:00", "x", csv, 20), 0);
    CHECK_EQ_STR(csv, "");
}

static void test_wifi_guide(void)
{
    char ip[24];
    CHECK_EQ_STR(wifi_guide_3digit("192.168.0.11", ip, sizeof ip), "192.168.000.011");
    CHECK_EQ_STR(wifi_guide_3digit("10.0.4.1", ip, sizeof ip), "010.000.004.001");
    CHECK_EQ_STR(wifi_guide_3digit("obd.local", ip, sizeof ip), "obd.local");

    for (int step = 0; step < WIFI_GUIDE_STEPS; step++) {
        CHECK(strlen(wifi_guide_title(step)) > 0);
        int n = wifi_guide_lines(step, "192.168.0.10", DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
        CHECK(n > 0 && n <= 24);   /* fits the top screen below the title */
        for (int i = 0; i < n; i++) {
            CHECK((int)strlen(lines[i]) <= DIAG_VIEW_COLS);
        }
    }
    int n = wifi_guide_lines(2, "192.168.0.10", DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "192.168.000.011");
    CHECK_VIEW(n, "192.168.000.010");
    n = wifi_guide_lines(3, "192.168.0.10", DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "Primary DNS:   192.168.000.011");
    /* An adapter at .11: the 3DS takes .12. */
    n = wifi_guide_lines(2, "10.0.0.11", DIAG_VIEW_COLS, lines, DIAG_VIEW_MAX);
    CHECK_VIEW(n, "010.000.000.012");
    CHECK_EQ_STR(wifi_guide_title(9), "");
}

void suite_diag(void)
{
    gb_suite("diag");
    RUN_TEST(test_scan_ridgeline);
    RUN_TEST(test_panel);
    RUN_TEST(test_scan_engine_off);
    RUN_TEST(test_scan_lost_midway);
    RUN_TEST(test_paged_pieces);
    RUN_TEST(test_clear_codes);
    RUN_TEST(test_code_history);
    RUN_TEST(test_wifi_guide);
}
