#include "home_top.h"

#include <stdio.h>
#include <string.h>

#define KM_PER_MI 1.609344

static double to_unit(const home_top_t *t, double km)
{
    return t->metric ? km : km / KM_PER_MI;
}

static const char *unit(const home_top_t *t)
{
    return t->metric ? "km" : "mi";
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

static void set(home_row_t *r, const char *tag, home_tone_t tone, const char *value,
                const char *detail)
{
    r->tag = tag;
    r->tone = tone;
    r->lcd = r->lamp = false;
    snprintf(r->value, sizeof r->value, "%s", value != NULL ? value : "");
    snprintf(r->detail, sizeof r->detail, "%s", detail != NULL ? detail : "");
}

/* "today" or "11/16" from "drive-YYYYMMDD-HHMMSS.csv"; "" if the name isn't one. */
static void drive_day(const home_top_t *t, char *out, size_t len)
{
    const char *n = t->last_name != NULL ? t->last_name : "";
    if (strncmp(n, "drive-", 6) != 0 || strlen(n) < 14) {
        snprintf(out, len, "%s", "");
    } else if (t->today != NULL && strncmp(n + 6, t->today, 8) == 0) {
        snprintf(out, len, "today");
    } else {
        int month = (n[10] - '0') * 10 + (n[11] - '0');
        int day = (n[12] - '0') * 10 + (n[13] - '0');
        snprintf(out, len, "%d/%d", month, day);
    }
}

void home_top_rows(const home_top_t *t, home_row_t rows[HOME_ROWS])
{
    char a[HOME_ROW_TEXT], b[HOME_ROW_TEXT];

    /* Car: nickname and model, the model alone, or not known yet */
    if (!t->have_car) {
        set(&rows[0], "Car", HOME_TONE_DIM, "No car yet", "");
    } else if (t->nickname != NULL && t->nickname[0] != '\0') {
        set(&rows[0], "Car", HOME_TONE_NORMAL, t->nickname, t->title);
    } else {
        set(&rows[0], "Car", HOME_TONE_NORMAL, t->title, "");
    }

    /* CEL: a lamp, Off or ON (amber), and how far it's been on */
    if (!t->cel_known) {
        set(&rows[1], "CEL", HOME_TONE_DIM, "--", "");
    } else if (t->cel_on) {
        b[0] = '\0';
        if (t->have_mil_km) {
            home_group_digits((long)(to_unit(t, t->mil_km) + 0.5), a, sizeof a);
            snprintf(b, sizeof b, "for %.20s %s", a, unit(t));
        }
        set(&rows[1], "CEL", HOME_TONE_AMBER, "ON", b);
        rows[1].lamp = true;
    } else {
        set(&rows[1], "CEL", HOME_TONE_NORMAL, "Off", "");
        rows[1].lamp = true;
    }

    /* Codes */
    if (t->codes < 0) {
        set(&rows[2], "Codes", HOME_TONE_DIM, "--", "");
    } else if (t->codes == 0) {
        set(&rows[2], "Codes", HOME_TONE_NORMAL, "None stored", "");
    } else {
        snprintf(a, sizeof a, "%d stored", t->codes);
        set(&rows[2], "Codes", HOME_TONE_NORMAL, a, t->code_list);
    }

    /* Odometer, in LCD digits */
    if (t->have_odo && t->odo_km >= 0) {
        home_group_digits((long)to_unit(t, t->odo_km), a, sizeof a);
        set(&rows[3], "Odometer", HOME_TONE_NORMAL, a, unit(t));
    } else {
        set(&rows[3], "Odometer", HOME_TONE_DIM, "---,---", unit(t));
    }
    rows[3].lcd = true;

    /* Last drive */
    if (t->have_last && t->last_km >= 0) {
        double d = to_unit(t, t->last_km);
        snprintf(a, sizeof a, "%.1f %s", d > 999.9 ? 999.9 : d, unit(t));
        drive_day(t, b, sizeof b);
        set(&rows[4], "Last drive", HOME_TONE_NORMAL, a, b);
    } else {
        set(&rows[4], "Last drive", HOME_TONE_DIM, "None yet", "");
    }

    /* Economy: the mileage log's average */
    if (t->have_economy && t->economy > 0) {
        snprintf(a, sizeof a, t->metric ? "%.1f L/100 km" : "%.1f mpg",
                 t->economy > 99.9 ? 99.9 : t->economy);
        set(&rows[5], "Economy", HOME_TONE_ECONOMY, a, "average");
    } else {
        set(&rows[5], "Economy", HOME_TONE_DIM, "--", "");
    }

    /* Adapter */
    if (t->connected) {
        const char *id = t->adapter_id != NULL && t->adapter_id[0] ? t->adapter_id : "Connected";
        set(&rows[6], "Adapter", HOME_TONE_NORMAL, id, home_protocol_name(t->protocol));
    } else {
        set(&rows[6], "Adapter", HOME_TONE_RED, "Not connected", t->adapter_host);
    }
}
