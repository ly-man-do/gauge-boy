#include "baro_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER "car,kpa,date\n"

void baro_store_key(const obd_diag_t *d, const char *setup_make, const char *setup_model,
                    int setup_year, char *out, size_t len)
{
    out[0] = '\0';
    if (d != NULL && d->have_vin) {
        snprintf(out, len, "%s", d->vin);
    } else if ((setup_make != NULL && setup_make[0]) || (setup_model != NULL && setup_model[0])) {
        snprintf(out, len, "%s/%s/%d", setup_make != NULL ? setup_make : "",
                 setup_model != NULL ? setup_model : "", setup_year);
    }
    for (char *p = out; *p != '\0'; p++) {
        if (*p == ',' || *p == '\n' || *p == '\r') {
            *p = ' ';   /* keep the CSV simple */
        }
    }
}

/* Splits "key,kpa,date" (in place). False for the header or a bad line. */
static bool split(char *line, char **key, float *kpa, char **date)
{
    char *a = strchr(line, ',');
    if (a == NULL) {
        return false;
    }
    *a = '\0';
    char *b = strchr(a + 1, ',');
    if (b == NULL) {
        return false;
    }
    *b = '\0';
    char *end = NULL;
    *kpa = strtof(a + 1, &end);
    if (end == a + 1 || strcmp(line, "car") == 0) {
        return false;
    }
    *key = line;
    *date = b + 1;
    return true;
}

bool baro_store_get(const char *text, const char *key, float *kpa, char *date, size_t date_len)
{
    if (text == NULL || key == NULL || key[0] == '\0') {
        return false;
    }
    bool found = false;
    const char *p = text;
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t n = eol != NULL ? (size_t)(eol - p) : strlen(p);
        char line[128];
        if (n < sizeof line) {
            memcpy(line, p, n);
            line[n] = '\0';
            if (n > 0 && line[n - 1] == '\r') {
                line[n - 1] = '\0';
            }
            char *k, *dt;
            float v;
            if (split(line, &k, &v, &dt) && strcmp(k, key) == 0) {
                *kpa = v;   /* the last line for a key wins */
                if (date != NULL) {
                    snprintf(date, date_len, "%s", dt);
                }
                found = true;
            }
        }
        p += n + (eol != NULL ? 1 : 0);
    }
    return found;
}

size_t baro_store_set(const char *text, const char *key, float kpa, const char *date, char *out,
                      size_t len)
{
    /* Keep the other cars' lines (newest last), then add this one. */
    char keep[BARO_STORE_MAX][128];
    int n = 0;
    const char *p = text != NULL ? text : "";
    while (*p != '\0') {
        const char *eol = strchr(p, '\n');
        size_t m = eol != NULL ? (size_t)(eol - p) : strlen(p);
        char line[128], copy[128];
        if (m < sizeof line) {
            memcpy(line, p, m);
            line[m] = '\0';
            if (m > 0 && line[m - 1] == '\r') {
                line[m - 1] = '\0';
            }
            memcpy(copy, line, sizeof copy);
            char *k, *dt;
            float v;
            if (split(line, &k, &v, &dt) && strcmp(k, key) != 0) {
                if (n == BARO_STORE_MAX - 1) {   /* room for the new one: drop the oldest */
                    memmove(keep[0], keep[1], sizeof keep[0] * (BARO_STORE_MAX - 2));
                    n--;
                }
                memcpy(keep[n++], copy, sizeof copy);
            }
        }
        p += m + (eol != NULL ? 1 : 0);
    }
    size_t used = 0;
    int w = snprintf(out, len, HEADER);
    if (w < 0 || (size_t)w >= len) {
        return 0;
    }
    used = (size_t)w;
    for (int i = 0; i < n; i++) {
        w = snprintf(out + used, len - used, "%s\n", keep[i]);
        if (w < 0 || (size_t)w >= len - used) {
            return 0;
        }
        used += (size_t)w;
    }
    w = snprintf(out + used, len - used, "%s,%.1f,%s\n", key, (double)kpa, date);
    if (w < 0 || (size_t)w >= len - used) {
        return 0;
    }
    return used + (size_t)w;
}
