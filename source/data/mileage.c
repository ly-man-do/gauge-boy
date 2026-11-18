#include "mileage.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER "car,date,start_km,end_km,fuel_l,price_l\n"
#define LINE_MAX_LEN 160

/* Splits "car,date,start,end,fuel[,price]" (in place). False for the
 * header or a bad line. */
static bool split(char *line, char **car, mileage_entry_t *e)
{
    char *part[6];
    int n = 0;
    char *p = line;
    part[n++] = p;
    while (n < 6 && (p = strchr(p, ',')) != NULL) {
        *p++ = '\0';
        part[n++] = p;
    }
    if (n < 5 || strcmp(part[0], "car") == 0 || part[0][0] == '\0') {
        return false;
    }
    e->price_l = n == 6 ? strtod(part[5], NULL) : 0.0;
    if (e->price_l < 0.0) {
        e->price_l = 0.0;
    }
    char *end;
    e->start_km = strtod(part[2], &end);
    if (end == part[2]) {
        return false;
    }
    e->end_km = strtod(part[3], &end);
    if (end == part[3]) {
        return false;
    }
    e->fuel_l = strtod(part[4], &end);
    if (end == part[4]) {
        return false;
    }
    snprintf(e->date, sizeof e->date, "%s", part[1]);
    *car = part[0];
    return true;
}

/* Each line of text, copied without its line break, to fn. */
static void each_line(const char *text, void (*fn)(void *ctx, const char *raw, char *line),
                      void *ctx)
{
    const char *p = text != NULL ? text : "";
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t n = eol != NULL ? (size_t)(eol - p) : strlen(p);
        char line[LINE_MAX_LEN];
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = '\0';
            if (n > 0 && line[n - 1] == '\r') {
                line[n - 1] = '\0';
            }
            fn(ctx, p, line);
        }
        p += n + (eol != NULL ? 1 : 0);
    }
}

typedef struct {
    const char *car;
    mileage_list_t *out;
} read_ctx_t;

static void read_line(void *ctx, const char *raw, char *line)
{
    (void)raw;
    read_ctx_t *r = ctx;
    char *car;
    mileage_entry_t e;
    if (!split(line, &car, &e) || strcmp(car, r->car) != 0) {
        return;
    }
    mileage_list_t *o = r->out;
    if (o->n == MILEAGE_MAX) {   /* keep the newest */
        memmove(&o->e[0], &o->e[1], sizeof o->e[0] * (MILEAGE_MAX - 1));
        o->n--;
        o->first++;
    }
    o->e[o->n++] = e;
}

int mileage_read(const char *text, const char *car, mileage_list_t *out)
{
    memset(out, 0, sizeof *out);
    if (car == NULL || car[0] == '\0') {
        return 0;
    }
    read_ctx_t r = { car, out };
    each_line(text, read_line, &r);
    for (int i = 0; i < out->n; i++) {
        double km = out->e[i].end_km - out->e[i].start_km;
        out->km += km;
        out->fuel_l += out->e[i].fuel_l;
        if (out->e[i].price_l > 0.0) {
            out->cost += mileage_cost(&out->e[i]);
            out->cost_km += km;
        }
    }
    return out->n;
}

static void format_entry(char *line, size_t len, const char *car, const mileage_entry_t *e);

/* Appends to out; false if it doesn't fit. */
static bool put(char *out, size_t len, size_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n >= len) {
        return false;
    }
    memcpy(out + *used, text, n + 1);
    *used += n;
    return true;
}

size_t mileage_add(const char *text, const char *car, const mileage_entry_t *e, char *out,
                   size_t len)
{
    const char *t = text != NULL ? text : "";
    size_t used = 0;
    if (len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (strncmp(t, "car,", 4) != 0 && !put(out, len, &used, HEADER)) {
        return 0;
    }
    if (!put(out, len, &used, t)) {
        return 0;
    }
    size_t tl = strlen(t);
    if (tl > 0 && t[tl - 1] != '\n' && !put(out, len, &used, "\n")) {
        return 0;
    }
    char line[LINE_MAX_LEN];
    format_entry(line, sizeof line, car, e);
    if (!put(out, len, &used, line)) {
        return 0;
    }
    return used;
}

typedef struct {
    const char *car;
    const char *last;   /* the raw start of car's newest line */
} last_ctx_t;

static void last_line(void *ctx, const char *raw, char *line)
{
    last_ctx_t *l = ctx;
    char *car;
    mileage_entry_t e;
    if (split(line, &car, &e) && strcmp(car, l->car) == 0) {
        l->last = raw;
    }
}

size_t mileage_delete_last(const char *text, const char *car, bool *removed, char *out,
                           size_t len)
{
    const char *t = text != NULL ? text : "";
    last_ctx_t l = { car, NULL };
    each_line(t, last_line, &l);
    *removed = l.last != NULL;
    size_t tl = strlen(t);
    size_t cut = l.last != NULL ? (size_t)(l.last - t) : tl;
    const char *after = l.last != NULL ? strchr(l.last, '\n') : NULL;
    size_t rest = after != NULL ? tl - (size_t)(after + 1 - t) : 0;
    if (cut + rest >= len) {
        return 0;
    }
    memcpy(out, t, cut);
    if (rest > 0) {
        memcpy(out + cut, after + 1, rest);
    }
    out[cut + rest] = '\0';
    return cut + rest;
}

static void format_entry(char *line, size_t len, const char *car, const mileage_entry_t *e)
{
    snprintf(line, len, "%.63s,%.15s,%.1f,%.1f,%.3f,%.4f\n", car, e->date, e->start_km, e->end_km,
             e->fuel_l, e->price_l > 0.0 ? e->price_l : 0.0);
}

size_t mileage_rewrite(const char *text, const char *car, int index, const mileage_entry_t *e,
                       bool *found, char *out, size_t len)
{
    const char *p = text != NULL ? text : "";
    size_t used = 0;
    int k = 0;
    *found = false;
    if (len == 0) {
        return 0;
    }
    out[0] = '\0';
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t n = eol != NULL ? (size_t)(eol - p) + 1 : strlen(p);
        char line[LINE_MAX_LEN];
        bool target = false;
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = '\0';
            line[strcspn(line, "\r\n")] = '\0';
            char *c;
            mileage_entry_t parsed;
            if (split(line, &c, &parsed) && strcmp(c, car) == 0) {
                target = k++ == index;
            }
        }
        if (target) {
            *found = true;
            if (e != NULL) {
                char repl[LINE_MAX_LEN];
                format_entry(repl, sizeof repl, car, e);
                if (!put(out, len, &used, repl)) {
                    return 0;
                }
            }
        } else {
            if (used + n >= len) {
                return 0;
            }
            memcpy(out + used, p, n);
            used += n;
            out[used] = '\0';
        }
        p += n;
    }
    return used;
}

bool mileage_l100(double km, double fuel_l, double *l100)
{
    if (km <= 0.0 || fuel_l <= 0.0) {
        return false;
    }
    *l100 = fuel_l / km * 100.0;
    return true;
}

bool mileage_mpg(double km, double fuel_l, double *mpg)
{
    if (km <= 0.0 || fuel_l <= 0.0) {
        return false;
    }
    *mpg = (km / MILEAGE_KM_MI) / (fuel_l / MILEAGE_L_GAL);
    return true;
}

bool mileage_parse(const char *text, double *v)
{
    char clean[32];
    size_t n = 0;
    bool digit = false, dot = false;
    for (const char *p = text != NULL ? text : ""; *p != '\0'; p++) {
        if (*p == ',' || *p == ' ') {
            continue;   /* thousands separators, spaces */
        }
        if (*p == '.' && !dot) {
            dot = true;
        } else if (isdigit((unsigned char)*p)) {
            digit = true;
        } else {
            return false;
        }
        if (n + 1 >= sizeof clean) {
            return false;
        }
        clean[n++] = *p;
    }
    clean[n] = '\0';
    if (!digit) {
        return false;
    }
    *v = strtod(clean, NULL);
    return true;
}

double mileage_cost(const mileage_entry_t *e)
{
    return e->price_l > 0.0 ? e->fuel_l * e->price_l : 0.0;
}

const char *mileage_check(double start_km, double end_km, double fuel_l)
{
    if (start_km < 0.0 || end_km < 0.0) {
        return "Odometer readings can't be negative.";
    }
    if (end_km <= start_km) {
        return "The end reading must be past the start.";
    }
    if (end_km - start_km > 5000.0 * MILEAGE_KM_MI) {
        return "Over 5,000 miles: check the readings.";
    }
    if (fuel_l <= 0.0) {
        return "Enter the fuel used.";
    }
    return NULL;
}
