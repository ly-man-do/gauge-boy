#ifndef GAUGEBOY_ELM327_H
#define GAUGEBOY_ELM327_H

/*
 * ELM327 driver: sends AT / OBD commands, reads replies up to the '>'
 * prompt, cleans them up and classifies adapter errors.
 *
 * Platform-free: no 3DS headers. The byte stream and the clock come from
 * an elm_io_t supplied by the caller (the TCP transport on the 3DS, a fake
 * in the host tests). Not thread-safe: use one elm_t from one thread.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Byte stream to the adapter. */
typedef struct {
    void *ctx;
    /* Write all len bytes. Return 0 on success, -1 on error. */
    int (*write)(void *ctx, const char *data, size_t len);
    /* Read up to len bytes, waiting at most timeout_ms (0 = don't wait).
     * Return the number of bytes read, 0 on timeout, -1 on error or closed. */
    int (*read)(void *ctx, char *buf, size_t len, unsigned timeout_ms);
    /* Monotonic clock in milliseconds. */
    uint64_t (*now_ms)(void *ctx);
} elm_io_t;

typedef enum {
    ELM_OK = 0,
    ELM_ERR_TIMEOUT,           /* no '>' prompt before the deadline */
    ELM_ERR_IO,                /* write / read failed or connection closed */
    ELM_ERR_OVERFLOW,          /* reply too long or too many lines */
    ELM_ERR_INVALID_ARG,       /* bad command string or argument */
    ELM_ERR_UNKNOWN_COMMAND,   /* adapter replied "?" */
    ELM_ERR_NO_DATA,           /* "NO DATA" */
    ELM_ERR_UNABLE_TO_CONNECT, /* "UNABLE TO CONNECT" */
    ELM_ERR_BUS_INIT,          /* "BUS INIT: ...ERROR" */
    ELM_ERR_CAN,               /* "CAN ERROR" */
    ELM_ERR_STOPPED,           /* "STOPPED" */
    ELM_ERR_ADAPTER,           /* other adapter / bus errors (BUS ERROR, BUFFER FULL, ERRxx, ...) */
    ELM_ERR_BAD_REPLY,         /* reply arrived but did not have the expected form */
    ELM_STATUS_COUNT
} elm_status_t;

/* Timeouts in milliseconds. */
#define ELM_TIMEOUT_DEFAULT_MS 2000u
#define ELM_TIMEOUT_RESET_MS   5000u  /* ATZ: adapter reboots */
#define ELM_TIMEOUT_SEARCH_MS 10000u  /* first OBD request after ATSP0 may search protocols */

#define ELM_COMMAND_MAX 32   /* longest command accepted, without the CR */
#define ELM_RAW_MAX   1024   /* bytes kept per reply, including the NUL */
#define ELM_LINE_MAX    96   /* chars per cleaned line, including the NUL */
#define ELM_MAX_LINES   16

typedef struct {
    elm_status_t status;
    /* Bytes received before the prompt, NUL bytes dropped (for logs / display). */
    char raw[ELM_RAW_MAX];
    size_t raw_len;
    /* Cleaned lines: trimmed, upper-case, printable ASCII only. Echo,
     * blank lines, "SEARCHING..." and "BUS INIT: ...OK" are removed. */
    char lines[ELM_MAX_LINES][ELM_LINE_MAX];
    int line_count;
    /* Index into lines of the line that set an error status, or -1. */
    int error_line;
} elm_response_t;

typedef struct {
    elm_io_t io;
} elm_t;

typedef struct {
    /* Adapter identity from ATZ, e.g. "ELM327 V1.5" (upper-case). */
    char id[ELM_LINE_MAX];
    /* Command that failed, or NULL on success. */
    const char *failed_command;
    /* Reply to the last command sent. */
    elm_response_t last;
} elm_startup_t;

void elm_bind(elm_t *elm, const elm_io_t *io);

/* Send cmd (no CR) and wait up to timeout_ms for the prompt. Fills resp and
 * returns resp->status. Stale bytes waiting before the command are discarded. */
elm_status_t elm_command(elm_t *elm, const char *cmd, unsigned timeout_ms,
                         elm_response_t *resp);

/* Init sequence: ATZ, ATE0, ATL0, ATSP0. Stops at the first failure. */
elm_status_t elm_startup(elm_t *elm, elm_startup_t *out);

/* ATRV: adapter supply voltage in millivolts. */
elm_status_t elm_read_voltage(elm_t *elm, int *millivolts, elm_response_t *resp);

/* ATDPN: current protocol number (0x0-0xC) and whether it was auto-detected. */
elm_status_t elm_read_protocol(elm_t *elm, int *protocol, bool *automatic,
                               elm_response_t *resp);

/* Parse a raw reply (bytes before the prompt) for command cmd. Used by
 * elm_command; exposed for tests. Sets status from adapter error lines only. */
void elm_parse_reply(const char *raw, size_t len, const char *cmd,
                     elm_response_t *resp);

/* "12.6V", "12,6V", "13" -> millivolts. Returns false if malformed. */
bool elm_parse_voltage(const char *line, int *millivolts);

/* "A6" -> 6 auto, "6" -> 6 manual, "A" -> 10 manual. Returns false if malformed. */
bool elm_parse_protocol(const char *line, int *protocol, bool *automatic);

const char *elm_status_str(elm_status_t status);

/* Protocol description for an ATDPN / ATSPx number, e.g. 6 ->
 * "ISO 15765-4 CAN (11 bit, 500 kbaud)". */
const char *elm_protocol_name(int protocol);

/* Copy raw adapter bytes as printable text for logs: CR -> \r, LF -> \n,
 * backslash -> \\, other non-printable bytes -> \xNN. Always
 * NUL-terminates (if out_len > 0) and ends with "..." when truncated.
 * Returns the length written. */
size_t elm_escape_raw(const char *raw, size_t len, char *out, size_t out_len);

#endif
