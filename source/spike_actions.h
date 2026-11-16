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
#include <time.h>

#include "elm327/elm327.h"
#include "transport/net.h"
#include "transport/tcp_transport.h"

typedef enum {
    SPIKE_CONNECT = 0,   /* (re)connect TCP to the adapter */
    SPIKE_ATZ,           /* reset adapter */
    SPIKE_SEARCH,        /* ATSP0 then 0100: auto protocol search */
    SPIKE_PROTOCOL,      /* ATDPN */
    SPIKE_VOLTAGE,       /* ATRV */
    SPIKE_VIN,           /* 0902 */
    SPIKE_SPEED,         /* time adapter and car requests (spike_speed_test) */
    SPIKE_ACTION_COUNT
} spike_action_t;

typedef void (*spike_log_fn)(void *ctx, const char *line);

/* Writes a one-line description of the console's network link (Wi-Fi
 * state, network name, own IP) into out. */
typedef void (*spike_net_info_fn)(char *out, size_t len);

/* Makes sure the console's network link is up before a connect attempt
 * (on the 3DS: asks the system to join a saved Wi-Fi network). Writes a
 * log line into msg, or leaves it empty if there is nothing to report.
 * Returns false if the link could not be brought up. */
typedef bool (*spike_net_up_fn)(char *msg, size_t len);

typedef struct {
    tcp_transport_t *transport;
    elm_t *elm;
    spike_log_fn log;
    void *log_ctx;
    uint64_t start_ms;   /* timestamps are relative to this */
    spike_net_up_fn net_up;     /* optional, NULL after spike_init; called
                                   before each connect attempt */
    spike_net_info_fn net_info; /* optional, NULL after spike_init; logged
                                   as "Network: ..." before each connect */
} spike_t;

void spike_init(spike_t *s, tcp_transport_t *transport, elm_t *elm,
                spike_log_fn log, void *log_ctx);

void spike_run(spike_t *s, spike_action_t action);

/* Connects to the adapter unless already connected (logged like the
 * automatic reconnect). True if connected. */
bool spike_connect_if_needed(spike_t *s);

/* Logs a command and its reply ("> 0100", "< 41 00 ...", "= OK (n ms)");
 * matches obd_link_trace_fn, ctx is the spike_t. */
void spike_trace_command(void *ctx, const char *cmd, const elm_response_t *resp, unsigned ms);

/* Timestamped log line, "[  12.345] text". */
void spike_logf(spike_t *s, const char *fmt, ...);

/* Button label, e.g. "ATSP0+0100". */
const char *spike_action_label(spike_action_t action);

/* Top-screen header, e.g. "GaugeBoy 0.0.0  [CONNECTED]  BUSY: ATSP0+0100".
 * busy is the running action, or -1. Fits SPIKE_HEADER_COLS for any
 * action label. Returns out. */
/* File name for a saved log, "gaugeboy-log-20261115-114205.txt".
 * Returns out. */
char *spike_format_log_name(char *out, size_t out_len, const struct tm *when);

/* SETUP SLOT plan for the adapter's connection slot: manual IP in the
 * adapter's /24 (.11, or .12 if the adapter itself is .11), mask
 * 255.255.255.0, gateway = the adapter, primary DNS = the 3DS's own IP
 * (so GaugeBoy answers the connection test), secondary DNS 0.0.0.0.
 * False if adapter_host isn't a dotted IPv4 address. */
bool spike_plan_slot(const char *adapter_host, net_ip_settings_t *plan);

/* True if the slot already has the plan's settings. */
bool spike_slot_matches(const net_ip_settings_t *now, const net_ip_settings_t *plan);

/* NOW / NEW table for the confirmation screen, one row per line, each
 * shorter than SPIKE_COMPARE_COLS. Returns the number of lines (<= max). */
#define SPIKE_COMPARE_COLS 40
int spike_format_slot_compare(const net_ip_settings_t *now, const net_ip_settings_t *plan,
                              char lines[][SPIKE_COMPARE_COLS], int max);

#define SPIKE_HEADER_COLS 49
char *spike_format_header(char *out, size_t out_len, bool connected, int busy);

/*
 * Speed test (the SPEED TEST button): times n requests of each kind and
 * logs min / average / max: ATRV (the adapter alone, so Wi-Fi and adapter
 * time), 010C (a car request) and 010C1 (asks the adapter to return after
 * one reply instead of waiting for more ECUs), then ATRV and 010C1 again
 * with the transport's keep-awake pings on, and puts the keep-awake setting
 * back. Needs the adapter connected; times come from the adapter driver's
 * clock.
 */
void spike_speed_test(spike_t *s, int n);
#define SPIKE_SPEED_COUNT 10

#endif
