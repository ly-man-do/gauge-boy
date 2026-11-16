/* Tests for source/data: config.ini parsing and the on-screen text log. */
#include <stdio.h>
#include <stdlib.h>

#include "harness.h"
#include "data/config.h"
#include "data/textlog.h"

/* ---------- config ---------- */

static void test_config_defaults(void)
{
    gb_config_t c;
    config_defaults(&c);
    CHECK_EQ_STR(c.adapter_host, "192.168.0.10");
    CHECK_EQ_INT(c.adapter_port, 35000);
    CHECK_EQ_INT(c.wifi_slot, 0);
}

static void test_config_wifi_slot(void)
{
    gb_config_t c;
    config_defaults(&c);
    CHECK_EQ_INT(config_parse(&c, "[adapter]\nwifi_slot = 2\n"), 0);
    CHECK_EQ_INT(c.wifi_slot, 2);
    CHECK_EQ_INT(config_parse(&c, "[adapter]\nwifi_slot = ANY\n"), 0);
    CHECK_EQ_INT(c.wifi_slot, 0);
    CHECK_EQ_INT(config_parse(&c, "[adapter]\nwifi_slot = 3\n"), 0);
    CHECK_EQ_INT(c.wifi_slot, 3);
    /* Bad values keep the current slot and count as warnings. */
    CHECK_EQ_INT(config_parse(&c, "[adapter]\nwifi_slot = 4\nwifi_slot = 12\nwifi_slot =\n"), 3);
    CHECK_EQ_INT(c.wifi_slot, 3);
}

static void test_config_car_make(void)
{
    gb_config_t c;
    config_defaults(&c);
    CHECK_EQ_STR(c.car_make, "");
    CHECK_EQ_INT(config_parse(&c, "[vehicle]\nmake = Geo\n"), 0);
    CHECK_EQ_STR(c.car_make, "Geo");
    CHECK_EQ_INT(config_parse(&c, "[vehicle]\nmake = AUTO\n"), 0);
    CHECK_EQ_STR(c.car_make, "");
    CHECK_EQ_INT(config_parse(&c, "[vehicle]\nmake = Mercedes-Benz\n"), 0);
    CHECK_EQ_STR(c.car_make, "Mercedes-Benz");
    /* too long: warning, keeps the old value; [adapter] make is ignored */
    CHECK_EQ_INT(config_parse(&c, "[vehicle]\nmake = ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
                                  "[adapter]\nmake = Honda\n"), 1);
    CHECK_EQ_STR(c.car_make, "Mercedes-Benz");

    char path[] = "gaugeboy_test_make.tmp.ini";
    strcpy(c.car_make, "Geo");
    CHECK(config_save(&c, path));
    gb_config_t back;
    int w = -1;
    CHECK(config_load(&back, path, &w));
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_STR(back.car_make, "Geo");
    c.car_make[0] = '\0';
    CHECK(config_save(&c, path));
    CHECK(config_load(&back, path, &w));
    CHECK_EQ_STR(back.car_make, "");
    remove(path);
}

static void test_config_vehicle_and_units(void)
{
    gb_config_t c;
    config_defaults(&c);
    CHECK_EQ_STR(c.car_model, "");
    CHECK_EQ_INT(c.car_year, 0);
    CHECK(!c.metric);
    CHECK_EQ_INT(config_parse(&c, "[vehicle]\nmodel = Tracker\nyear = 1996\n"
                                  "[display]\nunits = metric\n"), 0);
    CHECK_EQ_STR(c.car_model, "Tracker");
    CHECK_EQ_INT(c.car_year, 1996);
    CHECK(c.metric);
    CHECK_EQ_INT(config_parse(&c, "[vehicle]\nmodel = auto\nyear = Auto\n"
                                  "[display]\nunits = us\n"), 0);
    CHECK_EQ_STR(c.car_model, "");
    CHECK_EQ_INT(c.car_year, 0);
    CHECK(!c.metric);
    /* bad values: warnings, old values kept */
    CHECK_EQ_INT(config_parse(&c, "[vehicle]\nyear = 1979\nyear = 20x5\n"
                                  "model = ABCDEFGHIJKLMNOPQRSTUVWXYZ\n"
                                  "[display]\nunits = imperial\n"), 4);
    CHECK_EQ_INT(c.car_year, 0);
    CHECK(!c.metric);

    char path[] = "gaugeboy_test_vehicle.tmp.ini";
    strcpy(c.car_model, "Sidekick JX");
    c.car_year = 1995;
    c.metric = true;
    CHECK(config_save(&c, path));
    gb_config_t back;
    int w = -1;
    CHECK(config_load(&back, path, &w));
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_STR(back.car_model, "Sidekick JX");
    CHECK_EQ_INT(back.car_year, 1995);
    CHECK(back.metric);
    remove(path);

    char t[24] = "  Sidekick; JX#  ";
    config_clean_text(t);
    CHECK_EQ_STR(t, "Sidekick JX");
    strcpy(t, "AUTO");
    config_clean_text(t);
    CHECK_EQ_STR(t, "");
    strcpy(t, "Tab\there");
    config_clean_text(t);
    CHECK_EQ_STR(t, "Tabhere");

    CHECK_EQ_INT(config_year_step(0, 1, 2027), 1996);
    CHECK_EQ_INT(config_year_step(0, -1, 2027), 2027);
    CHECK_EQ_INT(config_year_step(2008, 1, 2027), 2009);
    CHECK_EQ_INT(config_year_step(2027, 1, 2027), 0);
    CHECK_EQ_INT(config_year_step(1996, -1, 2027), 0);
    CHECK_EQ_INT(config_year_step(1990, 1, 2027), 1991);
}

/* Keys this version doesn't know (e.g. keep_awake from 0.1.0+446ee76) are
 * ignored without a warning. */
static void test_config_live_view(void)
{
    gb_config_t c;
    config_defaults(&c);
    CHECK_EQ_STR(c.live_view, "engine");
    CHECK_EQ_INT(config_parse(&c, "[live]\nview = Warmup\n"), 0);
    CHECK_EQ_STR(c.live_view, "warmup");
    CHECK_EQ_INT(config_parse(&c, "[live]\nview =\nview = muchtoolongname\n"), 2);
    CHECK_EQ_STR(c.live_view, "warmup");
    char path[] = "gaugeboy_test_view.tmp.ini";
    CHECK(config_save(&c, path));
    gb_config_t back;
    int w = -1;
    CHECK(config_load(&back, path, &w));
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_STR(back.live_view, "warmup");
    remove(path);
}

static void test_config_unknown_key_ignored(void)
{
    gb_config_t c;
    config_defaults(&c);
    CHECK_EQ_INT(config_parse(&c, "[adapter]\nkeep_awake = on\n"), 0);
    CHECK_EQ_STR(c.adapter_host, "192.168.0.10");
}

static void test_config_live_pids(void)
{
    gb_config_t c;
    config_defaults(&c);
    CHECK_EQ_INT(c.live_pid_count, 9);
    CHECK_EQ_INT(c.live_pids[0], 0x0C);
    CHECK_EQ_INT(config_parse(&c, "[live]\npids = 0c, 0D 05,06 0c\n"), 0);
    CHECK_EQ_INT(c.live_pid_count, 4);                 /* the repeated 0C dropped */
    CHECK_EQ_INT(c.live_pids[0], 0x0C);
    CHECK_EQ_INT(c.live_pids[3], 0x06);
    CHECK_EQ_INT(config_parse(&c, "[live]\npids =\n"), 0);   /* none: LIVE polls nothing */
    CHECK_EQ_INT(c.live_pid_count, 0);
    /* Bad tokens keep the old list. */
    config_parse(&c, "[live]\npids = 0C 0D\n");
    CHECK_EQ_INT(config_parse(&c, "[live]\npids = 0C xyz\npids = 100\npids = 00\npids = 0C;0D\n"), 4);
    CHECK_EQ_INT(c.live_pid_count, 2);

    char path[] = "gaugeboy_test_live.tmp.ini";
    CHECK(config_save(&c, path));
    gb_config_t back;
    int w = -1;
    CHECK(config_load(&back, path, &w));
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_INT(back.live_pid_count, 2);
    CHECK_EQ_INT(back.live_pids[1], 0x0D);
    remove(path);
}

static void test_config_parse_adapter_section(void)
{
    gb_config_t c;
    config_defaults(&c);
    int w = config_parse(&c,
        "; comment\n"
        "# another comment\n"
        "\n"
        "[Adapter]\r\n"
        "  Host =  192.168.4.1  \r\n"
        "PORT=23\r\n");
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_STR(c.adapter_host, "192.168.4.1");
    CHECK_EQ_INT(c.adapter_port, 23);
}

static void test_config_ignores_other_sections_and_keys(void)
{
    gb_config_t c;
    config_defaults(&c);
    int w = config_parse(&c,
        "host = 10.0.0.1\n"          /* no section */
        "[display]\nhost = 10.0.0.2\n"
        "[adapter]\nunits = metric\n");
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_STR(c.adapter_host, "192.168.0.10");
}

static void test_config_bad_values_keep_defaults(void)
{
    gb_config_t c;
    config_defaults(&c);
    int w = config_parse(&c,
        "[adapter]\n"
        "host = obd.local\n"
        "port = 99999\n"
        "this line has no equals sign\n"
        "[broken section\n");
    CHECK_EQ_INT(w, 4);
    CHECK_EQ_STR(c.adapter_host, "192.168.0.10");
    CHECK_EQ_INT(c.adapter_port, 35000);

    config_parse(&c, "[adapter]\nport = 0\nport = -1\nport = 35000x\nport =\n");
    CHECK_EQ_INT(c.adapter_port, 35000);
}

static void test_config_valid_ipv4(void)
{
    CHECK(config_valid_ipv4("192.168.0.10"));
    CHECK(config_valid_ipv4("0.0.0.0"));
    CHECK(config_valid_ipv4("255.255.255.255"));
    CHECK(!config_valid_ipv4("256.1.1.1"));
    CHECK(!config_valid_ipv4("1.2.3"));
    CHECK(!config_valid_ipv4("1.2.3.4.5"));
    CHECK(!config_valid_ipv4("1..2.3"));
    CHECK(!config_valid_ipv4("1.2.3.4 "));
    CHECK(!config_valid_ipv4("0001.2.3.4"));
    CHECK(!config_valid_ipv4(""));
}

static void test_config_file_round_trip(void)
{
    /* Current folder: exists however the test binary is run. */
    char path[] = "gaugeboy_test_config.tmp.ini";
    gb_config_t c;
    int w = -1;

    remove(path);
    CHECK(!config_load(&c, path, &w));
    CHECK_EQ_STR(c.adapter_host, "192.168.0.10");

    CHECK(config_write_default(path));
    CHECK(config_load(&c, path, &w));
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_STR(c.adapter_host, "192.168.0.10");
    CHECK_EQ_INT(c.adapter_port, 35000);

    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs("[adapter]\nhost = 192.168.0.123\nport = 35001\n", f);
        fclose(f);
    }
    CHECK(config_load(&c, path, NULL));
    CHECK_EQ_STR(c.adapter_host, "192.168.0.123");
    CHECK_EQ_INT(c.adapter_port, 35001);

    /* Save and load back, including the slot. */
    c.wifi_slot = 2;
    CHECK(config_save(&c, path));
    gb_config_t back;
    CHECK(config_load(&back, path, &w));
    CHECK_EQ_INT(w, 0);
    CHECK_EQ_STR(back.adapter_host, "192.168.0.123");
    CHECK_EQ_INT(back.adapter_port, 35001);
    CHECK_EQ_INT(back.wifi_slot, 2);
    remove(path);
}

/* ---------- textlog ---------- */

static void test_textlog_add_and_read(void)
{
    static textlog_t log;
    textlog_init(&log);
    CHECK_EQ_INT(textlog_count(&log), 0);
    CHECK_EQ_STR(textlog_line(&log, 0), "");

    textlog_add(&log, "first");
    textlog_add(&log, "second");
    CHECK_EQ_INT(textlog_count(&log), 2);
    CHECK_EQ_STR(textlog_line(&log, 0), "first");
    CHECK_EQ_STR(textlog_line(&log, 1), "second");
    CHECK_EQ_STR(textlog_line(&log, 2), "");
    CHECK_EQ_STR(textlog_line(&log, -1), "");
    CHECK_EQ_INT((int)log.version, 2);
}

static void test_textlog_wraps_long_lines(void)
{
    static textlog_t log;
    char text[TEXTLOG_COLS * 2 + 10];
    for (size_t i = 0; i < sizeof text - 1; i++) {
        text[i] = (char)('a' + (int)(i % 26));
    }
    text[sizeof text - 1] = '\0';

    textlog_init(&log);
    textlog_add(&log, text);
    /* 107 chars: 49 on the first line, then 47 and 11 after the indent. */
    CHECK_EQ_INT(textlog_count(&log), 3);
    CHECK_EQ_INT((int)strlen(textlog_line(&log, 0)), TEXTLOG_COLS);
    CHECK_EQ_INT((int)strlen(textlog_line(&log, 1)), TEXTLOG_COLS);
    CHECK(strncmp(textlog_line(&log, 1), "  ", TEXTLOG_INDENT) == 0);
    CHECK(textlog_line(&log, 1)[TEXTLOG_INDENT] == text[TEXTLOG_COLS]);
    CHECK_EQ_INT((int)strlen(textlog_line(&log, 2)), TEXTLOG_INDENT + 11);
}

static void test_textlog_wraps_between_words(void)
{
    static textlog_t log;
    textlog_init(&log);
    /* The line that split "Wi-F" / "i" in Azahar. */
    textlog_add(&log, "[   0.028] Adapter 127.0.0.1:35000. Join its Wi-Fi in System Settings,");
    CHECK_EQ_INT(textlog_count(&log), 2);
    CHECK_EQ_STR(textlog_line(&log, 0), "[   0.028] Adapter 127.0.0.1:35000. Join its");
    CHECK_EQ_STR(textlog_line(&log, 1), "  Wi-Fi in System Settings,");
}

static void test_textlog_wrap_keeps_tokens_whole(void)
{
    static textlog_t log;
    textlog_init(&log);
    textlog_add(&log, "[   0.445] Not connected; connecting to 127.0.0.1:35000 ...");
    CHECK_EQ_INT(textlog_count(&log), 2);
    CHECK_EQ_STR(textlog_line(&log, 0), "[   0.445] Not connected; connecting to");
    CHECK_EQ_STR(textlog_line(&log, 1), "  127.0.0.1:35000 ...");
}

static void test_textlog_wrap_at_exact_width(void)
{
    static textlog_t log;
    char text[TEXTLOG_COLS + 16];
    memset(text, 'a', TEXTLOG_COLS);
    strcpy(text + TEXTLOG_COLS, "   bbb");

    textlog_init(&log);
    textlog_add(&log, text);
    CHECK_EQ_INT(textlog_count(&log), 2);
    CHECK_EQ_INT((int)strlen(textlog_line(&log, 0)), TEXTLOG_COLS);
    CHECK_EQ_STR(textlog_line(&log, 1), "  bbb");

    /* Only spaces after a full line: no blank continuation line. */
    strcpy(text + TEXTLOG_COLS, "     ");
    textlog_add(&log, text);
    CHECK_EQ_INT(textlog_count(&log), 3);
}

static void test_textlog_hard_split_when_space_too_early(void)
{
    static textlog_t log;
    char text[80];
    /* Only space is at index 1, inside a timestamp-like prefix. */
    memcpy(text, "[ ", 2);
    memset(text + 2, 'x', 70);
    text[72] = '\0';

    textlog_init(&log);
    textlog_add(&log, text);
    CHECK_EQ_INT(textlog_count(&log), 2);
    CHECK_EQ_INT((int)strlen(textlog_line(&log, 0)), TEXTLOG_COLS);
    CHECK_EQ_INT((int)strlen(textlog_line(&log, 1)), TEXTLOG_INDENT + 72 - TEXTLOG_COLS);
}

static void test_textlog_no_trailing_spaces_at_breaks(void)
{
    static textlog_t log;
    textlog_init(&log);
    textlog_add(&log, "aaaaaaaaaa bbbbbbbbbb cccccccccc dddddddddd     eeeeeeeeee ffff");
    for (int i = 0; i < textlog_count(&log); i++) {
        const char *line = textlog_line(&log, i);
        size_t n = strlen(line);
        CHECK(n > 0 && line[n - 1] != ' ');
        CHECK((int)n <= TEXTLOG_COLS);
    }
    CHECK_EQ_STR(textlog_line(&log, 1), "  eeeeeeeeee ffff");
}

static void test_textlog_newlines_and_bad_bytes(void)
{
    static textlog_t log;
    textlog_init(&log);
    textlog_add(&log, "one\ntwo\x01\n");
    CHECK_EQ_INT(textlog_count(&log), 2);
    CHECK_EQ_STR(textlog_line(&log, 0), "one");
    CHECK_EQ_STR(textlog_line(&log, 1), "two?");

    textlog_add(&log, "");
    CHECK_EQ_INT(textlog_count(&log), 3);
    CHECK_EQ_STR(textlog_line(&log, 2), "");
}

static void test_textlog_drops_oldest_when_full(void)
{
    static textlog_t log;
    char text[16];
    textlog_init(&log);
    for (int i = 0; i < TEXTLOG_LINES + 10; i++) {
        snprintf(text, sizeof text, "line %d", i);
        textlog_add(&log, text);
    }
    CHECK_EQ_INT(textlog_count(&log), TEXTLOG_LINES);
    CHECK_EQ_STR(textlog_line(&log, 0), "line 10");
    snprintf(text, sizeof text, "line %d", TEXTLOG_LINES + 9);
    CHECK_EQ_STR(textlog_line(&log, TEXTLOG_LINES - 1), text);
}

void suite_data(void)
{
    gb_suite("data");
    RUN_TEST(test_config_defaults);
    RUN_TEST(test_config_wifi_slot);
    RUN_TEST(test_config_car_make);
    RUN_TEST(test_config_vehicle_and_units);
    RUN_TEST(test_config_live_pids);
    RUN_TEST(test_config_unknown_key_ignored);
    RUN_TEST(test_config_live_view);
    RUN_TEST(test_config_parse_adapter_section);
    RUN_TEST(test_config_ignores_other_sections_and_keys);
    RUN_TEST(test_config_bad_values_keep_defaults);
    RUN_TEST(test_config_valid_ipv4);
    RUN_TEST(test_config_file_round_trip);
    RUN_TEST(test_textlog_add_and_read);
    RUN_TEST(test_textlog_wraps_long_lines);
    RUN_TEST(test_textlog_wraps_between_words);
    RUN_TEST(test_textlog_wrap_keeps_tokens_whole);
    RUN_TEST(test_textlog_wrap_at_exact_width);
    RUN_TEST(test_textlog_hard_split_when_space_too_early);
    RUN_TEST(test_textlog_no_trailing_spaces_at_breaks);
    RUN_TEST(test_textlog_newlines_and_bad_bytes);
    RUN_TEST(test_textlog_drops_oldest_when_full);
}
