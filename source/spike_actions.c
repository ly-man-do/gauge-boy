#include "spike_actions.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "data/config.h"
#include "version.h"

void spike_init(spike_t *s, tcp_transport_t *transport, elm_t *elm,
                spike_log_fn log, void *log_ctx)
{
    s->transport = transport;
    s->elm = elm;
    s->log = log;
    s->log_ctx = log_ctx;
    s->start_ms = tp_now_ms();
    s->net_up = NULL;
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
        spike_logf(s, "! %s (errno %d at %s: %s)", tp_status_str(st), t->last_errno,
                   t->last_step[0] != '\0' ? t->last_step : "?", strerror(t->last_errno));
    } else {
        spike_logf(s, "! %s", tp_status_str(st));
    }
}

static void log_net_info(spike_t *s)
{
    if (s->net_up != NULL) {
        char msg[160];
        msg[0] = '\0';
        bool up = s->net_up(msg, sizeof msg);
        if (msg[0] != '\0') {
            spike_logf(s, "%s%s", up ? "" : "! ", msg);
        }
    }
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

char *spike_format_log_name(char *out, size_t out_len, const struct tm *when)
{
    if (when == NULL) {
        snprintf(out, out_len, "gaugeboy-log.txt");
    } else {
        snprintf(out, out_len, "gaugeboy-log-%04d%02d%02d-%02d%02d%02d.txt",
                 when->tm_year + 1900, when->tm_mon + 1, when->tm_mday,
                 when->tm_hour, when->tm_min, when->tm_sec);
    }
    return out;
}

bool spike_plan_slot(const char *adapter_host, net_ip_settings_t *plan)
{
    unsigned a, b, c, d;
    memset(plan, 0, sizeof *plan);
    if (adapter_host == NULL || !config_valid_ipv4(adapter_host) ||
        sscanf(adapter_host, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }
    uint8_t own = d == 11 ? 12 : 11;
    const uint8_t ip[4] = { (uint8_t)a, (uint8_t)b, (uint8_t)c, own };
    const uint8_t gw[4] = { (uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d };
    const uint8_t mask[4] = { 255, 255, 255, 0 };
    plan->dhcp = false;
    plan->auto_dns = false;
    memcpy(plan->ip, ip, 4);
    memcpy(plan->mask, mask, 4);
    memcpy(plan->gateway, gw, 4);
    memcpy(plan->dns1, ip, 4);
    memset(plan->dns2, 0, 4);
    return true;
}

bool spike_slot_matches(const net_ip_settings_t *now, const net_ip_settings_t *plan)
{
    return now->dhcp == plan->dhcp && now->auto_dns == plan->auto_dns &&
           memcmp(now->ip, plan->ip, 4) == 0 && memcmp(now->mask, plan->mask, 4) == 0 &&
           memcmp(now->gateway, plan->gateway, 4) == 0 && memcmp(now->dns1, plan->dns1, 4) == 0 &&
           memcmp(now->dns2, plan->dns2, 4) == 0;
}

static void ip_str(char *out, size_t len, const uint8_t b[4])
{
    snprintf(out, len, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

int spike_format_slot_compare(const net_ip_settings_t *now, const net_ip_settings_t *plan,
                              char lines[][SPIKE_COMPARE_COLS], int max)
{
    static const char *const labels[] = { "IP", "Mask", "Gate", "DNS 1", "DNS 2" };
    const uint8_t *now_v[] = { now->ip, now->mask, now->gateway, now->dns1, now->dns2 };
    const uint8_t *new_v[] = { plan->ip, plan->mask, plan->gateway, plan->dns1, plan->dns2 };
    int n = 0;
    if (n < max) {
        snprintf(lines[n++], SPIKE_COMPARE_COLS, "%-5s %-15s %s", "", "NOW", "NEW");
    }
    for (int i = 0; i < 5 && n < max; i++) {
        char a[16], b[16];
        bool now_auto = i < 3 ? now->dhcp : now->auto_dns;
        bool new_auto = i < 3 ? plan->dhcp : plan->auto_dns;
        if (now_auto) {
            snprintf(a, sizeof a, "auto");
        } else {
            ip_str(a, sizeof a, now_v[i]);
        }
        if (new_auto) {
            snprintf(b, sizeof b, "auto");
        } else {
            ip_str(b, sizeof b, new_v[i]);
        }
        snprintf(lines[n++], SPIKE_COMPARE_COLS, "%-5s %-15s %s", labels[i], a, b);
    }
    return n;
}
