/* Connection-test DNS and web server thread. 3DS build only. */
#include "conntest_server.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "conntest.h"

#define CT_STACK        (32 * 1024)
#define CT_POLL_MS      100
#define CT_RETRY_MS     20     /* while the network is down: catch the test's window */
#define CT_HTTP_WAIT_MS 1000
#define CT_HTTP_MAX     2048

static Thread thread;
static volatile bool quit;
static ct_server_log_fn log_fn;
static void *log_ctx;

static void logf_ct(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_ct(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (log_fn != NULL) {
        log_fn(log_ctx, line);
    }
}

static void sleep_ms(unsigned ms)
{
    svcSleepThread((s64)ms * 1000000LL);
}

static void own_ip(uint8_t ip[4])
{
    struct in_addr a;
    a.s_addr = (in_addr_t)gethostid();
    if (a.s_addr == 0 || a.s_addr == INADDR_NONE) {
        a.s_addr = htonl(INADDR_LOOPBACK);
    }
    memcpy(ip, &a.s_addr, 4);
}

static int open_socket(int type, uint16_t port, int *err)
{
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) {
        *err = errno;
        return -1;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        (type == SOCK_STREAM && listen(fd, 4) != 0)) {
        *err = errno;
        close(fd);
        return -1;
    }
    return fd;
}

static void handle_dns(int udp)
{
    uint8_t q[CT_DNS_MAX], r[CT_DNS_MAX];
    struct sockaddr_in from;
    socklen_t from_len = sizeof from;
    ssize_t n = recvfrom(udp, q, sizeof q, 0, (struct sockaddr *)&from, &from_len);
    if (n <= 0) {
        return;
    }
    uint8_t ip[4];
    own_ip(ip);
    char note[128];
    size_t rl = ct_dns_reply(q, (size_t)n, ip, r, sizeof r, note, sizeof note);
    if (rl > 0) {
        (void)sendto(udp, r, rl, 0, (struct sockaddr *)&from, from_len);
    }
    if (note[0] != '\0') {
        logf_ct("DNS %s", note);
    }
}

static void handle_http(int tcp)
{
    struct sockaddr_in from;
    socklen_t from_len = sizeof from;
    int c = accept(tcp, (struct sockaddr *)&from, &from_len);
    if (c < 0) {
        return;
    }

    char req[CT_HTTP_MAX + 1];
    size_t got = 0;
    u64 deadline = osGetTime() + CT_HTTP_WAIT_MS;
    while (got < CT_HTTP_MAX && !ct_http_request_complete(req, got) && osGetTime() < deadline) {
        struct pollfd p = { c, POLLIN, 0 };
        if (poll(&p, 1, CT_POLL_MS) <= 0) {
            continue;
        }
        ssize_t n = recv(c, req + got, CT_HTTP_MAX - got, 0);
        if (n <= 0) {
            break;
        }
        got += (size_t)n;
    }
    req[got] = '\0';

    /* Only the 3DS's connection test gets the test page; other devices on
     * the network (e.g. a file-transfer app probing port 80) get 404. */
    static bool told_others;
    bool test = ct_http_is_test_request(req);
    size_t len;
    const char *reply = test ? ct_http_reply(&len) : ct_http_not_found(&len);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(c, reply + sent, len - sent, 0);
        if (n <= 0) {
            break;
        }
        sent += (size_t)n;
    }
    close(c);

    /* Log the request line, e.g. "GET / HTTP/1.1". */
    char *eol = strpbrk(req, "\r\n");
    if (eol != NULL) {
        *eol = '\0';
    }
    if (strlen(req) > 60) {
        req[60] = '\0';
    }
    if (test) {
        logf_ct("HTTP %s -> test page%s", req, sent == len ? "" : " (send failed)");
    } else if (!told_others) {
        told_others = true;
        logf_ct("HTTP %s from another device -> 404 (not logged again)",
                got > 0 ? req : "(no request)");
    }
}

static void server_main(void *arg)
{
    (void)arg;
    int udp = -1, tcp = -1;
    int last_err = 0;
    u64 waiting_since = osGetTime();

    while (!quit) {
        if (udp < 0 || tcp < 0) {
            int err = 0;
            if (udp < 0) {
                udp = open_socket(SOCK_DGRAM, 53, &err);
            }
            if (udp >= 0 && tcp < 0) {
                tcp = open_socket(SOCK_STREAM, 80, &err);
            }
            if (udp < 0 || tcp < 0) {
                if (err != last_err && err != ENETDOWN && err != -ENETDOWN) {
                    logf_ct("! could not listen (errno %d: %s); retrying", err < 0 ? -err : err,
                            strerror(err < 0 ? -err : err));
                }
                last_err = err;
                sleep_ms(CT_RETRY_MS);
                continue;
            }
            uint8_t ip[4];
            own_ip(ip);
            logf_ct("listening on %u.%u.%u.%u (DNS 53, web 80) after %u ms",
                    ip[0], ip[1], ip[2], ip[3], (unsigned)(osGetTime() - waiting_since));
            last_err = 0;
        }

        struct pollfd p[2] = { { udp, POLLIN, 0 }, { tcp, POLLIN, 0 } };
        int pr = poll(p, 2, CT_POLL_MS);
        bool broken = pr < 0 || ((p[0].revents | p[1].revents) & (POLLERR | POLLHUP | POLLNVAL));
        if (!broken && pr > 0) {
            if (p[0].revents & POLLIN) {
                handle_dns(udp);
            }
            if (p[1].revents & POLLIN) {
                handle_http(tcp);
            }
        }
        if (broken) {
            /* Network went away (e.g. Wi-Fi rejoined): start over. */
            logf_ct("network changed; listening again when it is back");
            close(udp);
            close(tcp);
            udp = tcp = -1;
            waiting_since = osGetTime();
            sleep_ms(CT_RETRY_MS);
        }
    }
    if (udp >= 0) {
        close(udp);
    }
    if (tcp >= 0) {
        close(tcp);
    }
}

bool ct_server_start(ct_server_log_fn log, void *ctx)
{
    if (thread != NULL) {
        return true;
    }
    log_fn = log;
    log_ctx = ctx;
    quit = false;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    int p = prio + 1 > 0x3F ? 0x3F : (int)prio + 1;
    thread = threadCreate(server_main, NULL, CT_STACK, p, -2, false);
    return thread != NULL;
}

void ct_server_stop(void)
{
    if (thread == NULL) {
        return;
    }
    quit = true;
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    thread = NULL;
}
