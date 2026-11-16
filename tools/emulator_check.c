/*
 * Host integration check: runs the GaugeBoy ELM327 driver, a full DIAG scan
 * and the adapter test actions against a running ELM327 emulator over TCP.
 *
 *   tools/run-emulator.sh            (in another terminal)
 *   make emulator-check              (host 127.0.0.1, port 35000)
 *   make emulator-check EMU_HOST=192.168.1.20 EMU_PORT=35000 EMU_PROTOCOL=3
 *   tools/check-scenarios.sh         (starts each scenario and checks it)
 *
 * Arguments: host port [protocol]. With a protocol number (e.g. 3 for
 * ISO 9141-2) ATDPN must report it; without one any protocol passes.
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
#include "obd/dtc.h"
#include "obd/obd_diag.h"
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
    int want_protocol = argc > 3 && argv[3][0] != '\0' ? (int)strtol(argv[3], NULL, 16) : 0;
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
    if (want_protocol != 0) {
        char what[80];
        snprintf(what, sizeof what, "ATDPN is %s", elm_protocol_name(want_protocol));
        check(s == ELM_OK && protocol == want_protocol, what, detail);
    } else {
        check(s == ELM_OK && protocol > 0, "ATDPN found a protocol", detail);
    }

    int mv = 0;
    s = elm_read_voltage(&elm, &mv, &r);
    snprintf(detail, sizeof detail, "%s, %d mV", elm_status_str(s), mv);
    check(s == ELM_OK && mv >= 9000 && mv <= 16000, "ATRV plausible", detail);

    s = elm_command(&elm, "0902", ELM_TIMEOUT_DEFAULT_MS, &r);
    snprintf(detail, sizeof detail, "%s, %d lines", elm_status_str(s), r.line_count);
    check((s == ELM_OK && r.line_count >= 1) || s == ELM_ERR_NO_DATA,
          "0902 VIN reply (or NO DATA: no VIN over OBD)", detail);

    printf("\nDIAG scan (as the DIAG screen runs it):\n");
    static obd_diag_t d;
    obd_link_result_t lr = obd_diag_scan(&elm, true, 2026, &d, NULL, NULL, NULL);
    check(lr == OBD_LINK_OK && d.stopped_at == OBD_DIAG_STEP_DONE, "scan completes",
          obd_link_result_str(lr));
    if (d.link.protocol != 0) {
        snprintf(detail, sizeof detail, "%s", elm_protocol_name(d.link.protocol));
        check(want_protocol == 0 || d.link.protocol == want_protocol, "scan protocol", detail);
    }
    snprintf(detail, sizeof detail, "CEL %s, %d stored by 0101",
             d.readiness.mil_on ? "ON" : "off", d.readiness.dtc_count);
    check(d.have_readiness, "0101 CEL and readiness", detail);
    snprintf(detail, sizeof detail, "%d stored, %d pending", d.stored.count, d.pending.count);
    check(!d.have_readiness || d.stored.count == d.readiness.dtc_count,
          "stored codes match the 0101 count", detail);
    for (int i = 0; i < d.stored.count; i++) {
        printf("    stored  %s  %s\n", d.stored.code[i].code,
               obd_dtc_text_for(d.stored.code[i].code, d.make));
    }
    for (int i = 0; i < d.pending.count; i++) {
        printf("    pending %s  %s\n", d.pending.code[i].code,
               obd_dtc_text_for(d.pending.code[i].code, d.make));
    }
    if (d.have_vin) {
        /* Check digits are a North American rule: report, don't require. */
        snprintf(detail, sizeof detail, "%s, %d %s, check digit %s", d.vin, d.model_year,
                 d.maker != NULL ? d.maker : "(maker not listed)",
                 obd_vin_check_digit_ok(d.vin) ? "OK" : "doesn't match");
        check(obd_vin_chars_ok(d.vin), "VIN decoded", detail);
    } else {
        printf("    VIN not read (choose the make in SETUP on the 3DS)\n");
    }

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
