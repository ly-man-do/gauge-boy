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
    RUN_TEST(test_config_parse_adapter_section);
    RUN_TEST(test_config_ignores_other_sections_and_keys);
    RUN_TEST(test_config_bad_values_keep_defaults);
    RUN_TEST(test_config_valid_ipv4);
    RUN_TEST(test_config_file_round_trip);
    RUN_TEST(test_textlog_add_and_read);
    RUN_TEST(test_textlog_wraps_long_lines);
    RUN_TEST(test_textlog_newlines_and_bad_bytes);
    RUN_TEST(test_textlog_drops_oldest_when_full);
}
