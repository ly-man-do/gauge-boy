/* TCP transport tests against a scripted local TCP server on a thread. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <errno.h>
#include <signal.h>

#include "harness.h"
#include "elm327/elm327.h"
#include "transport/tcp_transport.h"
#include "test_server.h"

/* ---------- tests ---------- */

static void test_init_validates_address(void)
{
    tcp_transport_t t;
    CHECK_EQ_INT(tcp_transport_init(&t, "192.168.0.10", 35000), TP_OK);
    CHECK_EQ_STR(t.host, "192.168.0.10");
    CHECK(!tcp_transport_connected(&t));
    CHECK_EQ_INT(tcp_transport_init(&t, "obd.local", 35000), TP_ERR_INVALID_ARG);
    CHECK_EQ_INT(tcp_transport_init(&t, "192.168.0", 35000), TP_ERR_INVALID_ARG);
    CHECK_EQ_INT(tcp_transport_init(&t, "192.168.0.10", 0), TP_ERR_INVALID_ARG);
    CHECK_EQ_INT(tcp_transport_init(&t, NULL, 35000), TP_ERR_INVALID_ARG);
    CHECK_EQ_INT(tcp_transport_init(&t, "192.168.000.010.1", 35000), TP_ERR_INVALID_ARG);
}

static void test_normalize_errno(void)
{
    CHECK_EQ_INT(tp_normalize_errno(0), 0);
    CHECK_EQ_INT(tp_normalize_errno(ECONNREFUSED), ECONNREFUSED);
    CHECK_EQ_INT(tp_normalize_errno(-ENETDOWN), ENETDOWN); /* libctru socket() */
}

static void test_3ds_soc_errno(void)
{
    CHECK_EQ_INT(tp_3ds_soc_errno(0), 0);
    CHECK_EQ_INT(tp_3ds_soc_errno(26), EINPROGRESS); /* seen on the Vgate iCar Pro (V-LINK) test */
    CHECK_EQ_INT(tp_3ds_soc_errno(-26), EINPROGRESS);
    CHECK_EQ_INT(tp_3ds_soc_errno(14), ECONNREFUSED);
    CHECK_EQ_INT(tp_3ds_soc_errno(23), EHOSTUNREACH);
    CHECK_EQ_INT(tp_3ds_soc_errno(30), EISCONN);
    CHECK_EQ_INT(tp_3ds_soc_errno(38), ENETDOWN);
    CHECK_EQ_INT(tp_3ds_soc_errno(76), ETIMEDOUT);
    CHECK_EQ_INT(tp_3ds_soc_errno(999), EIO);
}

static void test_io_when_disconnected_fails(void)
{
    tcp_transport_t t;
    char buf[8];
    tcp_transport_init(&t, "127.0.0.1", 35000);
    CHECK_EQ_INT(tcp_transport_write(&t, "ATZ\r", 4), -1);
    CHECK_EQ_INT(tcp_transport_read(&t, buf, sizeof buf, 10), -1);
}

static void test_connect_and_run_elm_command(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_REPLY, "ATI\rELM327 v1.5\r\r>" },
        { S_WAIT_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t s;
    server_start(&s, steps);

    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", s.port);
    CHECK_EQ_INT(tcp_transport_connect(&t, 1000), TP_OK);
    CHECK(tcp_transport_connected(&t));
    CHECK_EQ_INT((int)t.connects, 1);

    elm_t elm;
    elm_io_t io = tcp_transport_io(&t);
    elm_bind(&elm, &io);
    elm_response_t r;
    CHECK_EQ_INT(elm_command(&elm, "ATI", 1000, &r), ELM_OK);
    CHECK_EQ_INT(r.line_count, 1);
    CHECK_EQ_STR(r.lines[0], "ELM327 V1.5");

    tcp_transport_close(&t);
    server_stop(&s);
    CHECK_EQ_STR(s.received, "ATI\r");
}

static void test_read_times_out(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_WAIT_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t s;
    server_start(&s, steps);

    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", s.port);
    CHECK_EQ_INT(tcp_transport_connect(&t, 1000), TP_OK);

    char buf[16];
    uint64_t start = tp_now_ms();
    CHECK_EQ_INT(tcp_transport_read(&t, buf, sizeof buf, 150), 0);
    uint64_t elapsed = tp_now_ms() - start;
    CHECK(elapsed >= 140);
    CHECK(elapsed < 1000);
    CHECK(tcp_transport_connected(&t));

    tcp_transport_close(&t);
    server_stop(&s);
}

static void test_keep_awake_pings_while_waiting(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_WAIT_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t s;
    server_start(&s, steps);

    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", s.port);
    CHECK_EQ_INT(tcp_transport_connect(&t, 1000), TP_OK);
    t.keep_awake_ms = 20;

    char buf[16];
    uint64_t start = tp_now_ms();
    CHECK_EQ_INT(tcp_transport_read(&t, buf, sizeof buf, 150), 0);   /* still times out */
    uint64_t elapsed = tp_now_ms() - start;
    CHECK(elapsed >= 140 && elapsed < 1000);
    CHECK(t.awake_pings >= 5 && t.awake_pings <= 9);   /* about one per 20 ms */
    CHECK(tcp_transport_connected(&t));

    uint32_t before = t.awake_pings;
    CHECK_EQ_INT(tcp_transport_read(&t, buf, sizeof buf, 0), 0);    /* no wait, no ping */
    CHECK_EQ_INT((int)t.awake_pings, (int)before);
    t.keep_awake_ms = 0;
    CHECK_EQ_INT(tcp_transport_read(&t, buf, sizeof buf, 50), 0);   /* off: no pings */
    CHECK_EQ_INT((int)t.awake_pings, (int)before);

    tcp_transport_close(&t);
    server_stop(&s);
}

static void test_peer_close_drops_connection(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t s;
    server_start(&s, steps);

    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", s.port);
    CHECK_EQ_INT(tcp_transport_connect(&t, 1000), TP_OK);

    char buf[16];
    CHECK_EQ_INT(tcp_transport_read(&t, buf, sizeof buf, 2000), -1);
    CHECK(!tcp_transport_connected(&t));
    CHECK_EQ_INT(t.last_errno, 0);

    server_stop(&s);
}

static void test_reconnect_after_drop(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_CLOSE, NULL },
        { S_ACCEPT, NULL },
        { S_REPLY, "OK\r\r>" },
        { S_WAIT_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t s;
    server_start(&s, steps);

    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", s.port);
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 1000), TP_OK);

    elm_t elm;
    elm_io_t io = tcp_transport_io(&t);
    elm_bind(&elm, &io);
    elm_response_t r;

    /* Server hung up: the driver sees an I/O error and the link drops. */
    CHECK_EQ_INT(elm_command(&elm, "ATZ", 2000, &r), ELM_ERR_IO);
    CHECK(!tcp_transport_connected(&t));

    /* First reconnect after a drop happens at once. */
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 1000), TP_OK);
    CHECK_EQ_INT((int)t.connects, 2);
    CHECK_EQ_INT(elm_command(&elm, "ATE0", 2000, &r), ELM_OK);
    CHECK_EQ_STR(r.lines[0], "OK");

    tcp_transport_close(&t);
    server_stop(&s);
}

static void test_connect_refused(void)
{
    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", closed_port());
    uint64_t start = tp_now_ms();
    CHECK_EQ_INT(tcp_transport_connect(&t, 2000), TP_ERR_CONNECT);
    CHECK(tp_now_ms() - start < 1000);
    CHECK(!tcp_transport_connected(&t));
    CHECK(t.last_errno != 0);
    CHECK_EQ_STR(t.last_step, "connect()");
}

static void test_connect_timeout_is_bounded(void)
{
    /* 192.0.2.1 is TEST-NET-1: never routed. Depending on the network this
     * times out or fails at once; either way it must not hang. */
    tcp_transport_t t;
    tcp_transport_init(&t, "192.0.2.1", 35000);
    uint64_t start = tp_now_ms();
    tp_status_t st = tcp_transport_connect(&t, 200);
    CHECK(st == TP_ERR_TIMEOUT || st == TP_ERR_CONNECT);
    CHECK(tp_now_ms() - start < 1500);
    CHECK(!tcp_transport_connected(&t));
}

static void test_backoff_doubles_and_caps(void)
{
    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", closed_port());
    t.backoff_min_ms = 40;
    t.backoff_max_ms = 100;

    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 500), TP_ERR_CONNECT);
    CHECK_EQ_INT((int)t.backoff_ms, 40);
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 500), TP_ERR_BACKOFF);

    sleep_ms(50);
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 500), TP_ERR_CONNECT);
    CHECK_EQ_INT((int)t.backoff_ms, 80);

    sleep_ms(90);
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 500), TP_ERR_CONNECT);
    CHECK_EQ_INT((int)t.backoff_ms, 100);

    sleep_ms(110);
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 500), TP_ERR_CONNECT);
    CHECK_EQ_INT((int)t.backoff_ms, 100);
}

static void test_backoff_resets_after_success(void)
{
    static const step_t steps[] = {
        { S_ACCEPT, NULL },
        { S_WAIT_CLOSE, NULL },
        { S_END, NULL },
    };
    server_t s;
    server_start(&s, steps);

    tcp_transport_t t;
    tcp_transport_init(&t, "127.0.0.1", s.port);
    t.backoff_ms = 800;
    t.next_attempt_ms = 0;
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 1000), TP_OK);
    CHECK_EQ_INT((int)t.backoff_ms, 0);
    CHECK_EQ_INT(tcp_transport_ensure_connected(&t, 1000), TP_OK);
    CHECK_EQ_INT((int)t.connects, 1);

    tcp_transport_close(&t);
    server_stop(&s);
}

static void test_transport_status_strings(void)
{
    for (int st = 0; st < TP_STATUS_COUNT; st++) {
        CHECK(strcmp(tp_status_str((tp_status_t)st), "Unknown status") != 0);
    }
}

void suite_transport(void)
{
    /* Writing to a socket the peer closed must not kill the test run. */
    signal(SIGPIPE, SIG_IGN);

    gb_suite("transport");
    RUN_TEST(test_init_validates_address);
    RUN_TEST(test_normalize_errno);
    RUN_TEST(test_3ds_soc_errno);
    RUN_TEST(test_io_when_disconnected_fails);
    RUN_TEST(test_connect_and_run_elm_command);
    RUN_TEST(test_read_times_out);
    RUN_TEST(test_keep_awake_pings_while_waiting);
    RUN_TEST(test_peer_close_drops_connection);
    RUN_TEST(test_reconnect_after_drop);
    RUN_TEST(test_connect_refused);
    RUN_TEST(test_connect_timeout_is_bounded);
    RUN_TEST(test_backoff_doubles_and_caps);
    RUN_TEST(test_backoff_resets_after_success);
    RUN_TEST(test_transport_status_strings);
}
