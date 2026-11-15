#ifndef GAUGEBOY_SPIKE_ACTIONS_H
#define GAUGEBOY_SPIKE_ACTIONS_H

/*
 * M0 spike: what each bottom-screen button does. Runs on the polling
 * thread and reports everything through a log callback (timestamped
 * text lines). No 3DS headers, so the host tests can run it over TCP.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elm327/elm327.h"
#include "transport/tcp_transport.h"

typedef enum {
    SPIKE_CONNECT = 0,   /* (re)connect TCP to the adapter */
    SPIKE_ATZ,           /* reset adapter */
    SPIKE_SEARCH,        /* ATSP0 then 0100: auto protocol search */
    SPIKE_PROTOCOL,      /* ATDPN */
    SPIKE_VOLTAGE,       /* ATRV */
    SPIKE_VIN,           /* 0902 */
    SPIKE_ACTION_COUNT
} spike_action_t;

typedef void (*spike_log_fn)(void *ctx, const char *line);

/* Writes a one-line description of the console's network link (Wi-Fi
 * state, network name, own IP) into out. */
typedef void (*spike_net_info_fn)(char *out, size_t len);

typedef struct {
    tcp_transport_t *transport;
    elm_t *elm;
    spike_log_fn log;
    void *log_ctx;
    uint64_t start_ms;   /* timestamps are relative to this */
    spike_net_info_fn net_info; /* optional, NULL after spike_init; logged
                                   as "Network: ..." before each connect */
} spike_t;

void spike_init(spike_t *s, tcp_transport_t *transport, elm_t *elm,
                spike_log_fn log, void *log_ctx);

void spike_run(spike_t *s, spike_action_t action);

/* Timestamped log line, "[  12.345] text". */
void spike_logf(spike_t *s, const char *fmt, ...);

/* Button label, e.g. "ATSP0+0100". */
const char *spike_action_label(spike_action_t action);

/* Top-screen header, e.g. "GaugeBoy 0.0.0  [CONNECTED]  BUSY: ATSP0+0100".
 * busy is the running action, or -1. Fits SPIKE_HEADER_COLS for any
 * action label. Returns out. */
#define SPIKE_HEADER_COLS 49
char *spike_format_header(char *out, size_t out_len, bool connected, int busy);

#endif
