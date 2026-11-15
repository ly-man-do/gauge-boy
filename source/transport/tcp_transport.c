/* Host builds need POSIX (and BSD on macOS) declarations under -std=c11. */
#ifndef __3DS__
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#endif

#include "tcp_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __3DS__
#include <3ds.h>
#else
#include <time.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0 /* macOS: SO_NOSIGPIPE is set on the socket instead */
#endif

uint64_t tp_now_ms(void)
{
#ifdef __3DS__
    return (uint64_t)((double)svcGetSystemTick() / CPU_TICKS_PER_MSEC);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

static int clamp_timeout(unsigned ms)
{
    return ms > (unsigned)INT_MAX ? INT_MAX : (int)ms;
}

static bool parse_ipv4(const char *host, struct in_addr *out)
{
    return host != NULL && inet_pton(AF_INET, host, out) == 1;
}

tp_status_t tcp_transport_init(tcp_transport_t *t, const char *host, uint16_t port)
{
    struct in_addr addr;

    memset(t, 0, sizeof *t);
    t->fd = -1;
    t->last_step = "";
    t->backoff_min_ms = TP_BACKOFF_MIN_MS;
    t->backoff_max_ms = TP_BACKOFF_MAX_MS;

    if (host == NULL || strlen(host) >= TP_HOST_MAX || port == 0 ||
        !parse_ipv4(host, &addr)) {
        return TP_ERR_INVALID_ARG;
    }
    strcpy(t->host, host);
    t->port = port;
    return TP_OK;
}

bool tcp_transport_connected(const tcp_transport_t *t)
{
    return t->fd >= 0;
}

void tcp_transport_close(tcp_transport_t *t)
{
    if (t->fd >= 0) {
        close(t->fd);
        t->fd = -1;
    }
}

int tp_normalize_errno(int err)
{
    return err < 0 ? -err : err;
}

/* Drop the connection after an error; errno_value 0 means peer closed. */
static void drop(tcp_transport_t *t, const char *step, int errno_value)
{
    t->last_errno = tp_normalize_errno(errno_value);
    t->last_step = step;
    tcp_transport_close(t);
}

static tp_status_t fail_socket(tcp_transport_t *t, int fd, tp_status_t status,
                               const char *step, int err)
{
    t->last_errno = tp_normalize_errno(err);
    t->last_step = step;
    if (fd >= 0) {
        close(fd);
    }
    return status;
}

static void tp_sleep_ms(unsigned ms)
{
#ifdef __3DS__
    svcSleepThread((s64)ms * 1000000LL);
#else
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

int tp_3ds_soc_errno(int raw)
{
    /* Horizon's socket error numbers (POSIX names in alphabetical order),
     * as in libctru's soc_common.c. Only the ones connect() can report. */
    switch (raw < 0 ? -raw : raw) {
    case 0:  return 0;
    case 6:  return EAGAIN;
    case 7:  return EALREADY;
    case 13: return ECONNABORTED;
    case 14: return ECONNREFUSED;
    case 15: return ECONNRESET;
    case 23: return EHOSTUNREACH;
    case 26: return EINPROGRESS;
    case 30: return EISCONN;
    case 38: return ENETDOWN;
    case 39: return ENETRESET;
    case 40: return ENETUNREACH;
    case 76: return ETIMEDOUT;
    default: return EIO;
    }
}

tp_status_t tcp_transport_connect(tcp_transport_t *t, unsigned timeout_ms)
{
    struct sockaddr_in addr;

    tcp_transport_close(t);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(t->port);
    if (t->port == 0 || !parse_ipv4(t->host, &addr.sin_addr)) {
        return TP_ERR_INVALID_ARG;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return fail_socket(t, -1, TP_ERR_SOCKET, "socket()", errno);
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return fail_socket(t, fd, TP_ERR_SOCKET, "fcntl()", errno);
    }

    /* Small request / reply messages: send them at once. Failure is harmless. */
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        if (errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EALREADY) {
            return fail_socket(t, fd, TP_ERR_CONNECT, "connect()", errno);
        }

        uint64_t deadline = tp_now_ms() + timeout_ms;
        for (;;) {
            uint64_t now = tp_now_ms();
            if (now >= deadline) {
                return fail_socket(t, fd, TP_ERR_TIMEOUT, "connect()", ETIMEDOUT);
            }
            struct pollfd p = { fd, POLLOUT, 0 };
            int pr = poll(&p, 1, clamp_timeout((unsigned)(deadline - now)));
            if (pr < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return fail_socket(t, fd, TP_ERR_SOCKET, "poll()", errno);
            }
            if (pr == 0) {
                return fail_socket(t, fd, TP_ERR_TIMEOUT, "connect()", ETIMEDOUT);
            }

            int err = 0;
            socklen_t len = sizeof err;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0) {
                if (p.revents & (POLLERR | POLLHUP)) {
                    return fail_socket(t, fd, TP_ERR_CONNECT, "connect()", ECONNREFUSED);
                }
                break; /* writable and no error to read: connected */
            }
#ifdef __3DS__
            err = tp_3ds_soc_errno(err);
#endif
            if (err == 0) {
                break;
            }
            if (err != EINPROGRESS && err != EALREADY) {
                return fail_socket(t, fd, TP_ERR_CONNECT, "connect()", err);
            }
            /* Still connecting (the 3DS can wake poll early): ask connect()
             * again, which says EISCONN once done, then wait a little. */
            if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0 || errno == EISCONN) {
                break;
            }
            if (errno != EINPROGRESS && errno != EALREADY && errno != EWOULDBLOCK) {
                return fail_socket(t, fd, TP_ERR_CONNECT, "connect()", errno);
            }
            tp_sleep_ms(20);
        }
    }

    t->fd = fd;
    t->last_errno = 0;
    t->last_step = "";
    t->connects++;
    t->backoff_ms = 0;
    t->next_attempt_ms = 0;
    return TP_OK;
}

tp_status_t tcp_transport_ensure_connected(tcp_transport_t *t, unsigned timeout_ms)
{
    if (tcp_transport_connected(t)) {
        return TP_OK;
    }
    if (tp_now_ms() < t->next_attempt_ms) {
        return TP_ERR_BACKOFF;
    }

    tp_status_t st = tcp_transport_connect(t, timeout_ms);
    if (st != TP_OK) {
        if (t->backoff_ms == 0) {
            t->backoff_ms = t->backoff_min_ms;
        } else {
            t->backoff_ms = t->backoff_ms * 2u > t->backoff_max_ms
                                ? t->backoff_max_ms
                                : t->backoff_ms * 2u;
        }
        t->next_attempt_ms = tp_now_ms() + t->backoff_ms;
    }
    return st;
}

int tcp_transport_write(tcp_transport_t *t, const char *data, size_t len)
{
    if (t->fd < 0) {
        return -1;
    }

    uint64_t deadline = tp_now_ms() + TP_WRITE_TIMEOUT_MS;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(t->fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            uint64_t now = tp_now_ms();
            if (now >= deadline) {
                drop(t, "send()", ETIMEDOUT);
                return -1;
            }
            struct pollfd p = { t->fd, POLLOUT, 0 };
            (void)poll(&p, 1, clamp_timeout((unsigned)(deadline - now)));
            continue;
        }
        drop(t, "send()", n < 0 ? errno : EPIPE);
        return -1;
    }
    return 0;
}

int tcp_transport_read(tcp_transport_t *t, char *buf, size_t len, unsigned timeout_ms)
{
    if (t->fd < 0) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (len > (size_t)INT_MAX) {
        len = (size_t)INT_MAX;
    }

    struct pollfd p = { t->fd, POLLIN, 0 };
    int pr = poll(&p, 1, clamp_timeout(timeout_ms));
    if (pr == 0) {
        return 0;
    }
    if (pr < 0) {
        if (errno == EINTR) {
            return 0;
        }
        drop(t, "poll()", errno);
        return -1;
    }

    ssize_t n = recv(t->fd, buf, len, 0);
    if (n > 0) {
        return (int)n;
    }
    if (n == 0) {
        drop(t, "recv()", 0); /* peer closed */
        return -1;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return 0;
    }
    drop(t, "recv()", errno);
    return -1;
}

static int io_write(void *ctx, const char *data, size_t len)
{
    return tcp_transport_write(ctx, data, len);
}

static int io_read(void *ctx, char *buf, size_t len, unsigned timeout_ms)
{
    return tcp_transport_read(ctx, buf, len, timeout_ms);
}

static uint64_t io_now(void *ctx)
{
    (void)ctx;
    return tp_now_ms();
}

elm_io_t tcp_transport_io(tcp_transport_t *t)
{
    elm_io_t io = { t, io_write, io_read, io_now };
    return io;
}

const char *tp_status_str(tp_status_t status)
{
    switch (status) {
    case TP_OK:              return "Connected";
    case TP_ERR_INVALID_ARG: return "Invalid adapter address";
    case TP_ERR_SOCKET:      return "Network error";
    case TP_ERR_CONNECT:     return "Adapter refused or unreachable";
    case TP_ERR_TIMEOUT:     return "Adapter did not answer";
    case TP_ERR_BACKOFF:     return "Waiting to retry";
    case TP_STATUS_COUNT:    break;
    }
    return "Unknown status";
}
