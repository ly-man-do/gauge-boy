#include "track_bests.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER  "car,run,seconds,extra,date\n"
#define LINE_MAX_LEN 128

typedef struct {
    char *car, *run, *date;
    float seconds, extra;
} fields_t;

/* Splits "car,run,seconds,extra,date" in place. False for the header or a bad line. */
static bool split(char *line, fields_t *f)
{
    char *part[5];
    int n = 0;
    char *p = line;
    part[n++] = p;
    while (n < 5 && (p = strchr(p, ',')) != NULL) {
        *p++ = '\0';
        part[n++] = p;
    }
    if (n != 5 || strcmp(part[0], "car") == 0 || part[0][0] == '\0') {
        return false;
    }
    char *end = NULL;
    f->seconds = strtof(part[2], &end);
    if (end == part[2] || f->seconds <= 0.0f) {
        return false;
    }
    f->extra = part[3][0] != '\0' ? strtof(part[3], NULL) : 0.0f;
    f->car = part[0];
    f->run = part[1];
    f->date = part[4];
    return true;
}

/* Calls fn for each line (copied, without the line break). */
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
    const char *car, *run;
    track_best_t *out;
    bool found;
} find_t;

static void find_line(void *ctx, char *line)
{
    find_t *q = ctx;
    fields_t f;
    if (split(line, &f) && strcmp(f.car, q->car) == 0 && strcmp(f.run, q->run) == 0) {
        q->out->seconds = f.seconds;   /* the last line for a pair wins */
        q->out->extra = f.extra;
        snprintf(q->out->date, sizeof q->out->date, "%s", f.date);
        q->found = true;
    }
}

bool track_bests_get(const char *text, const char *car, const char *run, track_best_t *out)
{
    if (car == NULL || car[0] == '\0' || run == NULL) {
        return false;
    }
    track_best_t tmp;
    find_t q = { car, run, out != NULL ? out : &tmp, false };
    each_line(text, find_line, &q);
    return q.found;
}

typedef struct {
    const char *car, *run;
    char keep[TRACK_BESTS_MAX][LINE_MAX_LEN];
    int n;
} keep_t;

static void keep_line(void *ctx, char *line)
{
    keep_t *k = ctx;
    char copy[LINE_MAX_LEN];
    memcpy(copy, line, sizeof copy);
    fields_t f;
    if (!split(line, &f) || (strcmp(f.car, k->car) == 0 && strcmp(f.run, k->run) == 0)) {
        return;
    }
    if (k->n == TRACK_BESTS_MAX - 1) {   /* room for the new one: drop the oldest */
        memmove(k->keep[0], k->keep[1], sizeof k->keep[0] * (TRACK_BESTS_MAX - 2));
        k->n--;
    }
    memcpy(k->keep[k->n++], copy, sizeof copy);
}

/* Appends to out; false if it doesn't fit. */
static bool add(char *out, size_t len, size_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n >= len) {
        return false;
    }
    memcpy(out + *used, text, n + 1);
    *used += n;
    return true;
}

size_t track_bests_offer(const char *text, const char *car, const char *run, float seconds,
                         float extra, const char *date, bool *record, char *out, size_t len)
{
    *record = false;
    if (len == 0) {
        return 0;
    }
    out[0] = '\0';
    track_best_t old;
    bool beat = car != NULL && car[0] != '\0' && seconds > 0.0f &&
                (!track_bests_get(text, car, run, &old) || seconds < old.seconds);
    if (!beat) {
        size_t n = text != NULL ? strlen(text) : 0;
        if (n >= len) {
            return 0;
        }
        memcpy(out, text != NULL ? text : "", n + 1);
        return n;
    }
    static keep_t k;
    memset(&k, 0, sizeof k);
    k.car = car;
    k.run = run;
    each_line(text, keep_line, &k);

    size_t used = 0;
    if (!add(out, len, &used, HEADER)) {
        return 0;
    }
    for (int i = 0; i < k.n; i++) {
        if (!add(out, len, &used, k.keep[i]) || !add(out, len, &used, "\n")) {
            return 0;
        }
    }
    char line[LINE_MAX_LEN], ex[16] = "";
    if (extra > 0.0f) {
        snprintf(ex, sizeof ex, "%.1f", (double)extra);
    }
    snprintf(line, sizeof line, "%.63s,%.15s,%.2f,%s,%.15s\n", car, run, (double)seconds, ex,
             date != NULL ? date : "");
    if (!add(out, len, &used, line)) {
        return 0;
    }
    *record = true;
    return used;
}
