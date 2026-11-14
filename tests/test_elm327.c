/* ELM327 driver tests: reply parsing, clone quirks, timeouts, init sequence. */
#include "harness.h"
#include "fake_io.h"
#include "elm327/elm327.h"

/* Parse a string literal reply (keeps embedded NUL bytes). */
#define PARSE(lit, cmd, resp) elm_parse_reply((lit), sizeof(lit) - 1, (cmd), (resp))

static void setup(fake_io_t *f, elm_t *elm)
{
    fake_io_init(f);
    elm_io_t io = fake_io_iface(f);
    elm_bind(elm, &io);
}

/* ---------- elm_parse_reply ---------- */

static void test_parse_normal_reply(void)
{
    elm_response_t r;
    PARSE("41 0C 1A F8\r\r", "010C", &r);
    CHECK_EQ_INT(r.status, ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 0C 1A F8");
    CHECK_EQ_INT(r.error_line, -1);
}

static void test_parse_strips_echo(void)
{
    elm_response_t r;
    PARSE("010C\r41 0C 1A F8\r\r", "010C", &r);
    CHECK_EQ_INT(r.status, ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 0C 1A F8");
}

static void test_parse_strips_echo_with_spaces_and_case(void)
{
    elm_response_t r;
    PARSE("01 0c\r41 0C 1A F8\r\r", "010C", &r);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 0C 1A F8");
}

static void test_parse_echo_only_checked_on_first_line(void)
{
    elm_response_t r;
    /* A later line equal to the command is data, not echo. */
    PARSE("OK\rATE0\r\r", "ATE0", &r);
    CHECK_EQ_INT(r.line_count, 2);
    CHECK_EQ_STR(r.lines[1], "ATE0");
}

static void test_parse_crlf_and_blank_lines(void)
{
    elm_response_t r;
    PARSE("\r\n\r\n41 00 BE 3E A8 13\r\n\r\n\r\n", "0100", &r);
    CHECK_EQ_INT(r.status, ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 00 BE 3E A8 13");
}

static void test_parse_trims_spaces(void)
{
    elm_response_t r;
    PARSE("   41 0D 32   \r", "010D", &r);
    CHECK_EQ_STR(r.lines[0], "41 0D 32");
}

static void test_parse_lowercase_is_uppercased(void)
{
    elm_response_t r;
    PARSE("41 0c 1a f8\r\r", "010C", &r);
    CHECK_EQ_STR(r.lines[0], "41 0C 1A F8");

    PARSE("no data\r\r", "010C", &r);
    CHECK_EQ_INT(r.status, ELM_ERR_NO_DATA);
}

static void test_parse_drops_searching(void)
{
    elm_response_t r;
    PARSE("SEARCHING...\r41 00 BE 3F A8 13\r\r", "0100", &r);
    CHECK_EQ_INT(r.status, ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 00 BE 3F A8 13");

    PARSE("searching...\rUNABLE TO CONNECT\r\r", "0100", &r);
    CHECK_EQ_INT(r.status, ELM_ERR_UNABLE_TO_CONNECT);
    CHECK_EQ_INT(r.line_count, 1);
}

static void test_parse_drops_bus_init_ok(void)
{
    elm_response_t r;
    PARSE("BUS INIT: ...OK\r41 00 BE 3E B8 11\r\r", "0100", &r);
    CHECK_EQ_INT(r.status, ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 00 BE 3E B8 11");
}

static void test_parse_removes_garbage_bytes(void)
{
    elm_response_t r;
    PARSE("\xff\xfe\x00" "41 0C\x00 1A\x01 F8\x80\r\r", "010C", &r);
    CHECK_EQ_INT(r.status, ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 0C 1A F8");
    /* raw keeps everything except NUL bytes */
    CHECK(r.raw_len == sizeof("\xff\xfe" "41 0C 1A\x01 F8\x80\r\r") - 1);
}

static void test_parse_garbage_line_before_echo(void)
{
    elm_response_t r;
    /* Clone junk after reset on its own line, then echo. */
    PARSE("\xfc\xfe\r\rATZ\r\rELM327 v1.5\r\r", "ATZ", &r);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "ELM327 V1.5");
}

static void test_parse_multiline_reply_kept_in_order(void)
{
    elm_response_t r;
    PARSE("014\r0: 49 02 01 31 48 47\r1: 59 4B 31 38 33 38 48\r2: 31 32 33 34 35 36 37\r\r",
          "0902", &r);
    CHECK_EQ_INT(r.status, ELM_OK);
    CHECK_EQ_INT(r.line_count, 4);
    CHECK_EQ_STR(r.lines[0], "014");
    CHECK_EQ_STR(r.lines[3], "2: 31 32 33 34 35 36 37");
}

static void check_error(const char *raw, elm_status_t expected)
{
    elm_response_t r;
    elm_parse_reply(raw, strlen(raw), "0100", &r);
    if (r.status != expected) {
        gb_check_failf(__FILE__, __LINE__, "reply \"%s\" gave status %d, expected %d",
                       raw, (int)r.status, (int)expected);
    }
}

static void test_parse_error_replies(void)
{
    check_error("?\r\r", ELM_ERR_UNKNOWN_COMMAND);
    check_error("NO DATA\r\r", ELM_ERR_NO_DATA);
    check_error("NODATA\r\r", ELM_ERR_NO_DATA);
    check_error("UNABLE TO CONNECT\r\r", ELM_ERR_UNABLE_TO_CONNECT);
    check_error("BUS INIT: ...ERROR\r\r", ELM_ERR_BUS_INIT);
    check_error("BUS INIT: ... ERROR\r\r", ELM_ERR_BUS_INIT);
    check_error("CAN ERROR\r\r", ELM_ERR_CAN);
    check_error("STOPPED\r\r", ELM_ERR_STOPPED);
    check_error("BUS ERROR\r\r", ELM_ERR_ADAPTER);
    check_error("BUS BUSY\r\r", ELM_ERR_ADAPTER);
    check_error("BUFFER FULL\r\r", ELM_ERR_ADAPTER);
    check_error("41 00 BE 3E <DATA ERROR\r\r", ELM_ERR_ADAPTER);
    check_error("<RX ERROR\r\r", ELM_ERR_ADAPTER);
    check_error("ERR94\r\r", ELM_ERR_ADAPTER);
    check_error("LV RESET\r\r", ELM_ERR_ADAPTER);
}

static void test_parse_error_line_index(void)
{
    elm_response_t r;
    PARSE("SEARCHING...\rCAN ERROR\r\r", "0100", &r);
    CHECK_EQ_INT(r.status, ELM_ERR_CAN);
    CHECK_EQ_INT(r.error_line, 0);
    CHECK_EQ_STR(r.lines[r.error_line], "CAN ERROR");
}

static void test_parse_too_many_lines_overflows(void)
{
    char raw[ELM_MAX_LINES * 8 + 16];
    size_t n = 0;
    for (int i = 0; i < ELM_MAX_LINES + 2; i++) {
        n += (size_t)snprintf(raw + n, sizeof raw - n, "4%d\r", i % 10);
    }
    elm_response_t r;
    elm_parse_reply(raw, n, "0100", &r);
    CHECK_EQ_INT(r.status, ELM_ERR_OVERFLOW);
    CHECK_EQ_INT(r.line_count, ELM_MAX_LINES);
}

static void test_parse_long_line_overflows(void)
{
    char raw[ELM_LINE_MAX + 20];
    memset(raw, 'A', sizeof raw);
    elm_response_t r;
    elm_parse_reply(raw, sizeof raw, "0100", &r);
    CHECK_EQ_INT(r.status, ELM_ERR_OVERFLOW);
    CHECK_EQ_INT((int)strlen(r.lines[0]), ELM_LINE_MAX - 1);
}

/* ---------- elm_command with the fake adapter ---------- */

static void test_command_writes_cr_and_reads_reply(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "010C", "41 0C 1A F8\r\r>", 20, 0);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "010C", ELM_TIMEOUT_DEFAULT_MS, &r), ELM_OK);
    CHECK_EQ_STR(f.written, "010C\r");
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 0C 1A F8");
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static void test_command_split_reads(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    /* One byte per read, with echo left on. */
    fake_io_reply(&f, "0100", "0100\rSEARCHING...\r41 00 BE 3E A8 13\r\r>", 5, 1);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "0100", ELM_TIMEOUT_SEARCH_MS, &r), ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 00 BE 3E A8 13");
}

static void test_command_slow_reply_within_timeout(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "0100", "41 00 BE 3E A8 13\r\r>", 1900, 3);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "0100", 2000, &r), ELM_OK);
}

static void test_command_no_reply_times_out(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "010C", "", 0, 0);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "010C", 1500, &r), ELM_ERR_TIMEOUT);
    CHECK_EQ_INT(r.line_count, 0);
    CHECK(f.now >= 1500);
    CHECK(f.now < 1600);
}

static void test_command_missing_prompt_times_out_keeps_partial(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATI", "ELM327 v1.5\r\r", 10, 0);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "ATI", 1000, &r), ELM_ERR_TIMEOUT);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "ELM327 V1.5");
    CHECK_EQ_INT(r.error_line, -1);
}

static void test_command_flushes_stale_bytes(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    /* A late reply from an earlier command is already waiting. */
    fake_io_inject(&f, "41 0D 00\r\r>", 0);
    fake_io_reply(&f, "010C", "41 0C 0F A0\r\r>", 10, 0);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "010C", ELM_TIMEOUT_DEFAULT_MS, &r), ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "41 0C 0F A0");
}

static void test_command_drops_bytes_after_prompt(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATE0", "OK\r\r>junk", 5, 0);
    fake_io_reply(&f, "ATL0", "OK\r\r>", 5, 0);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "ATE0", ELM_TIMEOUT_DEFAULT_MS, &r), ELM_OK);
    CHECK_EQ_INT(elm_command(&elm, "ATL0", ELM_TIMEOUT_DEFAULT_MS, &r), ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "OK");
}

static void test_command_adapter_error_status(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "0100", "SEARCHING...\rUNABLE TO CONNECT\r\r>", 50, 4);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "0100", ELM_TIMEOUT_SEARCH_MS, &r),
                 ELM_ERR_UNABLE_TO_CONNECT);
}

static void test_command_connection_closed(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    f.closed = true;

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "ATZ", ELM_TIMEOUT_RESET_MS, &r), ELM_ERR_IO);
}

static void test_command_closed_mid_reply(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATZ", "ELM3", 10, 0);
    f.close_after_write = true; /* reads fail once the partial reply is consumed */

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "ATZ", ELM_TIMEOUT_RESET_MS, &r), ELM_ERR_IO);
    CHECK_EQ_STR(f.written, "ATZ\r");
    CHECK_EQ_STR(r.raw, "ELM3");
}

static void test_command_write_failure(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    f.fail_writes = true;

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "ATZ", ELM_TIMEOUT_RESET_MS, &r), ELM_ERR_IO);
}

static void test_command_rejects_bad_commands(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "", 100, &r), ELM_ERR_INVALID_ARG);
    CHECK_EQ_INT(elm_command(&elm, NULL, 100, &r), ELM_ERR_INVALID_ARG);
    CHECK_EQ_INT(elm_command(&elm, "AT\rZ", 100, &r), ELM_ERR_INVALID_ARG);
    CHECK_EQ_INT(elm_command(&elm, "0123456789ABCDEF0123456789ABCDEF0", 100, &r),
                 ELM_ERR_INVALID_ARG);
    CHECK_EQ_INT((int)f.written_len, 0);
}

static void test_command_huge_reply_overflows_but_stays_in_sync(void)
{
    static char big[ELM_RAW_MAX + 200];
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    for (size_t i = 0; i < sizeof big - 1; i++) {
        big[i] = (i % 20 == 19) ? '\r' : 'A';
    }
    big[sizeof big - 1] = '>';
    fake_io_reply_n(&f, "0100", big, sizeof big, 5, 64);
    fake_io_reply(&f, "ATRV", "12.4V\r\r>", 5, 0);

    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "0100", ELM_TIMEOUT_DEFAULT_MS, &r), ELM_ERR_OVERFLOW);
    CHECK_EQ_INT(elm_command(&elm, "ATRV", ELM_TIMEOUT_DEFAULT_MS, &r), ELM_OK);
    CHECK_EQ_STR(r.lines[0], "12.4V");
}

/* ---------- elm_startup ---------- */

static void test_startup_happy_path_echo_on(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATZ", "ATZ\r\r\rELM327 v1.5\r\r>", 800, 4);
    fake_io_reply(&f, "ATE0", "ATE0\rOK\r\r>", 10, 0);
    fake_io_reply(&f, "ATL0", "OK\r\r>", 10, 0);
    fake_io_reply(&f, "ATSP0", "OK\r\r>", 10, 0);

    elm_startup_t s;
    CHECK_EQ_INT(elm_startup(&elm, &s), ELM_OK);
    CHECK_EQ_STR(s.id, "ELM327 V1.5");
    CHECK(s.failed_command == NULL);
    CHECK_EQ_STR(f.written, "ATZ\rATE0\rATL0\rATSP0\r");
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static void test_startup_clone_with_reset_garbage(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATZ", "\xfc\xfe\x00\r\n\r\nELM327 v2.1\r\n\r\n>", 300, 0);
    fake_io_reply(&f, "ATE0", "OK\r\n>", 10, 0);
    fake_io_reply(&f, "ATL0", "OK\r\n>", 10, 0);
    fake_io_reply(&f, "ATSP0", "OK\r>", 10, 0);

    elm_startup_t s;
    CHECK_EQ_INT(elm_startup(&elm, &s), ELM_OK);
    CHECK_EQ_STR(s.id, "ELM327 V2.1");
}

static void test_startup_stops_at_unknown_command(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATZ", "ELM327 v1.5\r\r>", 100, 0);
    fake_io_reply(&f, "ATE0", "?\r\r>", 10, 0);

    elm_startup_t s;
    CHECK_EQ_INT(elm_startup(&elm, &s), ELM_ERR_UNKNOWN_COMMAND);
    CHECK_EQ_STR(s.failed_command, "ATE0");
    CHECK_EQ_STR(f.written, "ATZ\rATE0\r");
}

static void test_startup_atz_timeout(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATZ", "", 0, 0);

    elm_startup_t s;
    CHECK_EQ_INT(elm_startup(&elm, &s), ELM_ERR_TIMEOUT);
    CHECK_EQ_STR(s.failed_command, "ATZ");
    CHECK(f.now >= ELM_TIMEOUT_RESET_MS);
}

static void test_startup_missing_ok_is_bad_reply(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATZ", "ELM327 v1.5\r\r>", 100, 0);
    fake_io_reply(&f, "ATE0", "OK\r\r>", 10, 0);
    fake_io_reply(&f, "ATL0", "\r\r>", 10, 0);

    elm_startup_t s;
    CHECK_EQ_INT(elm_startup(&elm, &s), ELM_ERR_BAD_REPLY);
    CHECK_EQ_STR(s.failed_command, "ATL0");
    CHECK_EQ_INT(s.last.status, ELM_ERR_BAD_REPLY);
}

static void test_startup_empty_atz_reply_is_bad_reply(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATZ", "\r\r>", 100, 0);

    elm_startup_t s;
    CHECK_EQ_INT(elm_startup(&elm, &s), ELM_ERR_BAD_REPLY);
    CHECK_EQ_STR(s.failed_command, "ATZ");
}

/* ---------- ATRV / ATDPN ---------- */

static void check_voltage(const char *line, bool ok, int mv)
{
    int got = -1;
    bool parsed = elm_parse_voltage(line, &got);
    if (parsed != ok || (ok && got != mv)) {
        gb_check_failf(__FILE__, __LINE__, "voltage \"%s\": parsed=%d mv=%d, expected parsed=%d mv=%d",
                       line, parsed, got, ok, mv);
    }
}

static void test_parse_voltage(void)
{
    check_voltage("12.6V", true, 12600);
    check_voltage("12,6V", true, 12600);
    check_voltage("14.25V", true, 14250);
    check_voltage("12.6", true, 12600);
    check_voltage("13V", true, 13000);
    check_voltage("12.6 V", true, 12600);
    check_voltage("11.8765V", true, 11876);
    check_voltage("V", false, 0);
    check_voltage("", false, 0);
    check_voltage("12.6VOLTS", false, 0);
    check_voltage("123V", false, 0);
}

static void test_read_voltage(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 10, 0);
    fake_io_reply(&f, "ATRV", "?\r\r>", 10, 0);
    fake_io_reply(&f, "ATRV", "HELLO\r\r>", 10, 0);

    elm_response_t r;
    int mv = 0;
    CHECK_EQ_INT(elm_read_voltage(&elm, &mv, &r), ELM_OK);
    CHECK_EQ_INT(mv, 12600);
    CHECK_EQ_INT(elm_read_voltage(&elm, &mv, &r), ELM_ERR_UNKNOWN_COMMAND);
    CHECK_EQ_INT(elm_read_voltage(&elm, &mv, &r), ELM_ERR_BAD_REPLY);
}

static void test_parse_protocol(void)
{
    int p = -1;
    bool a = false;
    CHECK(elm_parse_protocol("A6", &p, &a));
    CHECK_EQ_INT(p, 6);
    CHECK(a);
    CHECK(elm_parse_protocol("6", &p, &a));
    CHECK_EQ_INT(p, 6);
    CHECK(!a);
    CHECK(elm_parse_protocol("AC", &p, &a));
    CHECK_EQ_INT(p, 0xC);
    CHECK(a);
    CHECK(elm_parse_protocol("A", &p, &a));
    CHECK_EQ_INT(p, 0xA);
    CHECK(!a);
    CHECK(elm_parse_protocol("a7", &p, &a));
    CHECK_EQ_INT(p, 7);
    CHECK(!elm_parse_protocol("AD", &p, &a));
    CHECK(!elm_parse_protocol("Z", &p, &a));
    CHECK(!elm_parse_protocol("", &p, &a));
    CHECK(!elm_parse_protocol("A66", &p, &a));
}

static void test_read_protocol(void)
{
    fake_io_t f;
    elm_t elm;
    setup(&f, &elm);
    fake_io_reply(&f, "ATDPN", "ATDPN\rA6\r\r>", 10, 2);

    elm_response_t r;
    int p = -1;
    bool a = false;
    CHECK_EQ_INT(elm_read_protocol(&elm, &p, &a, &r), ELM_OK);
    CHECK_EQ_INT(p, 6);
    CHECK(a);
}

static void test_protocol_names(void)
{
    CHECK_EQ_STR(elm_protocol_name(0), "Automatic");
    CHECK_EQ_STR(elm_protocol_name(6), "ISO 15765-4 CAN (11 bit, 500 kbaud)");
    CHECK_EQ_STR(elm_protocol_name(0xC), "USER2 CAN");
    CHECK_EQ_STR(elm_protocol_name(13), "Unknown protocol");
    CHECK_EQ_STR(elm_protocol_name(-1), "Unknown protocol");
}

static void test_escape_raw(void)
{
    char out[64];
    static const char raw[] = "ATZ\r\nOK\\\x00\xff>";
    size_t n = elm_escape_raw(raw, sizeof raw - 1, out, sizeof out);
    CHECK_EQ_STR(out, "ATZ\\r\\nOK\\\\\\x00\\xFF>");
    CHECK_EQ_INT((int)n, (int)strlen(out));
}

static void test_escape_raw_truncates(void)
{
    char out[10];
    size_t n = elm_escape_raw("0123456789ABCDEF", 16, out, sizeof out);
    CHECK_EQ_STR(out, "012345...");
    CHECK_EQ_INT((int)n, 9);

    /* Exactly fits: no dots. */
    n = elm_escape_raw("012345678", 9, out, sizeof out);
    CHECK_EQ_STR(out, "012345678");

    char tiny[1];
    CHECK_EQ_INT((int)elm_escape_raw("AB", 2, tiny, sizeof tiny), 0);
    CHECK_EQ_STR(tiny, "");
    CHECK_EQ_INT((int)elm_escape_raw("AB", 2, out, 0), 0);
}

static void test_status_strings(void)
{
    for (int s = 0; s < ELM_STATUS_COUNT; s++) {
        const char *str = elm_status_str((elm_status_t)s);
        CHECK(str != NULL);
        CHECK(strcmp(str, "Unknown status") != 0);
    }
}

void suite_elm327(void)
{
    gb_suite("elm327");
    RUN_TEST(test_parse_normal_reply);
    RUN_TEST(test_parse_strips_echo);
    RUN_TEST(test_parse_strips_echo_with_spaces_and_case);
    RUN_TEST(test_parse_echo_only_checked_on_first_line);
    RUN_TEST(test_parse_crlf_and_blank_lines);
    RUN_TEST(test_parse_trims_spaces);
    RUN_TEST(test_parse_lowercase_is_uppercased);
    RUN_TEST(test_parse_drops_searching);
    RUN_TEST(test_parse_drops_bus_init_ok);
    RUN_TEST(test_parse_removes_garbage_bytes);
    RUN_TEST(test_parse_garbage_line_before_echo);
    RUN_TEST(test_parse_multiline_reply_kept_in_order);
    RUN_TEST(test_parse_error_replies);
    RUN_TEST(test_parse_error_line_index);
    RUN_TEST(test_parse_too_many_lines_overflows);
    RUN_TEST(test_parse_long_line_overflows);
    RUN_TEST(test_command_writes_cr_and_reads_reply);
    RUN_TEST(test_command_split_reads);
    RUN_TEST(test_command_slow_reply_within_timeout);
    RUN_TEST(test_command_no_reply_times_out);
    RUN_TEST(test_command_missing_prompt_times_out_keeps_partial);
    RUN_TEST(test_command_flushes_stale_bytes);
    RUN_TEST(test_command_drops_bytes_after_prompt);
    RUN_TEST(test_command_adapter_error_status);
    RUN_TEST(test_command_connection_closed);
    RUN_TEST(test_command_closed_mid_reply);
    RUN_TEST(test_command_write_failure);
    RUN_TEST(test_command_rejects_bad_commands);
    RUN_TEST(test_command_huge_reply_overflows_but_stays_in_sync);
    RUN_TEST(test_startup_happy_path_echo_on);
    RUN_TEST(test_startup_clone_with_reset_garbage);
    RUN_TEST(test_startup_stops_at_unknown_command);
    RUN_TEST(test_startup_atz_timeout);
    RUN_TEST(test_startup_missing_ok_is_bad_reply);
    RUN_TEST(test_startup_empty_atz_reply_is_bad_reply);
    RUN_TEST(test_parse_voltage);
    RUN_TEST(test_read_voltage);
    RUN_TEST(test_parse_protocol);
    RUN_TEST(test_read_protocol);
    RUN_TEST(test_protocol_names);
    RUN_TEST(test_escape_raw);
    RUN_TEST(test_escape_raw_truncates);
    RUN_TEST(test_status_strings);
}
