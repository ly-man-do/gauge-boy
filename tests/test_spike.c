/* Spike actions end to end: spike_run over TCP to a scripted adapter. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <signal.h>

#include "harness.h"
#include "spike_actions.h"
#include "test_server.h"

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

    CHECK_EQ_STR(srv.received, "ATZ\rATSP0\r0100\rATDPN\rATRV\r0902\r");
    CHECK_LOGGED(&cap, "= Connected (");
    CHECK_LOGGED(&cap, "] > ATZ\n");
    CHECK_LOGGED(&cap, "] < ELM327 V1.5\n");
    CHECK_LOGGED(&cap, "raw: ATZ\\r\\r\\rELM327 v1.5\\r\\r\n");
    CHECK_LOGGED(&cap, "] < 41 00 BE 3E B8 11\n");
    CHECK_LOGGED(&cap, "= Protocol 6: ISO 15765-4 CAN (11 bit, 500 kbaud) (auto)");
    CHECK_LOGGED(&cap, "= 12.60 V");
    CHECK_LOGGED(&cap, "] < 0: 49 02 01 35 46 50\n");
    CHECK_LOGGED(&cap, "] < 2: 31 32 33 34 35 36 37\n");
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
    spike_run(&s, SPIKE_ATZ);
    CHECK_LOGGED(&cap, "! Not connected; auto retry in 0.");

    /* CONNECT ignores the backoff and tries at once. */
    int before = cap.lines;
    spike_run(&s, SPIKE_CONNECT);
    CHECK(cap.lines == before + 2);
    CHECK_LOGGED(&cap, "Connecting to 127.0.0.1:");
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

void suite_spike(void)
{
    signal(SIGPIPE, SIG_IGN);
    gb_suite("spike");
    RUN_TEST(test_spike_all_buttons_against_adapter);
    RUN_TEST(test_spike_auto_connects_and_reports_errors);
    RUN_TEST(test_spike_unreachable_adapter_backs_off);
    RUN_TEST(test_spike_labels);
}
