#include "home_top.h"

#include <stdio.h>
#include <string.h>

#define KM_PER_MI 1.609344

static double to_unit(const home_top_t *t, double km)
{
    return t->metric ? km : km / KM_PER_MI;
}

void home_top_car(const home_top_t *t, char *line1, size_t len1, char *line2, size_t len2)
{
    if (!t->have_car) {
        snprintf(line1, len1, "No car yet");
        snprintf(line2, len2, "%s", t->connected ? "Tap DIAG, then SCAN to read it"
                                                 : "Connect the adapter to read it");
        return;
    }
    const char *title = t->title != NULL ? t->title : "";
    if (t->nickname != NULL && t->nickname[0] != '\0') {
        snprintf(line1, len1, "%s", t->nickname);
        snprintf(line2, len2, "%s", title);
    } else {
        snprintf(line1, len1, "%s", title);
        snprintf(line2, len2, "%s", "");
    }
}

const char *home_top_cel(const home_top_t *t)
{
    if (!t->cel_known) {
        return "CEL --";
    }
    return t->cel_on ? "CEL ON" : "CEL off";
}

void home_top_codes(const home_top_t *t, char *out, size_t len)
{
    if (t->codes < 0) {
        snprintf(out, len, "-- codes");
    } else {
        snprintf(out, len, "%d code%s", t->codes, t->codes == 1 ? "" : "s");
    }
}

const char *home_protocol_name(int protocol)
{
    static const char *const names[13] = {
        "",           "J1850 PWM",  "J1850 VPW",  "ISO 9141-2", "KWP slow",
        "KWP fast",   "CAN 11/500", "CAN 29/500", "CAN 11/250", "CAN 29/250",
        "J1939",      "CAN user 1", "CAN user 2",
    };
    return protocol >= 1 && protocol <= 12 ? names[protocol] : "";
}

void home_top_detail(const home_top_t *t, char *out, size_t len)
{
    if (!t->connected) {
        snprintf(out, len, "Adapter %s", t->adapter_host != NULL ? t->adapter_host : "");
        return;
    }
    const char *p = home_protocol_name(t->protocol);
    const char *a = t->adapter_id != NULL ? t->adapter_id : "";
    snprintf(out, len, "%s%s%s", p, p[0] && a[0] ? " - " : "", a);
}

void home_group_digits(long v, char *out, size_t len)
{
    char raw[24];
    snprintf(raw, sizeof raw, "%ld", v < 0 ? -v : v);
    size_t n = strlen(raw), o = 0;
    if (v < 0 && o + 1 < len) {
        out[o++] = '-';
    }
    for (size_t i = 0; i < n && o + 1 < len; i++) {
        if (i > 0 && (n - i) % 3 == 0) {
            if (o + 2 >= len) {
                break;
            }
            out[o++] = ',';
        }
        out[o++] = raw[i];
    }
    if (len > 0) {
        out[o < len ? o : len - 1] = '\0';
    }
}

void home_top_odometer(const home_top_t *t, char *out, size_t len)
{
    if (!t->have_odo || t->odo_km < 0) {
        snprintf(out, len, "%s", "");
        return;
    }
    home_group_digits((long)to_unit(t, t->odo_km), out, len);
}

const char *home_top_dist_unit(const home_top_t *t)
{
    return t->metric ? "km" : "mi";
}

void home_top_last(const home_top_t *t, char *dist, size_t dlen, char *when, size_t wlen)
{
    if (!t->have_last || t->last_km < 0) {
        snprintf(dist, dlen, "%s", "");
        snprintf(when, wlen, "%s", "");
        return;
    }
    double d = to_unit(t, t->last_km);
    if (d > 999.9) {
        d = 999.9;
    }
    snprintf(dist, dlen, "%.1f", d);
    /* "drive-YYYYMMDD-HHMMSS.csv" */
    const char *n = t->last_name != NULL ? t->last_name : "";
    if (strncmp(n, "drive-", 6) != 0 || strlen(n) < 14) {
        snprintf(when, wlen, "%s", "");
    } else if (t->today != NULL && strncmp(n + 6, t->today, 8) == 0) {
        snprintf(when, wlen, "today");
    } else {
        int month = (n[10] - '0') * 10 + (n[11] - '0');
        int day = (n[12] - '0') * 10 + (n[13] - '0');
        snprintf(when, wlen, "%d/%d", month, day);
    }
}

void home_top_economy(const home_top_t *t, char *value, size_t vlen, char *label, size_t llen)
{
    if (!t->have_economy || t->economy <= 0) {
        snprintf(value, vlen, "%s", "");
        snprintf(label, llen, "%s", "");
        return;
    }
    snprintf(value, vlen, "%.1f", t->economy > 99.9 ? 99.9 : t->economy);
    snprintf(label, llen, "%s", t->metric ? "L/100 km average" : "mpg average");
}
