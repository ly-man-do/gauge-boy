#ifndef GAUGEBOY_TCP_TRANSPORT_H
#define GAUGEBOY_TCP_TRANSPORT_H

/*
 * TCP transport to the Wi-Fi adapter (or, later, an ESP32 bridge).
 *
 * Plain BSD sockets: libctru's soc service on the 3DS, POSIX sockets on the
 * host for tests. Non-blocking socket; waits ask poll() with a zero timeout
 * and sleep between asks, because on the 3DS a poll() with a timeout holds
 * the process's only session with the network service and stalls every
 * other thread's socket calls. On the 3DS, call net_init() (net.h) first.
 *
 * The ELM327 driver talks to it through tcp_transport_io(). Use from the
 * polling thread only; never from the render loop.
 *
 * Reconnect: any read / write error or peer close drops the connection.
 * Call tcp_transport_ensure_connected() before each poll cycle; it
 * reconnects at once after a drop, then backs off (min..max, doubling)
 * while attempts keep failing, e.g. while Wi-Fi is down after sleep.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elm327/elm327.h"

#define TP_DEFAULT_HOST        "192.168.0.10"
#define TP_DEFAULT_PORT        35000
#define TP_CONNECT_TIMEOUT_MS  3000u
#define TP_CONNECT_SLICE_MS    100u /* re-check a pending connect this often */
#define TP_WRITE_TIMEOUT_MS    1000u
#define TP_BACKOFF_MIN_MS      500u
#define TP_BACKOFF_MAX_MS      8000u
#define TP_HOST_MAX            16   /* dotted IPv4 "255.255.255.255" + NUL */

typedef enum {
    TP_OK = 0,
    TP_ERR_INVALID_ARG,  /* host is not a dotted IPv4 address, or port 0 */
    TP_ERR_SOCKET,       /* socket setup failed */
    TP_ERR_CONNECT,      /* connection refused / unreachable */
    TP_ERR_TIMEOUT,      /* no connection within the timeout */
    TP_ERR_BACKOFF,      /* waiting before the next reconnect attempt */
    TP_STATUS_COUNT
} tp_status_t;

typedef struct {
    char host[TP_HOST_MAX];
    uint16_t port;
    int fd;                     /* -1 when not connected */
    int last_errno;             /* errno of the last failure (positive), 0 if none / peer closed */
    const char *last_step;      /* call that failed, e.g. "socket()"; "" if none */
    unsigned backoff_min_ms;
    unsigned backoff_max_ms;
    unsigned backoff_ms;        /* delay after the last failed attempt, 0 after success */
    uint64_t next_attempt_ms;   /* ensure_connected waits until this time */
    unsigned connects;          /* successful connects so far */
} tcp_transport_t;

/* Set address and defaults; does not connect. */
tp_status_t tcp_transport_init(tcp_transport_t *t, const char *host, uint16_t port);

/* Connect now (closing any current connection), waiting at most timeout_ms. */
tp_status_t tcp_transport_connect(tcp_transport_t *t, unsigned timeout_ms);

/* TP_OK if connected; otherwise tries to connect unless still backing off. */
tp_status_t tcp_transport_ensure_connected(tcp_transport_t *t, unsigned timeout_ms);

bool tcp_transport_connected(const tcp_transport_t *t);
void tcp_transport_close(tcp_transport_t *t);

/* elm_io_t semantics: write returns 0 / -1; read returns bytes, 0 on
 * timeout, -1 on error or peer close (the connection is then closed). */
int tcp_transport_write(tcp_transport_t *t, const char *data, size_t len);
int tcp_transport_read(tcp_transport_t *t, char *buf, size_t len, unsigned timeout_ms);

/* I/O interface for the ELM327 driver; ctx is t. */
elm_io_t tcp_transport_io(tcp_transport_t *t);

/* Monotonic milliseconds. */
uint64_t tp_now_ms(void);

const char *tp_status_str(tp_status_t status);

/* errno as a positive value. libctru stores some socket errors negated
 * (socket() on a 3DS with no network sets errno to -ENETDOWN). */
int tp_normalize_errno(int err);

/* errno for a raw 3DS socket error number. libctru returns getsockopt
 * SO_ERROR unconverted (26 is EINPROGRESS there, not ETXTBSY). Unknown
 * values map to EIO. */
int tp_3ds_soc_errno(int raw);

#endif
