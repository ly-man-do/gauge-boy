#include "garage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/drive_list.h"
#include "obd/obd_vin.h"

#define LINE_MAX_LEN 160

static void copy(char *out, size_t len, const char *in)
{
    snprintf(out, len, "%s", in != NULL ? in : "");
}

static bool looks_like_vin(const char *key)
{
    return strlen(key) == OBD_VIN_LEN && strchr(key, '/') == NULL && obd_vin_chars_ok(key);
}

void garage_title(const char *key, int current_year, char *out, size_t len)
{
    out[0] = '\0';
    if (key == NULL || key[0] == '\0') {
        return;
    }
    if (looks_like_vin(key)) {
        int year = obd_vin_model_year(key, current_year);
        obd_vin_maker_t maker;
        obd_vin_model_t model;
        bool have_maker = obd_vin_maker(key, &maker);
        bool have_model = year > 0 && obd_vin_model(key, year, &model);
        const char *make = have_model ? model.make : have_maker ? maker.maker : NULL;
        if (year > 0) {
            snprintf(out, len, "%d", year);
        }
        if (make != NULL) {
            snprintf(out + strlen(out), len - strlen(out), "%s%s", out[0] ? " " : "", make);
        }
        if (have_model) {
            snprintf(out + strlen(out), len - strlen(out), "%s%s", out[0] ? " " : "",
                     model.model);
        }
        if (out[0] == '\0') {
            snprintf(out, len, "VIN %s", key);
        }
        return;
    }
    /* "make/model/year" */
    char make[32] = "", mdl[32] = "";
    int year = 0;
    const char *a = strchr(key, '/');
    const char *b = a != NULL ? strchr(a + 1, '/') : NULL;
    if (a == NULL || b == NULL) {
        copy(out, len, key);
        return;
    }
    snprintf(make, sizeof make, "%.*s", (int)(a - key), key);
    snprintf(mdl, sizeof mdl, "%.*s", (int)(b - a - 1), a + 1);
    year = atoi(b + 1);
    if (year > 0) {
        snprintf(out, len, "%d", year);
    }
    if (make[0] != '\0') {
        snprintf(out + strlen(out), len - strlen(out), "%s%s", out[0] ? " " : "", make);
    }
    if (mdl[0] != '\0') {
        snprintf(out + strlen(out), len - strlen(out), "%s%s", out[0] ? " " : "", mdl);
    }
}

int garage_add(garage_t *g, const char *key, int current_year)
{
    if (key == NULL || key[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < g->count; i++) {
        if (strcmp(g->car[i].key, key) == 0) {
            return i;
        }
    }
    if (g->count >= GARAGE_MAX) {
        return -1;
    }
    garage_car_t *c = &g->car[g->count];
    memset(c, 0, sizeof *c);
    copy(c->key, sizeof c->key, key);
    garage_title(key, current_year, c->title, sizeof c->title);
    if (looks_like_vin(key)) {
        copy(c->vin, sizeof c->vin, key);
    }
    return g->count++;
}

/* Calls fn with each line (copied, without its line break). */
static void each_line(const char *text, void (*fn)(void *ctx, char *line), void *ctx)
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
            fn(ctx, line);
        }
        p += n + (eol != NULL ? 1 : 0);
    }
}

typedef struct {
    garage_t *g;
    int year;
} add_ctx_t;

static void add_line(void *ctx, char *line)
{
    add_ctx_t *a = ctx;
    char *comma = strchr(line, ',');
    if (comma == NULL || line[0] == '#') {
        return;
    }
    *comma = '\0';
    if (strcmp(line, "car") != 0) {
        garage_add(a->g, line, a->year);
    }
}

void garage_add_from_csv(garage_t *g, const char *text, int current_year)
{
    add_ctx_t a = { g, current_year };
    each_line(text, add_line, &a);
}

static void nick_line(void *ctx, char *line)
{
    garage_t *g = ctx;
    char *comma = strchr(line, ',');
    if (comma == NULL) {
        return;
    }
    *comma = '\0';
    for (int i = 0; i < g->count; i++) {
        if (strcmp(g->car[i].key, line) == 0) {
            copy(g->car[i].nickname, sizeof g->car[i].nickname, comma + 1);
        }
    }
}

void garage_read_nicknames(garage_t *g, const char *text)
{
    each_line(text, nick_line, g);
}

typedef struct {
    const char *key;
    char *out;
    size_t len, used;
    bool ok;
} set_ctx_t;

static void put(set_ctx_t *s, const char *text)
{
    size_t n = strlen(text);
    if (s->used + n >= s->len) {
        s->ok = false;
        return;
    }
    memcpy(s->out + s->used, text, n + 1);
    s->used += n;
}

static void keep_line(void *ctx, char *line)
{
    set_ctx_t *s = ctx;
    char *comma = strchr(line, ',');
    if (comma == NULL || strncmp(line, "car,", 4) == 0) {
        return;
    }
    *comma = '\0';
    if (strcmp(line, s->key) == 0 || comma[1] == '\0') {
        return;
    }
    *comma = ',';
    put(s, line);
    put(s, "\n");
}

size_t garage_set_nickname(const char *text, const char *key, const char *nickname, char *out,
                           size_t len)
{
    if (len == 0) {
        return 0;
    }
    set_ctx_t s = { key, out, len, 0, true };
    out[0] = '\0';
    put(&s, "car,nickname\n");
    each_line(text, keep_line, &s);
    if (nickname != NULL && nickname[0] != '\0') {
        char clean[GARAGE_NICK_MAX + 1];
        size_t n = 0;
        for (const char *p = nickname; *p != '\0' && n < GARAGE_NICK_MAX; p++) {
            if (*p != ',' && *p != '\n' && *p != '\r') {
                clean[n++] = *p;
            }
        }
        clean[n] = '\0';
        put(&s, key);
        put(&s, ",");
        put(&s, clean);
        put(&s, "\n");
    }
    return s.ok ? s.used : 0;
}

int garage_find_drive(const garage_t *g, const char *vin, const char *car)
{
    if (vin != NULL && vin[0] != '\0') {
        for (int i = 0; i < g->count; i++) {
            if (strcmp(g->car[i].vin, vin) == 0) {
                return i;
            }
        }
    }
    if (car == NULL || car[0] == '\0') {
        return -1;
    }
    char text[64];
    copy(text, sizeof text, car);
    char *setup = strstr(text, " (set in SETUP)");
    if (setup != NULL) {
        *setup = '\0';
    }
    for (int i = 0; i < g->count; i++) {
        if (g->car[i].vin[0] == '\0' && strcmp(g->car[i].title, text) == 0) {
            return i;
        }
    }
    return -1;
}

/* ---------- distance ---------- */

bool drive_dist_begin(drive_dist_t *dd, const char *head, size_t len)
{
    memset(dd, 0, sizeof *dd);
    dd->col = -1;
    drive_column_t cols[DRIVE_COL_MAX];
    int n = drive_columns(head, len, cols, DRIVE_COL_MAX);
    for (int i = 0; i < n; i++) {
        if (strcmp(cols[i].name, "Speed") == 0) {
            dd->col = i;
            dd->to_kmh = strcmp(cols[i].unit, "mph") == 0 ? 1.609344f : 1.0f;
            return true;
        }
    }
    return false;
}

void drive_dist_line(drive_dist_t *dd, const char *line)
{
    double t;
    float v;
    if (dd->col < 0 || !drive_row_value(line, dd->col, &t, &v) || isnan(v)) {
        return;
    }
    float kmh = v * dd->to_kmh;
    if (dd->have && t > dd->t && t - dd->t <= GARAGE_GAP_S) {
        dd->km += ((double)dd->kmh + kmh) / 2.0 * (t - dd->t) / 3600.0;
    }
    dd->have = true;
    dd->t = t;
    dd->kmh = kmh;
}

/* ---------- cache ---------- */

typedef struct {
    const char *name;
    uint32_t size;
    bool found;
    double km;
    char vin[18], car[64];
} cache_ctx_t;

static void cache_line(void *ctx, char *line)
{
    cache_ctx_t *c = ctx;
    char *part[5];
    int n = 0;
    char *p = line;
    part[n++] = p;
    while (n < 5 && (p = strchr(p, ',')) != NULL) {
        *p++ = '\0';
        part[n++] = p;
    }
    if (n != 5 || strcmp(part[0], c->name) != 0 ||
        strtoul(part[1], NULL, 10) != (unsigned long)c->size) {
        return;
    }
    c->found = true;
    c->km = strtod(part[2], NULL);
    copy(c->vin, sizeof c->vin, part[3]);
    copy(c->car, sizeof c->car, part[4]);
}

bool garage_cache_get(const char *text, const char *name, uint32_t size, double *km, char *vin,
                      size_t vin_len, char *car, size_t car_len)
{
    cache_ctx_t c;
    memset(&c, 0, sizeof c);
    c.name = name;
    c.size = size;
    each_line(text, cache_line, &c);
    if (!c.found) {
        return false;
    }
    *km = c.km;
    if (vin != NULL) {
        copy(vin, vin_len, c.vin);
    }
    if (car != NULL) {
        copy(car, car_len, c.car);
    }
    return true;
}

bool garage_cache_add(char *out, size_t len, size_t *used, const char *name, uint32_t size,
                      double km, const char *vin, const char *car)
{
    char clean[64];
    size_t n = 0;
    for (const char *p = car != NULL ? car : ""; *p != '\0' && n < sizeof clean - 1; p++) {
        if (*p != ',' && *p != '\n' && *p != '\r') {
            clean[n++] = *p;
        }
    }
    clean[n] = '\0';
    int w = snprintf(out + *used, len - *used, "%s,%lu,%.3f,%s,%s\n", name, (unsigned long)size, km,
                     vin != NULL ? vin : "", clean);
    if (w < 0 || (size_t)w >= len - *used) {
        out[*used] = '\0';
        return false;
    }
    *used += (size_t)w;
    return true;
}
