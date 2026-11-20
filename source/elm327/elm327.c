#include "elm327.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

/* Bytes read per io.read call. */
#define READ_CHUNK 128
/* Upper bound on reads when discarding stale input before a command. */
#define FLUSH_MAX_READS 64

void elm_bind(elm_t *elm, const elm_io_t *io)
{
    elm->io = *io;
}

static void response_reset(elm_response_t *r)
{
    r->status = ELM_OK;
    r->raw[0] = '\0';
    r->raw_len = 0;
    r->line_count = 0;
    r->error_line = -1;
}

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* Printable ASCII, no CR / LF, not empty, not too long. */
static bool command_valid(const char *cmd)
{
    if (cmd == NULL) {
        return false;
    }
    size_t n = strlen(cmd);
    if (n == 0 || n > ELM_COMMAND_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)cmd[i];
        if (c < 0x20 || c > 0x7e) {
            return false;
        }
    }
    return true;
}

/* Equal ignoring spaces and case: "01 0c" matches "010C". */
static bool same_command(const char *line, const char *cmd)
{
    for (;;) {
        while (*line == ' ') {
            line++;
        }
        while (*cmd == ' ') {
            cmd++;
        }
        if (*line == '\0' || *cmd == '\0') {
            return *line == *cmd;
        }
        if (toupper((unsigned char)*line) != toupper((unsigned char)*cmd)) {
            return false;
        }
        line++;
        cmd++;
    }
}

/* Status lines that carry no data. */
static bool is_noise_line(const char *line)
{
    if (starts_with(line, "SEARCHING")) {
        return true;
    }
    return starts_with(line, "BUS INIT") && strstr(line, "ERROR") == NULL;
}

static elm_status_t classify_line(const char *line)
{
    static const char *const adapter_errors[] = {
        "BUS ERROR", "BUS BUSY", "FB ERROR", "DATA ERROR", "RX ERROR",
        "BUFFER FULL", "LV RESET", "ACT ALERT",
    };

    if (strcmp(line, "?") == 0) {
        return ELM_ERR_UNKNOWN_COMMAND;
    }
    if (strcmp(line, "NO DATA") == 0 || strcmp(line, "NODATA") == 0) {
        return ELM_ERR_NO_DATA;
    }
    if (starts_with(line, "UNABLE TO CONNECT")) {
        return ELM_ERR_UNABLE_TO_CONNECT;
    }
    if (starts_with(line, "BUS INIT") && strstr(line, "ERROR") != NULL) {
        return ELM_ERR_BUS_INIT;
    }
    if (strcmp(line, "CAN ERROR") == 0) {
        return ELM_ERR_CAN;
    }
    if (strcmp(line, "STOPPED") == 0) {
        return ELM_ERR_STOPPED;
    }
    for (size_t i = 0; i < sizeof adapter_errors / sizeof adapter_errors[0]; i++) {
        if (strstr(line, adapter_errors[i]) != NULL) {
            return ELM_ERR_ADAPTER;
        }
    }
    if (starts_with(line, "ERR") && isdigit((unsigned char)line[3])) {
        return ELM_ERR_ADAPTER;
    }
    return ELM_OK;
}

/* State while splitting a reply into lines. */
typedef struct {
    elm_response_t *resp;
    const char *cmd;
    bool seen_text;   /* a non-empty line has been seen (echo check is done) */
    bool overflow;
} line_builder_t;

static void finish_line(line_builder_t *b, char *line, size_t len)
{
    /* Trim spaces at both ends. */
    while (len > 0 && line[len - 1] == ' ') {
        len--;
    }
    line[len] = '\0';
    char *start = line;
    while (*start == ' ') {
        start++;
    }
    if (*start == '\0') {
        return;
    }

    bool first = !b->seen_text;
    b->seen_text = true;
    if (first && b->cmd != NULL && same_command(start, b->cmd)) {
        return; /* echo */
    }
    if (is_noise_line(start)) {
        return;
    }

    elm_response_t *r = b->resp;
    if (r->line_count >= ELM_MAX_LINES) {
        b->overflow = true;
        return;
    }
    /* start fits: it came from a buffer of ELM_LINE_MAX. */
    strcpy(r->lines[r->line_count], start);
    r->line_count++;
}

void elm_parse_reply(const char *raw, size_t len, const char *cmd,
                     elm_response_t *resp)
{
    response_reset(resp);

    bool overflow = false;

    /* Raw copy for logs, without NUL bytes. */
    for (size_t i = 0; i < len; i++) {
        if (raw[i] == '\0') {
            continue;
        }
        if (resp->raw_len < ELM_RAW_MAX - 1) {
            resp->raw[resp->raw_len++] = raw[i];
        } else {
            overflow = true;
        }
    }
    resp->raw[resp->raw_len] = '\0';

    /* Split on CR / LF, keep printable ASCII, upper-case it. */
    line_builder_t b = { resp, cmd, false, false };
    char line[ELM_LINE_MAX];
    size_t n = 0;
    for (size_t i = 0; i <= len; i++) {
        char c = (i < len) ? raw[i] : '\r';
        if (c == '\r' || c == '\n') {
            finish_line(&b, line, n);
            n = 0;
            continue;
        }
        unsigned char u = (unsigned char)c;
        if (u < 0x20 || u > 0x7e) {
            continue; /* garbage byte */
        }
        if (n < ELM_LINE_MAX - 1) {
            line[n++] = (char)toupper(u);
        } else {
            overflow = true;
        }
    }

    for (int i = 0; i < resp->line_count; i++) {
        elm_status_t s = classify_line(resp->lines[i]);
        if (s != ELM_OK) {
            resp->status = s;
            resp->error_line = i;
            break;
        }
    }

    if ((overflow || b.overflow) && resp->status == ELM_OK) {
        resp->status = ELM_ERR_OVERFLOW;
    }
}

/* Discard bytes already waiting (late replies, junk after a prompt). */
static int flush_input(elm_t *elm)
{
    char junk[READ_CHUNK];
    for (int i = 0; i < FLUSH_MAX_READS; i++) {
        int n = elm->io.read(elm->io.ctx, junk, sizeof junk, 0);
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }
    }
    return 0;
}

/* Read until '>' or the deadline. Bytes after the prompt in the same read
 * are dropped. Keeps reading to the prompt even if buf fills up, so the
 * stream stays in step with the adapter. */
static elm_status_t read_reply(elm_t *elm, unsigned timeout_ms, char *buf,
                               size_t cap, size_t *out_len)
{
    uint64_t deadline = elm->io.now_ms(elm->io.ctx) + timeout_ms;
    size_t len = 0;
    bool overflow = false;

    for (;;) {
        uint64_t now = elm->io.now_ms(elm->io.ctx);
        if (now >= deadline) {
            *out_len = len;
            return ELM_ERR_TIMEOUT;
        }
        uint64_t left = deadline - now;
        unsigned wait = left > UINT_MAX ? UINT_MAX : (unsigned)left;

        char chunk[READ_CHUNK];
        int n = elm->io.read(elm->io.ctx, chunk, sizeof chunk, wait);
        if (n < 0 || n > (int)sizeof chunk) {
            *out_len = len;
            return ELM_ERR_IO;
        }
        for (int i = 0; i < n; i++) {
            if (chunk[i] == '>') {
                *out_len = len;
                return overflow ? ELM_ERR_OVERFLOW : ELM_OK;
            }
            if (len < cap) {
                buf[len++] = chunk[i];
            } else {
                overflow = true;
            }
        }
    }
}

elm_status_t elm_command(elm_t *elm, const char *cmd, unsigned timeout_ms,
                         elm_response_t *resp)
{
    response_reset(resp);

    if (!command_valid(cmd)) {
        resp->status = ELM_ERR_INVALID_ARG;
        return resp->status;
    }
    if (flush_input(elm) < 0) {
        resp->status = ELM_ERR_IO;
        return resp->status;
    }

    char out[ELM_COMMAND_MAX + 1];
    size_t n = strlen(cmd);
    memcpy(out, cmd, n);
    out[n] = '\r';
    if (elm->io.write(elm->io.ctx, out, n + 1) != 0) {
        resp->status = ELM_ERR_IO;
        return resp->status;
    }

    char buf[ELM_RAW_MAX];
    size_t len = 0;
    elm_status_t read_status = read_reply(elm, timeout_ms, buf, sizeof buf, &len);

    /* Parse even on timeout so partial replies can be shown. */
    elm_parse_reply(buf, len, cmd, resp);
    if (read_status != ELM_OK) {
        resp->status = read_status;
        resp->error_line = -1;
    }
    return resp->status;
}

static bool has_ok_line(const elm_response_t *r)
{
    for (int i = 0; i < r->line_count; i++) {
        if (strcmp(r->lines[i], "OK") == 0) {
            return true;
        }
    }
    return false;
}

elm_status_t elm_startup(elm_t *elm, elm_startup_t *out)
{
    static const char *const commands[] = { "ATZ", "ATE0", "ATL0", "ATSP0" };

    out->id[0] = '\0';
    out->failed_command = NULL;

    for (size_t i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        unsigned timeout = (i == 0) ? ELM_TIMEOUT_RESET_MS : ELM_TIMEOUT_DEFAULT_MS;
        elm_status_t st = elm_command(elm, commands[i], timeout, &out->last);

        if (st == ELM_OK) {
            if (i == 0) {
                /* Identity: last line mentioning ELM, else the last line. */
                int pick = out->last.line_count - 1;
                for (int k = 0; k < out->last.line_count; k++) {
                    if (strstr(out->last.lines[k], "ELM") != NULL) {
                        pick = k;
                    }
                }
                if (pick < 0) {
                    st = ELM_ERR_BAD_REPLY;
                } else {
                    strcpy(out->id, out->last.lines[pick]);
                }
            } else if (!has_ok_line(&out->last)) {
                st = ELM_ERR_BAD_REPLY;
            }
        }

        if (st != ELM_OK) {
            out->last.status = st;
            out->failed_command = commands[i];
            return st;
        }
    }
    return ELM_OK;
}

static int hex_value(char c)
{
    c = (char)toupper((unsigned char)c);
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool elm_parse_voltage(const char *s, int *millivolts)
{
    int whole = 0;
    int frac = 0;
    int frac_digits = 0;
    bool any = false;

    while (isdigit((unsigned char)*s)) {
        whole = whole * 10 + (*s - '0');
        if (whole > 99) {
            return false;
        }
        any = true;
        s++;
    }
    if (!any) {
        return false;
    }
    if (*s == '.' || *s == ',') {
        s++;
        while (isdigit((unsigned char)*s)) {
            if (frac_digits < 3) {
                frac = frac * 10 + (*s - '0');
                frac_digits++;
            }
            s++;
        }
    }
    while (*s == ' ') {
        s++;
    }
    if (*s == 'V' || *s == 'v') {
        s++;
    }
    if (*s != '\0') {
        return false;
    }
    while (frac_digits < 3) {
        frac *= 10;
        frac_digits++;
    }
    *millivolts = whole * 1000 + frac;
    return true;
}

bool elm_parse_protocol(const char *s, int *protocol, bool *automatic)
{
    size_t n = strlen(s);
    int v;
    bool is_auto;

    if (n == 2 && (s[0] == 'A' || s[0] == 'a')) {
        v = hex_value(s[1]);
        is_auto = true;
    } else if (n == 1) {
        v = hex_value(s[0]);
        is_auto = false;
    } else {
        return false;
    }
    if (v < 0 || v > 0xC) {
        return false;
    }
    *protocol = v;
    *automatic = is_auto;
    return true;
}

elm_status_t elm_read_voltage(elm_t *elm, int *millivolts, elm_response_t *resp)
{
    if (elm_command(elm, "ATRV", ELM_TIMEOUT_DEFAULT_MS, resp) != ELM_OK) {
        return resp->status;
    }
    if (resp->line_count < 1 ||
        !elm_parse_voltage(resp->lines[resp->line_count - 1], millivolts)) {
        resp->status = ELM_ERR_BAD_REPLY;
    }
    return resp->status;
}

elm_status_t elm_read_protocol(elm_t *elm, int *protocol, bool *automatic,
                               elm_response_t *resp)
{
    if (elm_command(elm, "ATDPN", ELM_TIMEOUT_DEFAULT_MS, resp) != ELM_OK) {
        return resp->status;
    }
    if (resp->line_count < 1 ||
        !elm_parse_protocol(resp->lines[resp->line_count - 1], protocol, automatic)) {
        resp->status = ELM_ERR_BAD_REPLY;
    }
    return resp->status;
}

const char *elm_status_str(elm_status_t status)
{
    switch (status) {
    case ELM_OK:                    return "OK";
    case ELM_ERR_TIMEOUT:           return "Timed out waiting for adapter";
    case ELM_ERR_IO:                return "Connection error";
    case ELM_ERR_OVERFLOW:          return "Reply too long";
    case ELM_ERR_INVALID_ARG:       return "Invalid command";
    case ELM_ERR_UNKNOWN_COMMAND:   return "Adapter did not understand command";
    case ELM_ERR_NO_DATA:           return "No data";
    case ELM_ERR_UNABLE_TO_CONNECT: return "Unable to connect to vehicle";
    case ELM_ERR_BUS_INIT:          return "Bus init error";
    case ELM_ERR_CAN:               return "CAN error";
    case ELM_ERR_STOPPED:           return "Stopped";
    case ELM_ERR_ADAPTER:           return "Adapter or bus error";
    case ELM_ERR_BAD_REPLY:         return "Unexpected reply";
    case ELM_STATUS_COUNT:          break;
    }
    return "Unknown status";
}

const char *elm_protocol_name(int protocol)
{
    static const char *const names[] = {
        "Automatic",
        "SAE J1850 PWM (41.6 kbaud)",
        "SAE J1850 VPW (10.4 kbaud)",
        "ISO 9141-2 (5 baud init)",
        "ISO 14230-4 KWP (5 baud init)",
        "ISO 14230-4 KWP (fast init)",
        "ISO 15765-4 CAN (11 bit, 500 kbaud)",
        "ISO 15765-4 CAN (29 bit, 500 kbaud)",
        "ISO 15765-4 CAN (11 bit, 250 kbaud)",
        "ISO 15765-4 CAN (29 bit, 250 kbaud)",
        "SAE J1939 CAN (29 bit, 250 kbaud)",
        "USER1 CAN",
        "USER2 CAN",
    };
    if (protocol < 0 || protocol >= (int)(sizeof names / sizeof names[0])) {
        return "Unknown protocol";
    }
    return names[protocol];
}

const char *elm_protocol_short(int protocol)
{
    static const char *const names[] = {
        "Auto",          "J1850 PWM",      "J1850 VPW",       "ISO 9141-2",
        "KWP 5-baud",    "KWP fast",       "CAN 11-bit 500k", "CAN 29-bit 500k",
        "CAN 11-bit 250k", "CAN 29-bit 250k", "J1939 CAN",     "USER1 CAN",
        "USER2 CAN",
    };
    if (protocol < 0 || protocol >= (int)(sizeof names / sizeof names[0])) {
        return "Unknown";
    }
    return names[protocol];
}

size_t elm_escape_raw(const char *raw, size_t len, char *out, size_t out_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    if (out_len == 0) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)raw[i];
        char piece[5];
        size_t plen;

        if (c == '\r') {
            memcpy(piece, "\\r", 2);
            plen = 2;
        } else if (c == '\n') {
            memcpy(piece, "\\n", 2);
            plen = 2;
        } else if (c == '\\') {
            memcpy(piece, "\\\\", 2);
            plen = 2;
        } else if (c >= 0x20 && c <= 0x7e) {
            piece[0] = (char)c;
            plen = 1;
        } else {
            piece[0] = '\\';
            piece[1] = 'x';
            piece[2] = hex[c >> 4];
            piece[3] = hex[c & 0xf];
            plen = 4;
        }

        if (n + plen + 1 > out_len) {
            /* Does not fit: end with "..." (may cut an escape short). */
            size_t keep = out_len >= 4 ? out_len - 4 : 0;
            n = n < keep ? n : keep;
            size_t dots = out_len - 1 - n;
            memcpy(out + n, "...", dots < 3 ? dots : 3);
            n += dots < 3 ? dots : 3;
            out[n] = '\0';
            return n;
        }
        memcpy(out + n, piece, plen);
        n += plen;
    }
    out[n] = '\0';
    return n;
}
