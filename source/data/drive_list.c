#include "drive_list.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool drive_list_is_log_name(const char *name)
{
    size_t n = strlen(name);
    return n > 10 && n < 32 && strncmp(name, "drive-", 6) == 0 &&
           strcmp(name + n - 4, ".csv") == 0;
}

static int newest_first(const void *a, const void *b)
{
    return strcmp((const char *)b, (const char *)a);
}

void drive_list_sort(char (*names)[32], int n)
{
    qsort(names, (size_t)n, sizeof names[0], newest_first);
}

/* One line of text[0..len) starting at *pos, without the line break. */
static bool next_line(const char *text, size_t len, size_t *pos, char *out, size_t out_len,
                      bool *complete)
{
    if (*pos >= len) {
        return false;
    }
    size_t start = *pos, end = start;
    while (end < len && text[end] != '\n') {
        end++;
    }
    *complete = end < len;
    *pos = end < len ? end + 1 : end;
    if (end > start && text[end - 1] == '\r') {
        end--;
    }
    size_t n = end - start < out_len - 1 ? end - start : out_len - 1;
    memcpy(out, text + start, n);
    out[n] = '\0';
    return true;
}

/* A CSV field's value: quotes removed, "" as one quote. */
static void unquote(const char *in, char *out, size_t out_len)
{
    size_t n = 0;
    if (*in != '"') {
        snprintf(out, out_len, "%s", in);
        return;
    }
    for (const char *p = in + 1; *p != '\0' && n < out_len - 1; p++) {
        if (*p == '"') {
            if (p[1] != '"') {
                break;
            }
            p++;
        }
        out[n++] = *p;
    }
    out[n] = '\0';
}

/* Copies src into dst (size bytes), cut to fit. */
static void copy(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* The first line of a drive log; logs from before the name change say
 * "GaugeBoy". */
static bool is_magic(const char *line)
{
    return strcmp(line, "# Gauge Boy drive log") == 0 || strcmp(line, "# GaugeBoy drive log") == 0;
}

static void meta(drive_info_t *d, const char *key, const char *value)
{
    char v[96];
    unquote(value, v, sizeof v);
    if (strcmp(key, "date") == 0) {
        copy(d->date, sizeof d->date, v);
    } else if (strcmp(key, "car") == 0) {
        copy(d->car, sizeof d->car, v);
    } else if (strcmp(key, "vin") == 0) {
        copy(d->vin, sizeof d->vin, v);
    } else if (strcmp(key, "protocol") == 0) {
        copy(d->protocol, sizeof d->protocol, v);
    } else if (strcmp(key, "version") == 0) {
        copy(d->version, sizeof d->version, v);
    } else if (strcmp(key, "units") == 0) {
        d->metric = strcmp(v, "metric") == 0;
    } else if (strcmp(key, "rate_hz") == 0) {
        d->rate_hz = strtof(v, NULL);
    }
}

bool drive_info_parse_head(drive_info_t *d, const char *text, size_t len)
{
    d->ok = false;
    d->date[0] = d->car[0] = d->vin[0] = d->protocol[0] = d->version[0] = '\0';
    d->values[0] = '\0';
    d->metric = false;
    d->rate_hz = 0.0f;
    d->columns = 0;
    d->duration_s = -1.0f;

    size_t pos = 0;
    char line[512];
    bool complete = false;
    bool magic = false;
    while (next_line(text, len, &pos, line, sizeof line, &complete)) {
        if (line[0] == '#') {
            if (is_magic(line)) {
                magic = true;
                continue;
            }
            char *comma = strchr(line, ',');
            if (comma != NULL && line[1] == ' ') {
                *comma = '\0';
                meta(d, line + 2, comma + 1);
            }
            continue;
        }
        /* The column header: time_s,RPM (rpm),Speed (mph),... */
        if (!magic || !complete || strncmp(line, "time_s,", 7) != 0) {
            break;
        }
        for (char *col = strtok(line + 7, ","); col != NULL; col = strtok(NULL, ",")) {
            char *unit = strstr(col, " (");
            if (unit != NULL) {
                *unit = '\0';
            }
            size_t used = strlen(d->values);
            snprintf(d->values + used, sizeof d->values - used, "%s%s", used ? ", " : "", col);
            d->columns++;
        }
        d->ok = true;
        break;
    }
    if (d->date[0] == '\0' && strlen(d->name) >= 21) {
        /* No date line: from the name, drive-YYYYMMDD-HHMMSS.csv. */
        const char *n = d->name + 6;
        snprintf(d->date, sizeof d->date, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s", n, n + 4, n + 6,
                 n + 9, n + 11, n + 13);
    }
    return d->ok;
}

float drive_info_parse_tail(const char *text, size_t len)
{
    /* Walk back over a cut-off last line to the last complete one. */
    size_t end = len;
    while (end > 0 && text[end - 1] != '\n') {
        end--;
    }
    while (end > 0) {
        size_t stop = end - 1;          /* this line's '\n' */
        size_t start = stop;
        while (start > 0 && text[start - 1] != '\n') {
            start--;
        }
        if (start == 0 && len == DRIVE_TAIL_BYTES) {
            return -1.0f;               /* may be cut at the front */
        }
        char first[24];
        size_t n = 0;
        while (start + n < stop && text[start + n] != ',' && n < sizeof first - 1) {
            first[n] = text[start + n];
            n++;
        }
        first[n] = '\0';
        char *num_end = NULL;
        float t = strtof(first, &num_end);
        if (n > 0 && num_end != NULL && *num_end == '\0' && first[0] != '#') {
            return t;
        }
        end = start;
    }
    return -1.0f;
}

static void duration_text(float secs, char *out, size_t len)
{
    if (secs < 0.0f) {
        snprintf(out, len, "?");
        return;
    }
    unsigned s = (unsigned)(secs + 0.5f);
    if (s >= 3600) {
        snprintf(out, len, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
    } else {
        snprintf(out, len, "%u:%02u", s / 60, s % 60);
    }
}

void drive_info_row(const drive_info_t *d, char *out, size_t len, int cols)
{
    char when[12] = "";
    if (strlen(d->date) >= 16) {
        snprintf(when, sizeof when, "%.5s %.5s", d->date + 5, d->date + 11);   /* MM-DD HH:MM */
    }
    char dur[24];
    duration_text(d->duration_s, dur, sizeof dur);
    const char *car = !d->ok ? "(not a drive log)" : d->car[0] ? d->car : "Car not known";
    int car_w = cols - 11 - 1 - (int)strlen(dur) - 1;
    if (car_w < 4) {
        car_w = 4;
    }
    snprintf(out, len, "%-11s %-*.*s %s", when, car_w, car_w, car, dur);
}

static void add(char lines[][DRIVE_INFO_COLS + 1], int max, int *n, const char *key,
                const char *value)
{
    if (*n >= max) {
        return;
    }
    char line[DRIVE_INFO_COLS * 2 + 16];
    snprintf(line, sizeof line, "%-10s%s", key, value);
    copy(lines[(*n)++], DRIVE_INFO_COLS + 1, line);
}

int drive_info_lines(const drive_info_t *d, char lines[][DRIVE_INFO_COLS + 1], int max)
{
    int n = 0;
    char buf[200];
    add(lines, max, &n, "File", d->name);
    if (!d->ok) {
        add(lines, max, &n, "", "Not a Gauge Boy drive log.");
        return n;
    }
    add(lines, max, &n, "Date", d->date);
    add(lines, max, &n, "Car", d->car[0] ? d->car : "not known (no VIN read)");
    if (d->vin[0]) {
        add(lines, max, &n, "VIN", d->vin);
    }
    add(lines, max, &n, "Protocol", d->protocol);
    char dur[24];
    duration_text(d->duration_s, dur, sizeof dur);
    if (d->duration_s >= 0.0f && d->rate_hz > 0.0f) {
        snprintf(buf, sizeof buf, "%s (about %u rows at %g a second)", dur,
                 (unsigned)(d->duration_s * d->rate_hz + 1.5f), (double)d->rate_hz);
    } else {
        snprintf(buf, sizeof buf, "%s", dur);
    }
    add(lines, max, &n, "Length", buf);
    snprintf(buf, sizeof buf, "%u KB, %s units", (unsigned)((d->size + 1023) / 1024),
             d->metric ? "metric" : "US");
    add(lines, max, &n, "Size", buf);
    /* The values, wrapped at commas. */
    const char *key = "Values";
    const char *p = d->values;
    int width = DRIVE_INFO_COLS - 10;
    while (*p != '\0' && n < max) {
        int take = (int)strlen(p);
        if (take > width) {
            take = width;
            while (take > 0 && p[take] != ' ') {
                take--;
            }
            if (take == 0) {
                take = width;
            }
        }
        snprintf(buf, sizeof buf, "%.*s", take, p);
        add(lines, max, &n, key, buf);
        key = "";
        p += take;
        while (*p == ' ') {
            p++;
        }
    }
    if (d->version[0]) {
        add(lines, max, &n, "Recorded", d->version);
    }
    return n;
}

int drive_columns(const char *head, size_t len, drive_column_t *cols, int max)
{
    size_t pos = 0;
    char line[512];
    bool complete = false;
    bool magic = false;
    while (next_line(head, len, &pos, line, sizeof line, &complete)) {
        if (line[0] == '#') {
            magic = magic || is_magic(line);
            continue;
        }
        if (!magic || !complete || strncmp(line, "time_s,", 7) != 0) {
            return 0;
        }
        int n = 0;
        for (char *col = strtok(line + 7, ","); col != NULL && n < max; col = strtok(NULL, ",")) {
            drive_column_t *c = &cols[n++];
            c->unit[0] = '\0';
            char *open = strstr(col, " (");
            if (open != NULL) {
                *open = '\0';
                char *close = strchr(open + 2, ')');
                if (close != NULL) {
                    *close = '\0';
                }
                copy(c->unit, sizeof c->unit, open + 2);
            }
            copy(c->name, sizeof c->name, col);
        }
        return n;
    }
    return 0;
}

bool drive_row_value(const char *line, int col, double *t, float *v)
{
    if (line[0] == '#' || line[0] == '\0') {
        return false;
    }
    char *end = NULL;
    *t = strtod(line, &end);
    if (end == line || *end != ',') {
        return false;   /* the column header, or a cut-off line */
    }
    const char *p = end + 1;
    for (int i = 0; i < col; i++) {
        p = strchr(p, ',');
        if (p == NULL) {
            *v = NAN;
            return true;
        }
        p++;
    }
    if (*p == ',' || *p == '\0' || *p == '\r' || *p == '\n') {
        *v = NAN;
        return true;
    }
    *v = strtof(p, &end);
    if (end == p) {
        *v = NAN;
    }
    return true;
}
