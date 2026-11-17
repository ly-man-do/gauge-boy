/* The CSV drive logger (source/data/drive_log). */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "data/drive_log.h"

typedef struct {
    char text[32768];
    size_t len;
    size_t limit;     /* accept at most this many bytes in all (0 = no limit) */
    int writes;
} capture_t;

static size_t capture_write(void *ctx, const char *data, size_t len)
{
    capture_t *c = ctx;
    c->writes++;
    size_t room = sizeof c->text - 1 - c->len;
    if (c->limit != 0 && c->len + len > c->limit) {
        room = c->limit > c->len ? c->limit - c->len : 0;
    }
    size_t n = len < room ? len : room;
    memcpy(c->text + c->len, data, n);
    c->len += n;
    c->text[c->len] = '\0';
    return n;
}

static obd_live_t lv;

/* RPM, speed, coolant and MAP polled; baro too, so vacuum is known. */
static void live_values(bool all)
{
    memset(&lv, 0, sizeof lv);
    static const uint8_t pids[] = { 0x0C, 0x0D, 0x05, 0x0B, 0x33 };
    for (int i = 0; i < 5; i++) {
        lv.pid[i].pid = pids[i];
    }
    lv.count = 5;
    lv.pid[0].have = true; lv.pid[0].value = 750.0f;    /* rpm */
    lv.pid[1].have = true; lv.pid[1].value = 0.0f;      /* km/h */
    lv.pid[2].have = all;  lv.pid[2].value = 83.0f;     /* C */
    lv.pid[3].have = true; lv.pid[3].value = 31.0f;     /* MAP kPa */
    lv.pid[4].have = true; lv.pid[4].value = 101.0f;    /* baro kPa */
    lv.have_volts = true;
    lv.volts = 14.1f;
}

static const drive_log_meta_t meta = {
    "0.2.0+test", "2026-11-16 17:12:30", "2023 Subaru Ascent", "4S4WMAHD0P3000001",
    "ISO 15765-4 CAN (11 bit, 500 kbaud)",
};

static bool has(const capture_t *c, const char *text)
{
    if (strstr(c->text, text) != NULL) {
        return true;
    }
    printf("    missing: [%s]\n    in: [%s]\n", text, c->text);
    return false;
}

static void test_drive_log_header_and_rows(void)
{
    static capture_t c;
    memset(&c, 0, sizeof c);
    static drive_log_t dl;
    live_values(false);
    CHECK(drive_log_open(&dl, capture_write, &c, &meta, &lv, OBD_UNITS_US, 0, 1000));
    CHECK(has(&c, "# GaugeBoy drive log\n# version,0.2.0+test\n# date,2026-11-16 17:12:30\n"));
    CHECK(has(&c, "# car,2023 Subaru Ascent\n# vin,4S4WMAHD0P3000001\n"));
    CHECK(has(&c, "# protocol,\"ISO 15765-4 CAN (11 bit, 500 kbaud)\"\n"));   /* quoted */
    CHECK(has(&c, "# units,US\n# rate_hz,5\n"));
    CHECK(has(&c, "time_s,RPM (rpm),Speed (mph),Coolant (F),MAP (inHg),Baro (inHg),"
                  "Battery (V),Vacuum (inHg)\n"));

    /* Rows every 200 ms; coolant not read yet is an empty cell. */
    CHECK(drive_log_sample(&dl, &lv, 1000));
    CHECK(!drive_log_sample(&dl, &lv, 1100));            /* not due */
    live_values(true);
    CHECK(drive_log_sample(&dl, &lv, 1200));
    CHECK_EQ_INT((int)dl.rows, 2);
    size_t before = c.len;
    CHECK(drive_log_flush(&dl));
    CHECK(c.len > before);
    CHECK(has(&c, "0.000,750,0.0,,9.2,29.8,14.10,20.7\n"));
    CHECK(has(&c, "0.200,750,0.0,181.4,9.2,29.8,14.10,20.7\n"));
    CHECK_EQ_INT((int)dl.bytes, (int)c.len);

    /* After a long gap the next row is a period from now, no catch-up. */
    CHECK(drive_log_sample(&dl, &lv, 5000));
    CHECK(!drive_log_sample(&dl, &lv, 5100));
    CHECK(drive_log_sample(&dl, &lv, 5200));
}

static void test_drive_log_metric_boost_and_no_map(void)
{
    static capture_t c;
    memset(&c, 0, sizeof c);
    static drive_log_t dl;
    live_values(true);
    lv.pid[3].value = 126.0f;                            /* 25 kPa of boost */
    CHECK(drive_log_open(&dl, capture_write, &c, &meta, &lv, OBD_UNITS_METRIC, 1000, 0));
    CHECK(has(&c, "# units,metric\n# rate_hz,1\n"));
    CHECK(has(&c, "Speed (km/h),Coolant (C),MAP (kPa)"));
    drive_log_sample(&dl, &lv, 0);
    drive_log_flush(&dl);
    CHECK(has(&c, "0.000,750,0.0,83.0,126.0,101.0,14.10,-25.0\n"));

    /* No MAP polled: no Vacuum column. */
    memset(&c, 0, sizeof c);
    lv.count = 2;
    lv.have_volts = false;
    CHECK(drive_log_open(&dl, capture_write, &c, &meta, &lv, OBD_UNITS_US, 0, 0));
    CHECK(has(&c, "time_s,RPM (rpm),Speed (mph),Battery (V)\n"));
    drive_log_sample(&dl, &lv, 0);
    drive_log_flush(&dl);
    CHECK(has(&c, "0.000,750,0.0,\n"));                  /* battery not read */
}

static void test_drive_log_buffering_and_errors(void)
{
    static capture_t c;
    memset(&c, 0, sizeof c);
    static drive_log_t dl;
    live_values(true);
    CHECK(drive_log_open(&dl, capture_write, &c, &meta, &lv, OBD_UNITS_US, 0, 0));
    int writes = c.writes;
    for (int i = 0; i < 20; i++) {
        drive_log_sample(&dl, &lv, (uint64_t)i * 200);
    }
    CHECK_EQ_INT(c.writes, writes);                      /* still in the buffer */
    for (int i = 20; i < 200; i++) {
        drive_log_sample(&dl, &lv, (uint64_t)i * 200);
    }
    CHECK(c.writes > writes);                            /* flushed when full */
    CHECK(drive_log_flush(&dl));
    CHECK_EQ_INT((int)dl.rows, 200);

    /* A short write (card full): failed, and nothing more is written. */
    memset(&c, 0, sizeof c);
    c.limit = 300;
    CHECK(!drive_log_open(&dl, capture_write, &c, &meta, &lv, OBD_UNITS_US, 0, 0) || c.len < 300);
    for (int i = 0; i < 200; i++) {
        drive_log_sample(&dl, &lv, (uint64_t)i * 200);
    }
    CHECK(!drive_log_flush(&dl));
    CHECK(dl.failed);
    CHECK(!drive_log_sample(&dl, &lv, 1000000));
}

static void test_drive_log_file_name(void)
{
    struct tm t = { 0 };
    t.tm_year = 2026 - 1900;
    t.tm_mon = 10;
    t.tm_mday = 16;
    t.tm_hour = 17;
    t.tm_min = 12;
    t.tm_sec = 30;
    char name[40];
    CHECK_EQ_STR(drive_log_file_name(name, sizeof name, &t), "drive-20261116-171230.csv");
}


/* ---------- the LOG list (source/data/drive_list) ---------- */
#include "data/drive_list.h"

static void test_drive_list_from_a_log(void)
{
    static capture_t c;
    memset(&c, 0, sizeof c);
    static drive_log_t dl;
    live_values(true);
    drive_log_open(&dl, capture_write, &c, &meta, &lv, OBD_UNITS_US, 0, 0);
    for (int i = 0; i <= 1360; i++) {                    /* 4:32 at 5 a second */
        drive_log_sample(&dl, &lv, (uint64_t)i * 200);
        drive_log_flush(&dl);
        if (c.len > sizeof c.text - 200) {
            memmove(c.text + 1024, c.text + c.len - 600, 600);   /* keep head and tail */
            c.len = 1024 + 600;
        }
    }
    static drive_info_t d;
    memset(&d, 0, sizeof d);
    snprintf(d.name, sizeof d.name, "drive-20261116-171230.csv");
    d.size = 70000;
    CHECK(drive_info_parse_head(&d, c.text, DRIVE_HEAD_BYTES));
    CHECK_EQ_STR(d.date, "2026-11-16 17:12:30");
    CHECK_EQ_STR(d.car, "2023 Subaru Ascent");
    CHECK_EQ_STR(d.protocol, "ISO 15765-4 CAN (11 bit, 500 kbaud)");   /* unquoted */
    CHECK_EQ_STR(d.version, "0.2.0+test");
    CHECK(!d.metric);
    CHECK(d.rate_hz == 5.0f);
    CHECK_EQ_INT(d.columns, 7);
    CHECK_EQ_STR(d.values, "RPM, Speed, Coolant, MAP, Baro, Battery, Vacuum");
    size_t tail = c.len > DRIVE_TAIL_BYTES ? DRIVE_TAIL_BYTES : c.len;
    d.duration_s = drive_info_parse_tail(c.text + c.len - tail, tail);
    CHECK(d.duration_s > 271.9f && d.duration_s < 272.1f);

    char row[40];
    drive_info_row(&d, row, sizeof row, 37);
    CHECK_EQ_STR(row, "11-16 17:12  2023 Subaru Ascent  4:32");
    CHECK((int)strlen(row) <= 40);
    static char lines[DRIVE_INFO_LINES][DRIVE_INFO_COLS + 1];
    int n = drive_info_lines(&d, lines, DRIVE_INFO_LINES);
    CHECK(n >= 9);
    CHECK_EQ_STR(lines[0], "File      drive-20261116-171230.csv");
    CHECK_EQ_STR(lines[2], "Car       2023 Subaru Ascent");
    bool len_ok = false, size_ok = false;
    for (int i = 0; i < n; i++) {
        len_ok |= strcmp(lines[i], "Length    4:32 (about 1361 rows at 5 a second)") == 0;
        size_ok |= strcmp(lines[i], "Size      69 KB, US units") == 0;
        CHECK((int)strlen(lines[i]) <= DRIVE_INFO_COLS);
    }
    CHECK(len_ok && size_ok);
}

static void test_drive_list_odd_files(void)
{
    static drive_info_t d;
    memset(&d, 0, sizeof d);
    snprintf(d.name, sizeof d.name, "drive-20261116-090000.csv");
    /* Not ours: listed, but marked. */
    CHECK(!drive_info_parse_head(&d, "a,b,c\n1,2,3\n", 12));
    CHECK_EQ_STR(d.date, "2026-11-16 09:00:00");          /* from the name */
    char row[40];
    drive_info_row(&d, row, sizeof row, 37);
    CHECK(strstr(row, "(not a drive log)") != NULL);
    /* Header only, no rows: length unknown. */
    const char *empty = "# GaugeBoy drive log\n# car,\ntime_s,RPM (rpm)\n";
    CHECK(drive_info_parse_head(&d, empty, strlen(empty)));
    CHECK(drive_info_parse_tail(empty, strlen(empty)) < 0.0f);
    drive_info_row(&d, row, sizeof row, 37);
    CHECK(strstr(row, "Car not known") != NULL && strstr(row, "?") != NULL);
    /* A cut-off last row: the one before counts. */
    const char *cut = "0.000,700\n0.200,710\n0.40";
    CHECK(drive_info_parse_tail(cut, strlen(cut)) == 0.2f);

    CHECK(drive_list_is_log_name("drive-20261116-171230.csv"));
    CHECK(!drive_list_is_log_name("notes.csv"));
    CHECK(!drive_list_is_log_name("drive-20261116-171230.txt"));
    char names[3][32] = { "drive-20261115-080000.csv", "drive-20261116-171230.csv",
                          "drive-20261116-090000.csv" };
    drive_list_sort(names, 3);
    CHECK_EQ_STR(names[0], "drive-20261116-171230.csv");
    CHECK_EQ_STR(names[2], "drive-20261115-080000.csv");
}

void suite_drive_log(void)
{
    RUN_TEST(test_drive_log_header_and_rows);
    RUN_TEST(test_drive_log_metric_boost_and_no_map);
    RUN_TEST(test_drive_log_buffering_and_errors);
    RUN_TEST(test_drive_log_file_name);
    RUN_TEST(test_drive_list_from_a_log);
    RUN_TEST(test_drive_list_odd_files);
}
