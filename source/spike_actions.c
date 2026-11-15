#include "spike_actions.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "version.h"

void spike_init(spike_t *s, tcp_transport_t *transport, elm_t *elm,
                spike_log_fn log, void *log_ctx)
{
    s->transport = transport;
    s->elm = elm;
    s->log = log;
    s->log_ctx = log_ctx;
    s->start_ms = tp_now_ms();
    s->net_info = NULL;

    elm_io_t io = tcp_transport_io(transport);
    elm_bind(elm, &io);
}

void spike_logf(spike_t *s, const char *fmt, ...)
{
    char line[1200];
    uint64_t t = tp_now_ms() - s->start_ms;
    int n = snprintf(line, sizeof line, "[%4u.%03u] ",
                     (unsigned)(t / 1000u), (unsigned)(t % 1000u));
    if (n < 0) {
        n = 0;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);

    s->log(s->log_ctx, line);
}

const char *spike_action_label(spike_action_t action)
{
    switch (action) {
    case SPIKE_CONNECT:  return "CONNECT";
    case SPIKE_ATZ:      return "ATZ";
    case SPIKE_SEARCH:   return "ATSP0+0100";
    case SPIKE_PROTOCOL: return "ATDPN";
    case SPIKE_VOLTAGE:  return "ATRV";
    case SPIKE_VIN:      return "0902 VIN";
    case SPIKE_ACTION_COUNT: break;
    }
    return "?";
}

char *spike_format_header(char *out, size_t out_len, bool connected, int busy)
{
    bool is_busy = busy >= 0 && busy < SPIKE_ACTION_COUNT;
    snprintf(out, out_len, "GaugeBoy %s  %s%s%s", GB_VERSION,
             connected ? "[CONNECTED]" : "[offline]",
             is_busy ? "  BUSY: " : "",
             is_busy ? spike_action_label((spike_action_t)busy) : "");
    return out;
}

static void log_connect_failure(spike_t *s, tp_status_t st)
{
    const tcp_transport_t *t = s->transport;
    if (t->last_errno != 0) {
        spike_logf(s, "! %s (errno %d: %s)", tp_status_str(st), t->last_errno,
                   strerror(t->last_errno));
    } else {
        spike_logf(s, "! %s", tp_status_str(st));
    }
}

static void log_net_info(spike_t *s)
{
    if (s->net_info != NULL) {
        char info[160];
        info[0] = '\0';
        s->net_info(info, sizeof info);
        spike_logf(s, "Network: %s", info);
    }
}

static void do_connect(spike_t *s)
{
    tcp_transport_t *t = s->transport;
    log_net_info(s);
    spike_logf(s, "Connecting to %s:%u ...", t->host, (unsigned)t->port);
    uint64_t t0 = tp_now_ms();
    tp_status_t st = tcp_transport_connect(t, TP_CONNECT_TIMEOUT_MS);
    if (st == TP_OK) {
        spike_logf(s, "= Connected (%u ms)", (unsigned)(tp_now_ms() - t0));
    } else {
        log_connect_failure(s, st);
    }
}

/* Connect first if needed. Returns 1 if connected. */
static int ensure_connected(spike_t *s)
{
    tcp_transport_t *t = s->transport;
    if (tcp_transport_connected(t)) {
        return 1;
    }

    uint64_t now = tp_now_ms();
    if (now < t->next_attempt_ms) {
        unsigned wait = (unsigned)(t->next_attempt_ms - now);
        spike_logf(s, "! Not connected; auto retry in %u.%u s (tap CONNECT to retry now)",
                   wait / 1000u, (wait % 1000u) / 100u);
        return 0;
    }

    log_net_info(s);
    spike_logf(s, "Not connected; connecting to %s:%u ...", t->host, (unsigned)t->port);
    uint64_t t0 = tp_now_ms();
    tp_status_t st = tcp_transport_ensure_connected(t, TP_CONNECT_TIMEOUT_MS);
    if (st != TP_OK) {
        log_connect_failure(s, st);
        return 0;
    }
    spike_logf(s, "= Connected (%u ms, connection #%u)", (unsigned)(tp_now_ms() - t0),
               t->connects);
    return 1;
}

/* Log the reply lines, the escaped raw bytes and the result. */
static void log_reply(spike_t *s, const elm_response_t *r, uint64_t t0)
{
    char escaped[1100];

    for (int i = 0; i < r->line_count; i++) {
        spike_logf(s, "< %s", r->lines[i]);
    }
    elm_escape_raw(r->raw, r->raw_len, escaped, sizeof escaped);
    spike_logf(s, "  raw: %s", escaped);

    unsigned ms = (unsigned)(tp_now_ms() - t0);
    if (r->status == ELM_OK) {
        spike_logf(s, "= OK (%u ms)", ms);
    } else {
        spike_logf(s, "! %s (%u ms)", elm_status_str(r->status), ms);
    }
    if (r->status == ELM_ERR_IO) {
        spike_logf(s, "! Connection lost; the next command reconnects");
    }
}

static elm_status_t send_logged(spike_t *s, const char *cmd, unsigned timeout_ms)
{
    elm_response_t r;
    spike_logf(s, "> %s", cmd);
    uint64_t t0 = tp_now_ms();
    elm_command(s->elm, cmd, timeout_ms, &r);
    log_reply(s, &r, t0);
    return r.status;
}

static void do_protocol(spike_t *s)
{
    elm_response_t r;
    int protocol = 0;
    bool automatic = false;

    spike_logf(s, "> ATDPN");
    uint64_t t0 = tp_now_ms();
    elm_read_protocol(s->elm, &protocol, &automatic, &r);
    log_reply(s, &r, t0);
    if (r.status == ELM_OK) {
        spike_logf(s, "= Protocol %X: %s%s", (unsigned)protocol,
                   elm_protocol_name(protocol), automatic ? " (auto)" : "");
    }
}

static void do_voltage(spike_t *s)
{
    elm_response_t r;
    int mv = 0;

    spike_logf(s, "> ATRV");
    uint64_t t0 = tp_now_ms();
    elm_read_voltage(s->elm, &mv, &r);
    log_reply(s, &r, t0);
    if (r.status == ELM_OK) {
        spike_logf(s, "= %d.%02d V", mv / 1000, (mv % 1000) / 10);
    }
}

void spike_run(spike_t *s, spike_action_t action)
{
    if (action == SPIKE_CONNECT) {
        do_connect(s);
        return;
    }
    if (!ensure_connected(s)) {
        return;
    }

    switch (action) {
    case SPIKE_ATZ:
        send_logged(s, "ATZ", ELM_TIMEOUT_RESET_MS);
        break;
    case SPIKE_SEARCH:
        if (send_logged(s, "ATSP0", ELM_TIMEOUT_DEFAULT_MS) == ELM_OK) {
            send_logged(s, "0100", ELM_TIMEOUT_SEARCH_MS);
        }
        break;
    case SPIKE_PROTOCOL:
        do_protocol(s);
        break;
    case SPIKE_VOLTAGE:
        do_voltage(s);
        break;
    case SPIKE_VIN:
        send_logged(s, "0902", ELM_TIMEOUT_SEARCH_MS);
        break;
    case SPIKE_CONNECT:
    case SPIKE_ACTION_COUNT:
        break;
    }
}
