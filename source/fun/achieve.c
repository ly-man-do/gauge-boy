#include "achieve.h"

#include <stdio.h>
#include <string.h>

static const achieve_info_t list[ACH_COUNT] = {
    [ACH_FIRST_SCAN]    = { "first_scan", "First Scan", "Scan a car in DIAG." },
    [ACH_CLEAN_BILL]    = { "clean_bill", "Clean Bill of Health",
                            "Scan with the CEL off, no codes, all monitors ready." },
    [ACH_CODE_BREAKER]  = { "code_breaker", "Code Breaker", "Clear the codes once." },
    [ACH_ON_THE_RECORD] = { "on_the_record", "On the Record", "Record a drive with REC." },
    [ACH_100_MILES]     = { "miles_100", "100 Miles Logged",
                            "Record 100 miles (161 km) of drives." },
    [ACH_FULL_GARAGE]   = { "full_garage", "Full Garage", "Have 3 cars in the garage." },
    [ACH_STOPWATCH]     = { "stopwatch", "Stopwatch", "Finish a Time Attack run." },
    [ACH_PERFECT_SYNC]  = { "perfect_sync", "Perfect Sync", "Get a PERFECT in Rev Sync." },
    [ACH_REV_S]         = { "rev_s", "Rev Sync Star", "Finish a Rev Sync game with rank S." },
    [ACH_HYPERMILER]    = { "hypermiler", "Hypermiler", "Finish an Eco trip with rank S." },
    [ACH_REDLINE]       = { "redline", "Redline", "Make the shift light flash." },
    [ACH_PULLING_GS]    = { "pulling_gs", "Pulling G's", "Reach 0.5 g on the G-meter." },
};

const achieve_info_t *achieve_info(achieve_id_t id)
{
    return (unsigned)id < (unsigned)ACH_COUNT ? &list[id] : NULL;
}

bool achieve_scan_ok(const obd_diag_t *d)
{
    return d->result == OBD_LINK_OK && d->have_readiness;
}

bool achieve_scan_clean(const obd_diag_t *d)
{
    if (!achieve_scan_ok(d) || d->stopped_at != OBD_DIAG_STEP_DONE || d->readiness.mil_on ||
        d->readiness.dtc_count != 0 || !d->have_stored || !d->have_pending ||
        d->stored.count != 0 || d->pending.count != 0) {
        return false;
    }
    return obd_readiness_ready_count(&d->readiness) > 0 &&
           obd_readiness_not_ready_count(&d->readiness) == 0;
}

/* The line for id in text: its date after the comma, or NULL. */
static const char *find(const char *text, achieve_id_t id, size_t *date_len)
{
    const char *key = list[id].key;
    size_t klen = strlen(key);
    for (const char *p = text != NULL ? text : ""; *p != '\0';) {
        const char *eol = strchr(p, '\n');
        size_t n = eol != NULL ? (size_t)(eol - p) : strlen(p);
        if (n > klen && strncmp(p, key, klen) == 0 && p[klen] == ',') {
            size_t d = n - klen - 1;
            if (d > 0 && p[klen + d] == '\r') {
                d--;
            }
            *date_len = d;
            return p + klen + 1;
        }
        p += n + (eol != NULL ? 1 : 0);
    }
    return NULL;
}

bool achieve_unlocked(const char *text, achieve_id_t id, char *date, size_t date_len)
{
    if ((unsigned)id >= (unsigned)ACH_COUNT) {
        return false;
    }
    size_t n = 0;
    const char *d = find(text, id, &n);
    if (d == NULL) {
        return false;
    }
    if (date != NULL && date_len > 0) {
        snprintf(date, date_len, "%.*s", (int)n, d);
    }
    return true;
}

size_t achieve_unlock(const char *text, achieve_id_t id, const char *date, bool *is_new, char *out,
                      size_t len)
{
    *is_new = false;
    if ((unsigned)id >= (unsigned)ACH_COUNT || achieve_unlocked(text, id, NULL, 0) || len == 0) {
        return 0;
    }
    const char *t = text != NULL ? text : "";
    bool header = strncmp(t, "id,date", 7) == 0;
    size_t tl = strlen(t);
    bool nl = tl > 0 && t[tl - 1] != '\n';
    int w = snprintf(out, len, "%s%s%s%s,%s\n", header ? "" : "id,date\n", t, nl ? "\n" : "",
                     list[id].key, date != NULL ? date : "");
    if (w < 0 || (size_t)w >= len) {
        out[0] = '\0';
        return 0;
    }
    *is_new = true;
    return (size_t)w;
}

int achieve_count(const char *text)
{
    int n = 0;
    for (int i = 0; i < ACH_COUNT; i++) {
        n += achieve_unlocked(text, (achieve_id_t)i, NULL, 0) ? 1 : 0;
    }
    return n;
}
