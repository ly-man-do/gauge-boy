#include "spike_actions.h"

#include <stdarg.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "data/config.h"
#include "data/live_views.h"
#include "obd/obd_link.h"
#include "obd/obd_reply.h"
#include "obd/obd_vin.h"
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
    s->units = OBD_UNITS_US;

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
    case SPIKE_LIVE_TEST: return "LIVE TEST";
    case SPIKE_PROTOCOL: return "ATDPN";
    case SPIKE_VOLTAGE:  return "ATRV";
    case SPIKE_VIN:      return "0902 VIN";
    case SPIKE_SPEED:    return "SPEED TEST";
    case SPIKE_ACTION_COUNT: break;
    }
    return "?";
}

char *spike_format_header(char *out, size_t out_len, bool connected, int busy)
{
    bool is_busy = busy >= 0 && busy < SPIKE_ACTION_COUNT;
    snprintf(out, out_len, "GaugeBoy %s  %s%s%s", GB_VERSION_TEXT,
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
static void log_reply_ms(spike_t *s, const elm_response_t *r, unsigned ms)
{
    char escaped[1100];

    for (int i = 0; i < r->line_count; i++) {
        spike_logf(s, "< %s", r->lines[i]);
    }
    elm_escape_raw(r->raw, r->raw_len, escaped, sizeof escaped);
    spike_logf(s, "  raw: %s", escaped);

    if (r->status == ELM_OK) {
        spike_logf(s, "= OK (%u ms)", ms);
    } else {
        spike_logf(s, "! %s (%u ms)", elm_status_str(r->status), ms);
    }
    if (r->status == ELM_ERR_IO) {
        spike_logf(s, "! Connection lost; the next command reconnects");
    }
}

static void log_reply(spike_t *s, const elm_response_t *r, uint64_t t0)
{
    log_reply_ms(s, r, (unsigned)(tp_now_ms() - t0));
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

/* 0902: log the reply, then decode the VIN. The spike doesn't keep the
 * protocol, so try the legacy layout (J1850 / ISO 9141-2 / KWP) and then
 * CAN, with headers off as the adapter runs here. */
static void do_vin(spike_t *s)
{
    static elm_response_t r;
    static obd_reply_t reply;
    spike_logf(s, "> 0902");
    uint64_t t0 = tp_now_ms();
    elm_command(s->elm, "0902", ELM_TIMEOUT_SEARCH_MS, &r);
    log_reply(s, &r, t0);
    if (r.status != ELM_OK) {
        return;
    }

    char vin[OBD_VIN_LEN + 1] = "";
    static const int protocols[] = { 3, 6 };
    bool found = false;
    for (unsigned i = 0; i < sizeof protocols / sizeof protocols[0] && !found; i++) {
        if (obd_assemble(&r, protocols[i], false, &reply) == OBD_OK) {
            found = obd_decode_vin(&reply, protocols[i], vin);
        }
    }
    if (!found) {
        spike_logf(s, "! No VIN in the reply");
        return;
    }
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    int year = obd_vin_model_year(vin, tm != NULL ? tm->tm_year + 1900 : 0);
    obd_vin_maker_t mk;
    bool known = obd_vin_maker(vin, &mk);
    obd_vin_model_t md;
    bool have_model = obd_vin_model(vin, year, &md);
    spike_logf(s, "= VIN %s: %s, model year %d%s%s%s%s", vin, known ? mk.maker : "maker not listed",
               year, have_model ? ", " : "", have_model ? md.make : "", have_model ? " " : "",
               have_model ? md.model : "");
    if (!obd_vin_check_digit_ok(vin)) {
        spike_logf(s, "  (check digit doesn't match)");
    }
}

void spike_trace_command(void *ctx, const char *cmd, const elm_response_t *resp, unsigned ms)
{
    spike_t *s = ctx;
    spike_logf(s, "> %s", cmd);
    log_reply_ms(s, resp, ms);
}

/* ATSP0+0100: protocol search plus the supported-PID scan (obd_link), with
 * every command logged, then a summary for the compatibility list. */
static void do_search(spike_t *s)
{
    static obd_link_t link;
    if (send_logged(s, "ATSP0", ELM_TIMEOUT_DEFAULT_MS) != ELM_OK) {
        return;
    }
    if (obd_link_open_traced(s->elm, false, &link, spike_trace_command, s) != OBD_LINK_OK) {
        spike_logf(s, "! %s", obd_link_result_str(link.result));
        return;
    }
    char pids[600];
    spike_logf(s, "= Protocol %d: %s%s, %d ECU%s answered", link.protocol,
               elm_protocol_name(link.protocol), link.protocol_auto ? " (auto)" : "",
               link.ecus, link.ecus == 1 ? "" : "s");
    spike_logf(s, "= Supported PIDs: %s", obd_link_pid_list(&link, pids, sizeof pids));
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

/* ---------- speed test ---------- */

typedef struct {
    unsigned min, max, sum;
    int ok, failed;
    bool unsupported;   /* the adapter answered "?" */
} speed_set_t;

static uint64_t elm_clock(const spike_t *s)
{
    return s->elm->io.now_ms != NULL ? s->elm->io.now_ms(s->elm->io.ctx) : tp_now_ms();
}

static speed_set_t time_command(spike_t *s, const char *cmd, int n)
{
    static elm_response_t r;
    speed_set_t st = { UINT_MAX, 0, 0, 0, 0, false };
    for (int i = 0; i < n; i++) {
        uint64_t t0 = elm_clock(s);
        elm_status_t e = elm_command(s->elm, cmd, ELM_TIMEOUT_DEFAULT_MS, &r);
        unsigned ms = (unsigned)(elm_clock(s) - t0);
        if (e == ELM_ERR_UNKNOWN_COMMAND) {
            st.unsupported = true;
            break;
        }
        if (e != ELM_OK) {
            st.failed++;
            if (e == ELM_ERR_IO || e == ELM_ERR_TIMEOUT) {
                break;
            }
            continue;
        }
        st.ok++;
        st.sum += ms;
        st.min = ms < st.min ? ms : st.min;
        st.max = ms > st.max ? ms : st.max;
    }
    return st;
}

static unsigned speed_avg(const speed_set_t *st)
{
    return st->ok > 0 ? st->sum / (unsigned)st->ok : 0;
}

static void log_set(spike_t *s, const char *what, const speed_set_t *st)
{
    if (st->unsupported) {
        spike_logf(s, "  %-20s not supported (?)", what);
    } else if (st->ok == 0) {
        spike_logf(s, "  %-20s failed", what);
    } else {
        spike_logf(s, "  %-20s avg %u ms (min %u, max %u)%s", what, speed_avg(st), st->min,
                   st->max, st->failed > 0 ? ", some failed" : "");
    }
}

/* "370" or "-" for the summary line. */
static const char *avg_text(const speed_set_t *st, char *buf, size_t len)
{
    if (st->ok == 0 || st->unsupported) {
        snprintf(buf, len, "-");
    } else {
        snprintf(buf, len, "%u", speed_avg(st));
    }
    return buf;
}

/* One more 010C1 set with an adapter setting on, then the default back. */
static speed_set_t time_with(spike_t *s, const char *setting, const char *restore,
                             const char *what, int n)
{
    static elm_response_t r;
    speed_set_t st = { UINT_MAX, 0, 0, 0, 0, true };
    elm_status_t e = elm_command(s->elm, setting, ELM_TIMEOUT_DEFAULT_MS, &r);
    if (e != ELM_OK) {
        spike_logf(s, "  %-20s not supported (%s)", setting, elm_status_str(e));
        return st;
    }
    st = time_command(s, "010C1", n);
    log_set(s, what, &st);
    elm_command(s->elm, restore, ELM_TIMEOUT_DEFAULT_MS, &r);
    return st;
}

void spike_speed_test(spike_t *s, int n)
{
    static elm_response_t r;
    char a[12], b[12], c[12], d[12], e2[12];
    spike_logf(s, "Speed test: each request %d times", n);

    speed_set_t adapter = time_command(s, "ATRV", n);
    log_set(s, "ATRV (adapter only)", &adapter);

    elm_status_t e = elm_command(s->elm, "0100", ELM_TIMEOUT_SEARCH_MS, &r);
    if (e != ELM_OK) {
        spike_logf(s, "! Speed test: the car didn't answer 0100 (%s); is the engine running?",
                   elm_status_str(e));
        return;
    }
    speed_set_t plain = time_command(s, "010C", n);
    log_set(s, "010C (RPM)", &plain);
    speed_set_t one = time_command(s, "010C1", n);
    log_set(s, "010C1 (one reply)", &one);
    /* The adapter's aggressive timing, and a 100 ms reply timeout instead of
     * the default 200 ms; the defaults go back afterwards. */
    speed_set_t at2 = time_with(s, "ATAT2", "ATAT1", "010C1 with ATAT2", n);
    speed_set_t st19 = time_with(s, "ATST19", "ATST32", "010C1 with ATST19", n);

    spike_logf(s, "= Speed (avg ms): adapter %s, car %s, one reply %s, ATAT2 %s, ATST19 %s",
               avg_text(&adapter, a, sizeof a), avg_text(&plain, b, sizeof b),
               avg_text(&one, c, sizeof c), avg_text(&at2, d, sizeof d),
               avg_text(&st19, e2, sizeof e2));
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
        do_search(s);
        break;
    case SPIKE_PROTOCOL:
        do_protocol(s);
        break;
    case SPIKE_VOLTAGE:
        do_voltage(s);
        break;
    case SPIKE_VIN:
        do_vin(s);
        break;
    case SPIKE_SPEED:
        spike_speed_test(s, SPIKE_SPEED_COUNT);
        break;
    case SPIKE_LIVE_TEST:
        spike_live_test(s, SPIKE_LIVE_TEST_MS);
        break;
    case SPIKE_CONNECT:
    case SPIKE_ACTION_COUNT:
        break;
    }
}

void spike_live_summary(spike_t *s, const obd_live_t *lv)
{
    if (lv->requests == 0) {
        return;
    }
    uint64_t now = lv->pid[0].at_ms > lv->volts_at_ms ? lv->pid[0].at_ms : lv->volts_at_ms;
    for (int i = 0; i < lv->count; i++) {
        now = lv->pid[i].at_ms > now ? lv->pid[i].at_ms : now;
    }
    unsigned secs = now > lv->started_ms ? (unsigned)((now - lv->started_ms) / 1000u) : 0;
    spike_logf(s, "LIVE: %u requests in %u s, %.1f/s, %u failed%s%s%s", (unsigned)lv->requests,
               secs, (double)obd_live_rate(lv, now), (unsigned)lv->failures,
               lv->use_count ? (lv->count_refused ? ", no response count" : ", response count 1")
                             : "",
               lv->multi_requests > 0 ? (lv->multi_refused ? ", multi-PID refused"
                                         : lv->multi_count_off ? ", several PIDs a request (no count)"
                                         : ", several PIDs a request")
                                      : "",
               lv->header_checked ? ", engine ECU only"
               : lv->header_refused && lv->use_engine_header &&
                 obd_family(lv->protocol) == OBD_FAMILY_CAN ? ", all ECUs (engine address unanswered)"
               : "");
    char line[200] = "";
    for (int i = 0; i < lv->count; i++) {
        const obd_live_pid_t *p = &lv->pid[i];
        snprintf(line + strlen(line), sizeof line - strlen(line), "%s%02X %.1f Hz%s",
                 i ? ", " : "", (unsigned)p->pid, (double)p->hz, p->dropped ? " (dropped)" : "");
    }
    if (line[0] != '\0') {
        spike_logf(s, "LIVE: %s", line);
    }
    if (lv->left_out > 0) {
        spike_logf(s, "LIVE: %u PIDs left out of combined replies (asked again alone)",
                   (unsigned)lv->left_out);
    }
    for (int i = 0; i < lv->left_out_logged; i++) {
        const obd_live_left_out_t *lo = &lv->left_out_log[i];
        char missing[3 * OBD_LIVE_MULTI_MAX + 1] = "";
        for (int k = 0; k < lo->missing_count; k++) {
            snprintf(missing + strlen(missing), sizeof missing - strlen(missing), "%s%02X",
                     k ? " " : "", (unsigned)lo->missing[k]);
        }
        spike_logf(s, "LIVE: %s left out %s: %s", lo->cmd, missing, lo->reply);
    }
}

/* One PID asked alone, as LIVE asks it ("010C1", or "010C" if the adapter
 * refuses the response count). Logged with its value. */
static bool live_test_pid(spike_t *s, const obd_link_t *link, uint8_t pid, bool *count)
{
    static elm_response_t r;
    static obd_reply_t reply;
    const obd_pid_info_t *info = obd_pid_info(pid);
    char cmd[8];
    snprintf(cmd, sizeof cmd, *count ? "01%02X1" : "01%02X", (unsigned)pid);
    uint64_t t0 = elm_clock(s);
    elm_status_t st = elm_command(s->elm, cmd, ELM_TIMEOUT_DEFAULT_MS, &r);
    if (*count && st == ELM_ERR_UNKNOWN_COMMAND) {
        *count = false;
        snprintf(cmd, sizeof cmd, "01%02X", (unsigned)pid);
        t0 = elm_clock(s);
        st = elm_command(s->elm, cmd, ELM_TIMEOUT_DEFAULT_MS, &r);
    }
    unsigned ms = (unsigned)(elm_clock(s) - t0);
    float v = 0.0f;
    char reply_text[64] = "";
    for (int i = 0; i < r.line_count && strlen(reply_text) < sizeof reply_text - 2; i++) {
        snprintf(reply_text + strlen(reply_text), sizeof reply_text - strlen(reply_text), "%s%s",
                 i ? " / " : "", r.lines[i]);
    }
    if (st == ELM_OK && obd_assemble(&r, link->protocol, false, &reply) == OBD_OK &&
        obd_pid_from_reply(&reply, pid, &v) == OBD_PID_OK) {
        char value[32];
        obd_pid_format(pid, v, s->units, value, sizeof value);
        spike_logf(s, "  %02X %-9s %-14s %s (%u ms)", (unsigned)pid, info->label, value, reply_text, ms);
        return true;
    }
    spike_logf(s, "! %02X %-9s %s: %s (%u ms)", (unsigned)pid, info->label,
               st == ELM_OK ? "not decoded" : elm_status_str(st), reply_text, ms);
    return false;
}

void spike_live_test(spike_t *s, unsigned poll_ms)
{
    static obd_link_t link;
    if (obd_link_open(s->elm, true, &link) != OBD_LINK_OK) {
        spike_logf(s, "! LIVE TEST: %s", obd_link_result_str(link.result));
        return;
    }
    spike_logf(s, "LIVE TEST: protocol %d, %s, %d ECU%s", link.protocol,
               elm_protocol_name(link.protocol), link.ecus, link.ecus == 1 ? "" : "s");

    /* 1: every supported PID GaugeBoy decodes, once. */
    bool count = true;
    int asked = 0, read = 0;
    for (int i = 0; i < obd_pid_count(); i++) {
        uint8_t pid = obd_pid_at(i)->pid;
        if (!obd_link_pid_supported(&link, pid)) {
            continue;
        }
        asked++;
        read += live_test_pid(s, &link, pid, &count) ? 1 : 0;
    }
    spike_logf(s, "= Decode: %d of %d supported PIDs read%s", read, asked,
               count ? "" : " (no response count)");

    /* 2: the Engine view for poll_ms, polled as LIVE polls it. */
    static obd_live_t lv;
    uint8_t pids[OBD_LIVE_MAX];
    uint32_t periods[OBD_LIVE_MAX];
    int n = live_view_request(LIVE_VIEW_ENGINE, NULL, 0, pids, periods, OBD_LIVE_MAX);
    if (obd_live_init(&lv, &link, pids, n) == 0) {
        spike_logf(s, "! LIVE TEST: the car supports none of the Engine view's PIDs");
        return;
    }
    for (int i = 0; i < n; i++) {
        if (periods[i] != LIVE_PERIOD_DEFAULT) {
            obd_live_set_period(&lv, pids[i], periods[i]);
        }
    }
    lv.use_count = true;
    lv.use_multi = true;
    lv.use_engine_header = true;
    spike_logf(s, "LIVE TEST: polling the Engine view for %u s", poll_ms / 1000u);
    uint64_t t0 = elm_clock(s);
    while (elm_clock(s) - t0 < poll_ms) {
        obd_live_step_t st = obd_live_step(&lv, s->elm, NULL, NULL);
        if (st == OBD_LIVE_LOST) {
            spike_logf(s, "! LIVE TEST: the adapter stopped answering");
            break;
        }
        if (st == OBD_LIVE_IDLE) {
            break;
        }
    }
    obd_live_restore_header(&lv, s->elm);
    spike_live_summary(s, &lv);
    char value[32];
    for (int i = 0; i < lv.count; i++) {
        if (lv.pid[i].have) {
            obd_pid_format(lv.pid[i].pid, lv.pid[i].value, s->units, value, sizeof value);
            spike_logf(s, "  %-9s %s", obd_pid_info(lv.pid[i].pid)->label, value);
        }
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

bool spike_connect_if_needed(spike_t *s)
{
    return ensure_connected(s) == 1;
}
