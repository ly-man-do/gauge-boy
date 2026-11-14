/* TCP transport tests against a scripted local TCP server on a thread. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "harness.h"
#include "elm327/elm327.h"
#include "transport/tcp_transport.h"

/* ---------- scripted server ---------- */

typedef enum {
    S_ACCEPT,     /* accept the next connection */
    S_REPLY,      /* read a command up to CR, then send arg */
    S_CLOSE,      /* close the current connection */
    S_WAIT_CLOSE, /* keep the connection open until the client closes it */
    S_END
} step_kind_t;

typedef struct {
    step_kind_t kind;
    const char *arg;
} step_t;

typedef struct {
    int listen_fd;
    uint16_t port;
    const step_t *steps;
    pthread_t thread;
    char received[512];
    size_t received_len;
} server_t;

static void *server_main(void *arg)
{
    server_t *s = arg;
    int conn = -1;

    for (const step_t *st = s->steps; st->kind != S_END; st++) {
        switch (st->kind) {
        case S_ACCEPT:
            conn = accept(s->listen_fd, NULL, NULL);
            break;
        case S_REPLY: {
            char c;
            while (recv(conn, &c, 1, 0) == 1) {
                if (s->received_len < sizeof s->received - 1) {
                    s->received[s->received_len++] = c;
                    s->received[s->received_len] = '\0';
                }
                if (c == '\r') {
                    break;
                }
            }
            (void)send(conn, st->arg, strlen(st->arg), 0);
            break;
        }
        case S_CLOSE:
            close(conn);
            conn = -1;
            break;
        case S_WAIT_CLOSE: {
            char buf[64];
            while (recv(conn, buf, sizeof buf, 0) > 0) {
            }
            break;
        }
        case S_END:
            break;
        }
    }
    if (conn >= 0) {
        close(conn);
    }
    return NULL;
}

static void server_start(server_t *s, const step_t *steps)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    int one = 1;

    memset(s, 0, sizeof *s);
    s->steps = steps;
    s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; /* any free port */
    bind(s->listen_fd, (struct sockaddr *)&addr, sizeof addr);
    listen(s->listen_fd, 4);
    getsockname(s->listen_fd, (struct sockaddr *)&addr, &len);
    s->port = ntohs(addr.sin_port);
    pthread_create(&s->thread, NULL, server_main, s);
}

static void server_stop(server_t *s)
{
    pthread_join(s->thread, NULL);
    close(s->listen_fd);
}

/* A local port with nothing listening on it. */
static uint16_t closed_port(void)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&addr, sizeof addr);
    getsockname(fd, (struct sockaddr *)&addr, &len);
    close(fd);
    return ntohs(addr.sin_port);
}

static void sleep_ms(unsigned ms)
{
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}

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
    RUN_TEST(test_io_when_disconnected_fails);
    RUN_TEST(test_connect_and_run_elm_command);
    RUN_TEST(test_read_times_out);
    RUN_TEST(test_peer_close_drops_connection);
    RUN_TEST(test_reconnect_after_drop);
    RUN_TEST(test_connect_refused);
    RUN_TEST(test_connect_timeout_is_bounded);
    RUN_TEST(test_backoff_doubles_and_caps);
    RUN_TEST(test_backoff_resets_after_success);
    RUN_TEST(test_transport_status_strings);
}
