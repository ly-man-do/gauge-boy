#ifndef GAUGEBOY_DRIVE_LOG_H
#define GAUGEBOY_DRIVE_LOG_H

/*
 * Drive log: the polled live values written as CSV (brief: M3 logging),
 * one row per sample at a fixed rate. Platform-free: output goes through
 * a write callback (drive_log_stdio_write for a FILE *), so host tests can
 * capture it. The app writes on its worker thread, never the render loop.
 *
 *   # Gauge Boy drive log
 *   # version,0.2.0+13b94ad
 *   # date,2026-11-16 17:12:30
 *   # car,2023 Subaru Ascent
 *   # vin,4S4...
 *   # protocol,"ISO 15765-4 CAN (11 bit, 500 kbaud)"
 *   # units,US
 *   # rate_hz,5
 *   time_s,RPM (rpm),Speed (mph),Coolant (F),Battery (V),Vacuum (inHg)
 *   0.000,750,0,181.4,14.10,18.2
 *   0.200,752,0,,14.10,18.3
 *
 * A value not read yet is an empty cell. Vacuum is negative under boost.
 * When both trims of a bank are polled, a Total B1 / B2 column (short +
 * long term) follows the PIDs.
 * Battery is the adapter's ATRV; Vacuum appears when MAP is polled.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "obd/obd_live.h"
#include "obd/obd_pid.h"

#define DRIVE_LOG_DIR      "drives"
#define DRIVE_LOG_RATE_MS  200u      /* 5 rows a second */
#define DRIVE_LOG_BUF      4096

/* Writes len bytes; returns how many were written (short = error). */
typedef size_t (*drive_log_write_fn)(void *ctx, const char *data, size_t len);

typedef struct {
    const char *version;    /* GB_VERSION_TEXT */
    const char *date;       /* "YYYY-MM-DD HH:MM:SS" */
    const char *car;        /* "2023 Subaru Ascent", or "" */
    const char *vin;        /* or "" */
    const char *protocol;   /* elm_protocol_name() */
    const char *baro;       /* where vacuum's baro comes from, or NULL */
} drive_log_meta_t;

typedef struct {
    drive_log_write_fn write;
    void *ctx;
    obd_units_t units;
    uint32_t period_ms;
    uint8_t pid[OBD_LIVE_MAX];   /* the columns, in order */
    int count;
    bool vacuum;                 /* a Vacuum column (MAP polled) */
    bool total_b1, total_b2;     /* Total B1 / B2 columns (both trims of a bank polled) */
    uint64_t start_ms;
    uint64_t next_ms;
    uint32_t rows;
    uint64_t bytes;              /* written so far, header included */
    bool failed;                 /* a write came up short: stop */
    char buf[DRIVE_LOG_BUF];
    size_t used;
} drive_log_t;

/* Starts a log of lv's polled PIDs (plus battery and, with MAP, vacuum)
 * and writes the header. period_ms 0 = DRIVE_LOG_RATE_MS. Returns false
 * if the header couldn't be written. */
bool drive_log_open(drive_log_t *dl, drive_log_write_fn write, void *ctx,
                    const drive_log_meta_t *meta, const obd_live_t *lv, obd_units_t units,
                    uint32_t period_ms, uint64_t now_ms);

/* Writes a row of lv's latest values if one is due at now_ms (rows are
 * period_ms apart; after a long gap the next one is period_ms from now).
 * Returns true if a row was added. Buffered: see drive_log_flush. */
bool drive_log_sample(drive_log_t *dl, const obd_live_t *lv, uint64_t now_ms);

/* Writes out the buffered rows. False once a write has failed. */
bool drive_log_flush(drive_log_t *dl);

/* "drive-20261116-171230.csv" for a local time. Returns out. */
char *drive_log_file_name(char *out, size_t len, const struct tm *when);

/* drive_log_write_fn for a stdio FILE * (ctx). */
size_t drive_log_stdio_write(void *ctx, const char *data, size_t len);

#endif
