#include "code_history.h"

#include "obd/obd_pid.h"

#include <stdio.h>
#include <string.h>

#include "obd/dtc.h"

/* Appends one CSV field (quoted if it holds a comma, quote or newline). */
static void field(char *row, size_t len, const char *text, bool last)
{
    size_t n = strlen(row);
    bool quote = strpbrk(text, ",\"\n\r") != NULL;
    if (quote && n + 1 < len) {
        row[n++] = '"';
    }
    for (const char *p = text; *p != '\0' && n + 2 < len; p++) {
        if (*p == '"') {
            row[n++] = '"';
        }
        row[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
    }
    if (quote && n + 1 < len) {
        row[n++] = '"';
    }
    if (n + 1 < len) {
        row[n++] = last ? '\n' : ',';
    }
    row[n] = '\0';
}

static void car_text(const obd_diag_t *d, char *out, size_t len)
{
    if (!d->have_vin) {
        out[0] = '\0';
    } else if (d->model != NULL) {
        snprintf(out, len, "%d %s %s", d->model_year, d->model_make, d->model);
    } else if (d->maker != NULL) {
        snprintf(out, len, "%d %s", d->model_year, d->maker);
    } else {
        snprintf(out, len, "%d", d->model_year);
    }
}

/* Appends a row to out if it fits whole; returns false if not. */
static bool add_row(char *out, size_t len, size_t *used, const char *when, const obd_diag_t *d,
                    const char *cel, const char *type, const char *code, const char *desc)
{
    char row[640] = "";
    char car[48];
    car_text(d, car, sizeof car);
    field(row, sizeof row, when, false);
    field(row, sizeof row, d->have_vin ? d->vin : "", false);
    field(row, sizeof row, car, false);
    field(row, sizeof row, cel, false);
    field(row, sizeof row, type, false);
    field(row, sizeof row, code, false);
    field(row, sizeof row, desc, true);
    size_t n = strlen(row);
    if (*used + n + 1 > len) {
        return false;
    }
    memcpy(out + *used, row, n + 1);
    *used += n;
    return true;
}

static bool add_codes(char *out, size_t len, size_t *used, const char *when, const obd_diag_t *d,
                      const char *cel, const char *type, const obd_code_list_t *list, bool have,
                      int *rows)
{
    if (!have) {
        return true;
    }
    for (int i = 0; i < list->count; i++) {
        const char *code = list->code[i].code;
        if (!add_row(out, len, used, when, d, cel, type, code, obd_dtc_text_for(code, d->make))) {
            return false;
        }
        (*rows)++;
    }
    return true;
}

/* The freeze frame (in US units, like the rest of the file's text). */
static bool add_freeze(char *out, size_t len, size_t *used, const char *when,
                       const obd_diag_t *d, const char *cel, int *rows)
{
    if (!d->freeze.have) {
        return true;
    }
    char text[400];
    obd_freeze_text(&d->freeze, OBD_UNITS_US, text, sizeof text);
    (*rows)++;
    return add_row(out, len, used, when, d, cel, "freeze", d->freeze.code,
                   text[0] ? text : "freeze frame with no values read");
}

size_t code_history_scan_rows(const obd_diag_t *d, const char *when, char *out, size_t len)
{
    if (len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!d->have_stored && !d->have_pending && !d->have_permanent) {
        return 0;
    }
    const char *cel = d->have_readiness ? (d->readiness.mil_on ? "ON" : "off") : "";
    size_t used = 0;
    int rows = 0;
    if (!add_codes(out, len, &used, when, d, cel, "stored", &d->stored, d->have_stored, &rows) ||
        !add_freeze(out, len, &used, when, d, cel, &rows) ||
        !add_codes(out, len, &used, when, d, cel, "pending", &d->pending, d->have_pending, &rows) ||
        !add_codes(out, len, &used, when, d, cel, "permanent", &d->permanent, d->have_permanent,
                   &rows)) {
        return used;
    }
    if (rows == 0 && !add_row(out, len, &used, when, d, cel, "none", "", "")) {
        return 0;
    }
    return used;
}

size_t code_history_clear_row(const obd_diag_t *d, const char *when, const char *result,
                              char *out, size_t len)
{
    size_t used = 0;
    if (len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (!add_row(out, len, &used, when, d, "", "cleared", "", result)) {
        return 0;
    }
    return used;
}
