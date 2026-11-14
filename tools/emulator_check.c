/*
 * Host integration check: runs the GaugeBoy ELM327 driver and the M0 spike
 * actions against a running ELM327 emulator over TCP.
 *
 *   tools/run-emulator.sh            (in another terminal)
 *   make emulator-check              (host 127.0.0.1, port 35000)
 *   make emulator-check EMU_HOST=192.168.1.20 EMU_PORT=35000
 *
 * Exits 0 when every check passes. Not part of `make test` / CI because it
 * needs the emulator running.
 */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elm327/elm327.h"
#include "spike_actions.h"
#include "transport/tcp_transport.h"

static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("  %s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail ? ": " : "", detail ? detail : "");
    if (!ok) {
        failures++;
    }
}

static void print_line(void *ctx, const char *line)
{
    (void)ctx;
    printf("    %s\n", line);
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    unsigned port = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 35000u;
    static tcp_transport_t tp;
    static elm_t elm;
    static elm_startup_t st;
    static elm_response_t r;
    char detail[160];

    signal(SIGPIPE, SIG_IGN);
    printf("ELM327 emulator check against %s:%u\n\n", host, port);

    if (tcp_transport_init(&tp, host, (uint16_t)port) != TP_OK) {
        printf("Invalid address %s:%u\n", host, port);
        return 2;
    }
    tp_status_t ts = tcp_transport_connect(&tp, TP_CONNECT_TIMEOUT_MS);
    check(ts == TP_OK, "TCP connect", tp_status_str(ts));
    if (ts != TP_OK) {
        printf("\nIs the emulator running? Start it with tools/run-emulator.sh\n");
        return 1;
    }

    printf("\nDriver:\n");
    elm_io_t io = tcp_transport_io(&tp);
    elm_bind(&elm, &io);

    elm_status_t s = elm_startup(&elm, &st);
    snprintf(detail, sizeof detail, "%s, id \"%s\"", elm_status_str(s), st.id);
    check(s == ELM_OK && strstr(st.id, "ELM327") != NULL, "startup ATZ/ATE0/ATL0/ATSP0", detail);

    s = elm_command(&elm, "0100", ELM_TIMEOUT_SEARCH_MS, &r);
    snprintf(detail, sizeof detail, "%s, \"%s\"", elm_status_str(s),
             r.line_count > 0 ? r.lines[0] : "");
    check(s == ELM_OK && r.line_count > 0 && strncmp(r.lines[0], "41 00", 5) == 0,
          "0100 supported PIDs", detail);

    int protocol = -1;
    bool automatic = false;
    s = elm_read_protocol(&elm, &protocol, &automatic, &r);
    snprintf(detail, sizeof detail, "%s, %X %s", elm_status_str(s), (unsigned)protocol,
             elm_protocol_name(protocol));
    check(s == ELM_OK && protocol == 6, "ATDPN is ISO 15765-4 CAN 11/500", detail);

    int mv = 0;
    s = elm_read_voltage(&elm, &mv, &r);
    snprintf(detail, sizeof detail, "%s, %d mV", elm_status_str(s), mv);
    check(s == ELM_OK && mv >= 9000 && mv <= 16000, "ATRV plausible", detail);

    s = elm_command(&elm, "0902", ELM_TIMEOUT_DEFAULT_MS, &r);
    snprintf(detail, sizeof detail, "%s, %d lines", elm_status_str(s), r.line_count);
    check(s == ELM_OK && r.line_count >= 1, "0902 VIN reply", detail);

    printf("\nSpike actions (as the 3DS app runs them):\n");
    spike_t spike;
    spike_init(&spike, &tp, &elm, print_line, NULL);
    for (int a = 0; a < SPIKE_ACTION_COUNT; a++) {
        spike_run(&spike, (spike_action_t)a);
    }
    check(tcp_transport_connected(&tp), "still connected after all spike actions", NULL);

    tcp_transport_close(&tp);
    printf("\n%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
