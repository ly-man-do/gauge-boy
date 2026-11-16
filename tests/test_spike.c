/* Spike actions end to end: spike_run over TCP to a scripted adapter. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <signal.h>

#include "harness.h"
#include "spike_actions.h"
#include "data/textlog.h"
#include "version.h"
#include "test_server.h"
#include "fake_io.h"

/* Collects log lines into one buffer. */
typedef struct {
    char text[16384];
    size_t len;
    int lines;
} capture_t;

static void capture(void *ctx, const char *line)
{
    capture_t *c = ctx;
    size_t n = strlen(line);
    if (c->len + n + 2 < sizeof c->text) {
        memcpy(c->text + c->len, line, n);
        c->len += n;
        c->text[c->len++] = '\n';
        c->text[c->len] = '\0';
    }
    c->lines++;
}

#define CHECK_LOGGED(cap, needle)                                            \
    do {                                                                     \
        if (strstr((cap)->text, (needle)) == NULL) {                         \
            gb_check_failf(__FILE__, __LINE__, "log lacks \"%s\"; log was:\n%s", \
                           (needle), (cap)->text);                           \
        }                                                                    \
    } while (0)

static void test_spike_all_buttons_against_adapter(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_REPLY, "ATZ\r\r\rELM327 v1.5\r\r>" },
        { S_REPLY, "ATSP0\rOK\r\r>" },
        { S_REPLY, "0100\rSEARCHING...\r41 00 BE 3E B8 11\r\r>" },
        { S_REPLY, "ATDPN\rA6\r\r>" },
        { S_REPLY, "0120\r41 20 80 00 00 00\r\r>" },
        { S_REPLY, "ATDPN\rA6\r\r>" },
        { S_REPLY, "ATRV\r12.6V\r\r>" },
        { S_REPLY, "0902\r014\r0: 49 02 01 35 46 50\r1: 59 4B 31 36 35 38 42\r2: 31 32 33 34 35 36 37\r\r>" },
        { S_WAIT_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t srv;
    server_start(&srv, steps);

    static capture_t cap;
    static tcp_transport_t tp;
    static elm_t elm;
    spike_t s;
    memset(&cap, 0, sizeof cap);
    tcp_transport_init(&tp, "127.0.0.1", srv.port);
    spike_init(&s, &tp, &elm, capture, &cap);

    spike_run(&s, SPIKE_CONNECT);
    spike_run(&s, SPIKE_ATZ);
    spike_run(&s, SPIKE_SEARCH);
    spike_run(&s, SPIKE_PROTOCOL);
    spike_run(&s, SPIKE_VOLTAGE);
    spike_run(&s, SPIKE_VIN);

    tcp_transport_close(&tp);
    server_stop(&srv);

    CHECK_EQ_STR(srv.received, "ATZ\rATSP0\r0100\rATDPN\r0120\rATDPN\rATRV\r0902\r");
    CHECK_LOGGED(&cap, "= Protocol 6: ISO 15765-4 CAN (11 bit, 500 kbaud) (auto), 1 ECU answered");
    CHECK_LOGGED(&cap, "= Supported PIDs: 01 03 04 05 06 07 0B 0C 0D 0E 0F 11 13 14 15 1C 20 21\n");
    CHECK_LOGGED(&cap, "= Connected (");
    CHECK_LOGGED(&cap, "] > ATZ\n");
    CHECK_LOGGED(&cap, "] < ELM327 V1.5\n");
    CHECK_LOGGED(&cap, "raw: ATZ\\r\\r\\rELM327 v1.5\\r\\r\n");
    CHECK_LOGGED(&cap, "] < 41 00 BE 3E B8 11\n");
    CHECK_LOGGED(&cap, "= Protocol 6: ISO 15765-4 CAN (11 bit, 500 kbaud) (auto)");
    CHECK_LOGGED(&cap, "= 12.60 V");
    CHECK_LOGGED(&cap, "] < 0: 49 02 01 35 46 50\n");
    CHECK_LOGGED(&cap, "] < 2: 31 32 33 34 35 36 37\n");
    CHECK_LOGGED(&cap, "= VIN 5FPYK1658B1234567: Honda, model year 2011");
    CHECK(strstr(cap.text, "] !") == NULL);
}

static void test_spike_auto_connects_and_reports_errors(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_REPLY, "ATSP0\rOK\r\r>" },
        { S_REPLY, "0100\rSEARCHING...\rUNABLE TO CONNECT\r\r>" },
        { S_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t srv;
    server_start(&srv, steps);

    static capture_t cap;
    static tcp_transport_t tp;
    static elm_t elm;
    spike_t s;
    memset(&cap, 0, sizeof cap);
    tcp_transport_init(&tp, "127.0.0.1", srv.port);
    spike_init(&s, &tp, &elm, capture, &cap);

    /* No CONNECT first: the command connects on its own. */
    spike_run(&s, SPIKE_SEARCH);
    CHECK_LOGGED(&cap, "Not connected; connecting to 127.0.0.1:");
    CHECK_LOGGED(&cap, "connection #1)");
    CHECK_LOGGED(&cap, "! Unable to connect to vehicle (");

    /* Adapter hung up: the next command reports the lost connection. */
    spike_run(&s, SPIKE_VOLTAGE);
    CHECK_LOGGED(&cap, "! Connection lost; the next command reconnects");

    tcp_transport_close(&tp);
    server_stop(&srv);
}

static void test_spike_unreachable_adapter_backs_off(void)
{
    static capture_t cap;
    static tcp_transport_t tp;
    static elm_t elm;
    spike_t s;
    memset(&cap, 0, sizeof cap);
    tcp_transport_init(&tp, "127.0.0.1", closed_port());
    spike_init(&s, &tp, &elm, capture, &cap);

    spike_run(&s, SPIKE_ATZ);
    CHECK_LOGGED(&cap, "! Adapter refused or unreachable (errno ");
    CHECK_LOGGED(&cap, " at connect(): ");
    spike_run(&s, SPIKE_ATZ);
    CHECK_LOGGED(&cap, "! Not connected; auto retry in 0.");

    /* CONNECT ignores the backoff and tries at once. */
    int before = cap.lines;
    spike_run(&s, SPIKE_CONNECT);
    CHECK(cap.lines == before + 2);
    CHECK_LOGGED(&cap, "Connecting to 127.0.0.1:");
}

static void fake_net_info(char *out, size_t len)
{
    snprintf(out, len, "Wi-Fi on, \"V-LINK\", 3DS IP 192.168.0.11");
}

static int net_up_calls;

static bool fake_net_up_fails(char *msg, size_t len)
{
    net_up_calls++;
    snprintf(msg, len, "Wi-Fi join failed (test)");
    return false;
}

static void test_spike_logs_network_before_connect(void)
{
    static capture_t cap;
    static tcp_transport_t tp;
    static elm_t elm;
    spike_t s;
    memset(&cap, 0, sizeof cap);
    tcp_transport_init(&tp, "127.0.0.1", closed_port());
    spike_init(&s, &tp, &elm, capture, &cap);

    /* No hook (host default): no Network line. */
    spike_run(&s, SPIKE_CONNECT);
    CHECK(strstr(cap.text, "Network:") == NULL);

    s.net_info = fake_net_info;
    spike_run(&s, SPIKE_CONNECT);
    CHECK_LOGGED(&cap, "] Network: Wi-Fi on, \"V-LINK\", 3DS IP 192.168.0.11\n");
    /* The Network line comes before the connect attempt. */
    const char *net = strstr(cap.text, "Network:");
    CHECK(net != NULL && strstr(net, "Connecting to") != NULL);
    /* net_up runs before the Network line; a failure is logged with "! ". */
    net_up_calls = 0;
    s.net_up = fake_net_up_fails;
    spike_run(&s, SPIKE_CONNECT);
    CHECK_EQ_INT(net_up_calls, 1);
    const char *join = strstr(cap.text, "] ! Wi-Fi join failed (test)\n");
    CHECK(join != NULL && strstr(join, "Network:") != NULL);
    /* errno is shown as a positive number. */
    CHECK(strstr(cap.text, "(errno -") == NULL);
}

static void test_spike_labels(void)
{
    for (int a = 0; a < SPIKE_ACTION_COUNT; a++) {
        const char *label = spike_action_label((spike_action_t)a);
        CHECK(strcmp(label, "?") != 0);
        /* Must fit a 19-column button. */
        CHECK(strlen(label) <= 19);
    }
    CHECK_EQ_STR(spike_action_label(SPIKE_SEARCH), "ATSP0+0100");
}

static void test_spike_header_fits_every_state(void)
{
    char out[128];
    /* The header is drawn on the top-screen row, which is as wide as the log. */
    CHECK_EQ_INT(SPIKE_HEADER_COLS, TEXTLOG_COLS);
    for (int connected = 0; connected <= 1; connected++) {
        for (int busy = -1; busy < SPIKE_ACTION_COUNT; busy++) {
            spike_format_header(out, sizeof out, connected != 0, busy);
            if ((int)strlen(out) > SPIKE_HEADER_COLS) {
                gb_check_failf(__FILE__, __LINE__, "header too long (%d): \"%s\"",
                               (int)strlen(out), out);
            }
        }
    }
}

static void test_spike_header_text(void)
{
    char out[128];
    char expected[128];

    snprintf(expected, sizeof expected, "GaugeBoy %s  [offline]", GB_VERSION_TEXT);
    CHECK_EQ_STR(spike_format_header(out, sizeof out, false, -1), expected);

    snprintf(expected, sizeof expected, "GaugeBoy %s  [CONNECTED]  BUSY: ATSP0+0100", GB_VERSION_TEXT);
    CHECK_EQ_STR(spike_format_header(out, sizeof out, true, SPIKE_SEARCH), expected);

    /* Out-of-range busy values show no BUSY label. */
    snprintf(expected, sizeof expected, "GaugeBoy %s  [CONNECTED]", GB_VERSION);
    CHECK_EQ_STR(spike_format_header(out, sizeof out, true, SPIKE_ACTION_COUNT), expected);
}

static void test_spike_log_name(void)
{
    char out[64];
    struct tm when;
    memset(&when, 0, sizeof when);
    when.tm_year = 2026 - 1900;
    when.tm_mon = 10;   /* November */
    when.tm_mday = 15;
    when.tm_hour = 11;
    when.tm_min = 42;
    when.tm_sec = 5;
    CHECK_EQ_STR(spike_format_log_name(out, sizeof out, &when), "gaugeboy-log-20261115-114205.txt");
    CHECK_EQ_STR(spike_format_log_name(out, sizeof out, NULL), "gaugeboy-log.txt");
}

static void test_spike_plan_slot(void)
{
    net_ip_settings_t p;
    CHECK(spike_plan_slot("192.168.0.10", &p));
    CHECK(!p.dhcp && !p.auto_dns);
    static const uint8_t ip[4] = { 192, 168, 0, 11 }, mask[4] = { 255, 255, 255, 0 },
                         gw[4] = { 192, 168, 0, 10 }, zero[4] = { 0, 0, 0, 0 };
    CHECK(memcmp(p.ip, ip, 4) == 0);
    CHECK(memcmp(p.mask, mask, 4) == 0);
    CHECK(memcmp(p.gateway, gw, 4) == 0);
    CHECK(memcmp(p.dns1, ip, 4) == 0);   /* DNS = the 3DS itself */
    CHECK(memcmp(p.dns2, zero, 4) == 0);

    /* Adapter at .11: the 3DS takes .12. */
    CHECK(spike_plan_slot("10.0.4.11", &p));
    CHECK(p.ip[0] == 10 && p.ip[2] == 4 && p.ip[3] == 12 && p.dns1[3] == 12 && p.gateway[3] == 11);

    CHECK(!spike_plan_slot("obd.local", &p));
    CHECK(!spike_plan_slot(NULL, &p));
}

static void test_spike_slot_matches_and_compare(void)
{
    net_ip_settings_t plan, now;
    CHECK(spike_plan_slot("192.168.0.10", &plan));
    now = plan;
    CHECK(spike_slot_matches(&now, &plan));
    now.auto_dns = true;
    CHECK(!spike_slot_matches(&now, &plan));

    /* Fresh slot: automatic IP and DNS, all zero. */
    memset(&now, 0, sizeof now);
    now.dhcp = true;
    now.auto_dns = true;
    char lines[8][SPIKE_COMPARE_COLS];
    int n = spike_format_slot_compare(&now, &plan, lines, 8);
    CHECK_EQ_INT(n, 6);
    CHECK_EQ_STR(lines[0], "      NOW             NEW");
    CHECK_EQ_STR(lines[1], "IP    auto            192.168.0.11");
    CHECK_EQ_STR(lines[2], "Mask  auto            255.255.255.0");
    CHECK_EQ_STR(lines[3], "Gate  auto            192.168.0.10");
    CHECK_EQ_STR(lines[4], "DNS 1 auto            192.168.0.11");
    CHECK_EQ_STR(lines[5], "DNS 2 auto            0.0.0.0");

    /* Widest values still fit the bottom screen (39 columns). */
    memset(&now, 255, sizeof now);
    now.dhcp = false;
    now.auto_dns = false;
    memset(plan.dns2, 255, 4);
    n = spike_format_slot_compare(&now, &plan, lines, 8);
    for (int i = 0; i < n; i++) {
        CHECK((int)strlen(lines[i]) <= 39);
    }
    /* max limits the lines. */
    CHECK_EQ_INT(spike_format_slot_compare(&now, &plan, lines, 2), 2);
}

static void test_spike_speed_test(void)
{
    static fake_io_t f;
    static elm_t elm;
    static tcp_transport_t unused;
    static capture_t cap;
    static spike_t s;
    memset(&cap, 0, sizeof cap);
    spike_init(&s, &unused, &elm, capture, &cap);
    fake_io_init(&f);
    elm_io_t io = fake_io_iface(&f);
    elm_bind(&elm, &io);
    unused.keep_awake_ms = 7;   /* the setting is put back afterwards */
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 300, 0);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 400, 0);
    fake_io_reply(&f, "0100", "41 00 BF BE A8 91\r\r>", 600, 0);
    fake_io_reply(&f, "010C", "41 0C 0A F0\r\r>", 600, 0);
    fake_io_reply(&f, "010C", "41 0C 0A F0\r\r>", 700, 0);
    fake_io_reply(&f, "010C1", "41 0C 0A F0\r\r>", 150, 0);
    fake_io_reply(&f, "010C1", "41 0C 0A F0\r\r>", 150, 0);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 30, 0);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 50, 0);
    fake_io_reply(&f, "010C1", "41 0C 0A F0\r\r>", 100, 0);
    fake_io_reply(&f, "010C1", "41 0C 0A F0\r\r>", 120, 0);
    spike_speed_test(&s, 2);
    CHECK_LOGGED(&cap, "ATRV (adapter only)  avg 350 ms (min 300, max 400)");
    CHECK_LOGGED(&cap, "010C (RPM)           avg 650 ms (min 600, max 700)");
    CHECK_LOGGED(&cap, "010C1 (one reply)    avg 150 ms");
    CHECK_LOGGED(&cap, "ATRV, keep-awake     avg 40 ms (min 30, max 50)");
    CHECK_LOGGED(&cap, "010C1, keep-awake    avg 110 ms (min 100, max 120)");
    CHECK_LOGGED(&cap, "= Speed (avg ms): adapter 350, car 650, one reply 150; keep-awake: adapter 40, car 110");
    CHECK_EQ_INT((int)unused.keep_awake_ms, 7);
    CHECK_EQ_INT(f.unexpected_writes, 0);

    /* A clone without the count digit: keep-awake is timed with 010C. */
    memset(&cap, 0, sizeof cap);
    fake_io_init(&f);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 300, 0);
    fake_io_reply(&f, "0100", "41 00 BF BE A8 91\r\r>", 600, 0);
    fake_io_reply(&f, "010C", "41 0C 0A F0\r\r>", 600, 0);
    fake_io_reply(&f, "010C1", "?\r\r>", 50, 0);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 30, 0);
    fake_io_reply(&f, "010C", "41 0C 0A F0\r\r>", 200, 0);
    spike_speed_test(&s, 1);
    CHECK_LOGGED(&cap, "010C1 (one reply)    not supported (?)");
    CHECK_LOGGED(&cap, "010C, keep-awake     avg 200 ms");
    CHECK_LOGGED(&cap, "one reply -; keep-awake: adapter 30, car 200");
    CHECK_EQ_INT(f.unexpected_writes, 0);

    /* The car is off: stop after ATRV, setting put back. */
    memset(&cap, 0, sizeof cap);
    fake_io_init(&f);
    fake_io_reply(&f, "ATRV", "12.6V\r\r>", 300, 0);
    fake_io_reply(&f, "0100", "NO DATA\r\r>", 600, 0);
    spike_speed_test(&s, 1);
    CHECK_LOGGED(&cap, "the car didn't answer 0100");
    CHECK_EQ_INT((int)unused.keep_awake_ms, 7);
}

void suite_spike(void)
{
    RUN_TEST(test_spike_speed_test);
    signal(SIGPIPE, SIG_IGN);
    gb_suite("spike");
    RUN_TEST(test_spike_all_buttons_against_adapter);
    RUN_TEST(test_spike_auto_connects_and_reports_errors);
    RUN_TEST(test_spike_unreachable_adapter_backs_off);
    RUN_TEST(test_spike_logs_network_before_connect);
    RUN_TEST(test_spike_labels);
    RUN_TEST(test_spike_header_fits_every_state);
    RUN_TEST(test_spike_header_text);
    RUN_TEST(test_spike_log_name);
    RUN_TEST(test_spike_plan_slot);
    RUN_TEST(test_spike_slot_matches_and_compare);
}
