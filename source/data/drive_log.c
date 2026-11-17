#include "drive_log.h"

#include <stdarg.h>
#include <string.h>

static bool flush_if(drive_log_t *dl, size_t room)
{
    if (dl->used + room <= sizeof dl->buf) {
        return !dl->failed;
    }
    return drive_log_flush(dl);
}

bool drive_log_flush(drive_log_t *dl)
{
    if (dl->failed) {
        dl->used = 0;
        return false;
    }
    if (dl->used > 0) {
        size_t n = dl->write(dl->ctx, dl->buf, dl->used);
        dl->bytes += n;
        if (n != dl->used) {
            dl->failed = true;
        }
        dl->used = 0;
    }
    return !dl->failed;
}

/* Appends text to the buffer (flushing first if it wouldn't fit). */
static void put(drive_log_t *dl, const char *text)
{
    size_t n = strlen(text);
    if (!flush_if(dl, n)) {
        return;
    }
    if (n > sizeof dl->buf) {   /* never in practice: lines are short */
        n = sizeof dl->buf;
    }
    memcpy(dl->buf + dl->used, text, n);
    dl->used += n;
}

static void putf(drive_log_t *dl, const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    put(dl, line);
}

/* A CSV field: quoted when it holds a comma, quote or line break. */
static void put_field(drive_log_t *dl, const char *text)
{
    if (strpbrk(text, ",\"\r\n") == NULL) {
        put(dl, text);
        return;
    }
    char out[256];
    size_t n = 0;
    out[n++] = '"';
    for (const char *p = text; *p != '\0' && n < sizeof out - 3; p++) {
        if (*p == '"') {
            out[n++] = '"';
        }
        out[n++] = *p;
    }
    out[n++] = '"';
    out[n] = '\0';
    put(dl, out);
}

static void meta_line(drive_log_t *dl, const char *key, const char *value)
{
    putf(dl, "# %s,", key);
    put_field(dl, value != NULL ? value : "");
    put(dl, "\n");
}

/* Decimals in the log: a little more than the screen shows. */
static int decimals(obd_unit_t unit)
{
    switch (unit) {
    case OBD_UNIT_RPM:
    case OBD_UNIT_SECONDS: return 0;
    case OBD_UNIT_GRAMS_S:
    case OBD_UNIT_VOLTS:   return 2;
    default:               return 1;
    }
}

static void put_value(drive_log_t *dl, float value, int places)
{
    char num[32];
    snprintf(num, sizeof num, "%.*f", places, (double)value);
    if (strcmp(num, "-0") == 0 || strcmp(num, "-0.0") == 0 || strcmp(num, "-0.00") == 0) {
        memmove(num, num + 1, strlen(num));   /* no "-0.0" for a tiny negative */
    }
    put(dl, num);
}

bool drive_log_open(drive_log_t *dl, drive_log_write_fn write, void *ctx,
                    const drive_log_meta_t *meta, const obd_live_t *lv, obd_units_t units,
                    uint32_t period_ms, uint64_t now_ms)
{
    memset(dl, 0, sizeof *dl);
    dl->write = write;
    dl->ctx = ctx;
    dl->units = units;
    dl->period_ms = period_ms != 0 ? period_ms : DRIVE_LOG_RATE_MS;
    dl->start_ms = now_ms;
    dl->next_ms = now_ms;
    for (int i = 0; i < lv->count && dl->count < OBD_LIVE_MAX; i++) {
        dl->pid[dl->count++] = lv->pid[i].pid;
        if (lv->pid[i].pid == OBD_PID_MAP || lv->pid[i].pid == OBD_PID_MAP_WIDE) {
            dl->vacuum = true;
        }
    }

    put(dl, "# GaugeBoy drive log\n");
    meta_line(dl, "version", meta->version);
    meta_line(dl, "date", meta->date);
    meta_line(dl, "car", meta->car);
    meta_line(dl, "vin", meta->vin);
    meta_line(dl, "protocol", meta->protocol);
    meta_line(dl, "units", units == OBD_UNITS_US ? "US" : "metric");
    putf(dl, "# rate_hz,%g\n", 1000.0 / (double)dl->period_ms);

    put(dl, "time_s");
    for (int i = 0; i < dl->count; i++) {
        const obd_pid_info_t *info = obd_pid_info(dl->pid[i]);
        char name[64];
        snprintf(name, sizeof name, "%s (%s)", info->label, obd_unit_label(info->unit, units));
        put(dl, ",");
        put_field(dl, name);
    }
    putf(dl, ",Battery (V)");
    if (dl->vacuum) {
        putf(dl, ",Vacuum (%s)", obd_unit_label(OBD_UNIT_KPA, units));
    }
    put(dl, "\n");
    return drive_log_flush(dl);
}

bool drive_log_sample(drive_log_t *dl, const obd_live_t *lv, uint64_t now_ms)
{
    if (dl->failed || now_ms < dl->next_ms) {
        return false;
    }
    dl->next_ms += dl->period_ms;
    if (dl->next_ms <= now_ms) {
        dl->next_ms = now_ms + dl->period_ms;   /* fell behind: don't catch up */
    }

    put_value(dl, (float)(now_ms - dl->start_ms) / 1000.0f, 3);
    for (int i = 0; i < dl->count; i++) {
        put(dl, ",");
        float v = 0.0f;
        if (obd_live_value(lv, dl->pid[i], &v)) {
            const obd_pid_info_t *info = obd_pid_info(dl->pid[i]);
            put_value(dl, obd_unit_convert(info->unit, v, dl->units), decimals(info->unit));
        }
    }
    put(dl, ",");
    if (lv->have_volts) {
        put_value(dl, lv->volts, 2);
    }
    if (dl->vacuum) {
        put(dl, ",");
        float vac = 0.0f;
        if (obd_live_vacuum(lv, &vac)) {
            put_value(dl, obd_unit_convert(OBD_UNIT_KPA, vac, dl->units), 1);
        }
    }
    put(dl, "\n");
    dl->rows++;
    return !dl->failed;
}

char *drive_log_file_name(char *out, size_t len, const struct tm *when)
{
    snprintf(out, len, "drive-%04d%02d%02d-%02d%02d%02d.csv", when->tm_year + 1900,
             when->tm_mon + 1, when->tm_mday, when->tm_hour, when->tm_min, when->tm_sec);
    return out;
}

size_t drive_log_stdio_write(void *ctx, const char *data, size_t len)
{
    return fwrite(data, 1, len, (FILE *)ctx);
}
