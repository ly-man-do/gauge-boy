#include "diag_view.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "obd/dtc.h"
#include "obd/obd_pid.h"

static int view_units = OBD_UNITS_US;   /* freeze frame values */

typedef struct {
    char (*lines)[DIAG_VIEW_COLS + 1];
    int max;
    int count;
    int cols;
} view_t;

static void add_line(view_t *v, const char *text)
{
    if (v->count >= v->max) {
        return;
    }
    snprintf(v->lines[v->count], (size_t)v->cols + 1, "%s", text);
    v->count++;
}

/* Adds text wrapped at word boundaries; lines after the first are
 * indented by indent spaces. */
static void add_wrapped(view_t *v, const char *text, int indent)
{
    char line[DIAG_VIEW_COLS + 1];
    const char *p = text;
    bool first = true;
    while (*p != '\0' && v->count < v->max) {
        int pad = first ? 0 : indent;
        int room = v->cols - pad;
        int len = (int)strlen(p);
        int take = len;
        if (len > room) {
            take = room;
            while (take > 0 && p[take] != ' ') {
                take--;
            }
            if (take == 0) {
                take = room; /* one long word: hard split */
            }
        }
        snprintf(line, sizeof line, "%*s%.*s", pad, "", take, p);
        add_line(v, line);
        p += take;
        while (*p == ' ') {
            p++;
        }
        first = false;
    }
}

static void addf(view_t *v, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void addf(view_t *v, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    add_wrapped(v, buf, 2);
}

static void codes_tab(view_t *v, const obd_diag_t *d, const obd_code_list_t *list, bool have,
                      const char *what)
{
    if (!have) {
        addf(v, "%s codes: not read", what);
        return;
    }
    if (list->count == 0) {
        addf(v, "No %s codes.", what);
        return;
    }
    addf(v, "%s codes: %d%s", what, list->count, list->truncated ? "+" : "");
    for (int i = 0; i < list->count; i++) {
        char line[256];
        snprintf(line, sizeof line, "%s  %s", list->code[i].code,
                 obd_dtc_text_for(list->code[i].code, d->make));
        add_wrapped(v, line, 7);
    }
}

static void readiness_tab(view_t *v, const obd_diag_t *d)
{
    if (!d->have_readiness) {
        addf(v, "Readiness monitors: not read");
        return;
    }
    const obd_readiness_t *rd = &d->readiness;
    addf(v, "Readiness: %d ready, %d not ready (%s)", obd_readiness_ready_count(rd),
         obd_readiness_not_ready_count(rd), rd->compression ? "diesel" : "spark ignition");
    char unsupported[256] = "";
    for (int i = 0; i < OBD_MONITOR_COUNT; i++) {
        const obd_monitor_t *m = &rd->monitor[i];
        if (m->name == NULL) {
            continue;
        }
        if (m->state == OBD_MONITOR_NOT_SUPPORTED) {
            size_t n = strlen(unsupported);
            snprintf(unsupported + n, sizeof unsupported - n, "%s%s", n ? ", " : "", m->name);
            continue;
        }
        char line[64];
        snprintf(line, sizeof line, "  %-26s %s", m->name, obd_monitor_state_str(m->state));
        add_line(v, line);
    }
    if (unsupported[0] != '\0') {
        char line[300];
        snprintf(line, sizeof line, "Not supported: %s", unsupported);
        add_wrapped(v, line, 2);
    }
}

/* "2008 Honda Ridgeline", "1996 Geo Tracker (set in SETUP)", "Model year
 * 2008", or "" when nothing is known. setup: the suffix for SETUP values. */
static void car_text(const obd_diag_t *d, const char *setup, char *out, size_t len)
{
    const char *make = d->model != NULL && d->model_make != NULL ? d->model_make : d->maker;
    out[0] = '\0';
    if (make == NULL && d->model == NULL) {
        if (d->model_year > 0) {
            snprintf(out, len, "Model year %d", d->model_year);
        }
        return;
    }
    if (d->model_year > 0) {
        snprintf(out, len, "%d", d->model_year);
    }
    if (make != NULL) {
        snprintf(out + strlen(out), len - strlen(out), "%s%s", out[0] ? " " : "", make);
    }
    if (d->model != NULL) {
        snprintf(out + strlen(out), len - strlen(out), "%s%s", out[0] ? " " : "", d->model);
    }
    if (d->make_chosen || d->model_chosen || d->year_chosen) {
        snprintf(out + strlen(out), len - strlen(out), "%s", setup);
    }
}

void diag_car_text(const obd_diag_t *d, char *out, size_t len)
{
    car_text(d, " (set in SETUP)", out, len);
}

int diag_view_lines(const obd_diag_t *d, diag_tab_t tab, int cols,
                    char lines[][DIAG_VIEW_COLS + 1], int max)
{
    view_t v = { lines, max, 0, cols > DIAG_VIEW_COLS ? DIAG_VIEW_COLS : cols };
    if (d->link.protocol == 0) {
        addf(&v, "Tap SCAN to read the car (engine running, adapter Wi-Fi joined).");
        return v.count;
    }

    char car[96];
    car_text(d, " (set in SETUP)", car, sizeof car);
    if (d->have_vin) {
        addf(&v, "%s%sVIN %s", car, car[0] ? "  " : "", d->vin);
    } else {
        addf(&v, "%s%sVIN not read", car, car[0] ? "  " : "");
    }
    addf(&v, "Protocol: %s", elm_protocol_name(d->link.protocol));
    if (d->have_readiness) {
        int n = d->readiness.dtc_count;
        addf(&v, "Check engine light: %s (%d stored code%s)", d->readiness.mil_on ? "ON" : "off",
             n, n == 1 ? "" : "s");
    } else {
        addf(&v, "Check engine light: not read");
    }
    if (d->result != OBD_LINK_OK || d->stopped_at != OBD_DIAG_STEP_DONE) {
        addf(&v, "! Scan stopped: %s (%s)", obd_link_result_str(d->result),
             obd_diag_step_str(d->stopped_at));
    }
    add_line(&v, "");

    switch (tab) {
    case DIAG_TAB_STORED:
        codes_tab(&v, d, &d->stored, d->have_stored, "Stored");
        break;
    case DIAG_TAB_PENDING:
        codes_tab(&v, d, &d->pending, d->have_pending, "Pending");
        break;
    case DIAG_TAB_PERMANENT:
        if (d->permanent_unsupported) {
            addf(&v, "Permanent codes: not supported by this car (normal before 2010).");
        } else {
            codes_tab(&v, d, &d->permanent, d->have_permanent, "Permanent");
        }
        break;
    case DIAG_TAB_READINESS:
    case DIAG_TAB_COUNT:
        readiness_tab(&v, d);
        break;
    }
    return v.count;
}

const char *diag_tab_name(diag_tab_t tab)
{
    switch (tab) {
    case DIAG_TAB_STORED:    return "STORED";
    case DIAG_TAB_PENDING:   return "PENDING";
    case DIAG_TAB_PERMANENT: return "PERMANENT";
    case DIAG_TAB_READINESS: return "READINESS";
    case DIAG_TAB_COUNT:     break;
    }
    return "?";
}

/* ---------- pieces for the paged DIAG screen ---------- */

int diag_wrap(const char *text, int cols, int indent, char lines[][DIAG_VIEW_COLS + 1], int max)
{
    view_t v = { lines, max, 0, cols > DIAG_VIEW_COLS ? DIAG_VIEW_COLS : cols };
    add_wrapped(&v, text, indent);
    return v.count;
}

static void fit_row(char *out, const char *text)
{
    size_t n = strlen(text);
    if (n <= DIAG_ROW_COLS) {
        memcpy(out, text, n + 1);
        return;
    }
    memcpy(out, text, DIAG_ROW_COLS - 3);
    memcpy(out + DIAG_ROW_COLS - 3, "...", 4);
}

static const obd_code_list_t *tab_codes(const obd_diag_t *d, diag_tab_t tab, bool *have,
                                        const char **kind)
{
    switch (tab) {
    case DIAG_TAB_STORED:    *have = d->have_stored;    *kind = "stored";    return &d->stored;
    case DIAG_TAB_PENDING:   *have = d->have_pending;   *kind = "pending";   return &d->pending;
    case DIAG_TAB_PERMANENT: *have = d->have_permanent; *kind = "permanent"; return &d->permanent;
    default: break;
    }
    *have = false;
    *kind = "";
    return NULL;
}

int diag_items(const obd_diag_t *d, diag_tab_t tab, diag_item_t *items, int max)
{
    int n = 0;
    if (tab == DIAG_TAB_READINESS) {
        if (!d->have_readiness) {
            return 0;
        }
        for (int i = 0; i < OBD_MONITOR_COUNT && n < max; i++) {
            const obd_monitor_t *m = &d->readiness.monitor[i];
            if (m->name == NULL || m->state == OBD_MONITOR_NOT_SUPPORTED) {
                continue;
            }
            char row[64];
            snprintf(row, sizeof row, "%-26.26s %s", m->name, obd_monitor_state_str(m->state));
            fit_row(items[n].row, row);
            snprintf(items[n].detail, sizeof items[n].detail, "%s: %s. %s", m->name,
                     obd_monitor_state_str(m->state),
                     m->state == OBD_MONITOR_COMPLETE
                         ? "The car has finished this self-test."
                         : "The car hasn't finished this self-test since the codes were "
                           "cleared or the battery was disconnected; it needs more driving.");
            n++;
        }
        return n;
    }
    bool have;
    const char *kind;
    const obd_code_list_t *list = tab_codes(d, tab, &have, &kind);
    if (list == NULL || !have) {
        return 0;
    }
    for (int i = 0; i < list->count && n < max; i++) {
        const obd_code_t *c = &list->code[i];
        const char *text = obd_dtc_text_for(c->code, d->make);
        char row[256];
        snprintf(row, sizeof row, "%s %s", c->code, text);
        fit_row(items[n].row, row);
        if (c->has_ecu) {
            snprintf(items[n].detail, sizeof items[n].detail, "%s (%s, ECU %X): %s", c->code, kind,
                     (unsigned)c->ecu, text);
        } else {
            snprintf(items[n].detail, sizeof items[n].detail, "%s (%s): %s", c->code, kind, text);
        }
        if (tab == DIAG_TAB_STORED && d->freeze.have && strcmp(c->code, d->freeze.code) == 0) {
            char ff[320];
            obd_freeze_text(&d->freeze, view_units, ff, sizeof ff);
            size_t used = strlen(items[n].detail);
            snprintf(items[n].detail + used, sizeof items[n].detail - used,
                     " Freeze frame when it set: %s.", ff[0] ? ff : "no values read");
        }
        n++;
    }
    return n;
}

void diag_view_set_units(int units)
{
    view_units = units;
}

bool diag_has_trim_codes(const obd_diag_t *d)
{
    static const char *const codes[] = { "P0171", "P0172", "P0174", "P0175" };
    const obd_code_list_t *lists[2] = { &d->stored, &d->pending };
    for (int l = 0; l < 2; l++) {
        for (int i = 0; i < lists[l]->count; i++) {
            for (size_t c = 0; c < sizeof codes / sizeof codes[0]; c++) {
                if (strcmp(lists[l]->code[i].code, codes[c]) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

const char *diag_empty_text(const obd_diag_t *d, diag_tab_t tab)
{
    if (d->link.protocol == 0) {
        return "Not scanned yet: tap SCAN.";
    }
    switch (tab) {
    case DIAG_TAB_STORED:
        return d->have_stored ? "No stored codes." : "Stored codes not read.";
    case DIAG_TAB_PENDING:
        return d->have_pending ? "No pending codes." : "Pending codes not read.";
    case DIAG_TAB_PERMANENT:
        if (d->permanent_unsupported) {
            return "Not supported by this car (normal before 2010).";
        }
        return d->have_permanent ? "No permanent codes." : "Permanent codes not read.";
    case DIAG_TAB_READINESS:
    case DIAG_TAB_COUNT:
        break;
    }
    return d->have_readiness ? "No readiness monitors supported." : "Readiness not read.";
}

static void fact(view_t *v, const char *label, const char *value)
{
    char line[160];   /* add_line cuts it to the panel width */
    snprintf(line, sizeof line, "%-9s %s", label, value);
    add_line(v, line);
}

int diag_facts(const obd_diag_t *d, int cols, char lines[][DIAG_VIEW_COLS + 1], int max)
{
    view_t v = { lines, max, 0, cols > DIAG_VIEW_COLS ? DIAG_VIEW_COLS : cols };
    char buf[96];
    if (d->link.protocol == 0) {
        fact(&v, "Car", "Not scanned yet");
        if (d->result != OBD_LINK_OK) {
            fact(&v, "Last scan", obd_link_result_str(d->result));
        }
        return v.count;
    }
    car_text(d, d->have_vin ? " (SETUP)" : " (set in SETUP)", buf, sizeof buf);
    if (d->have_vin) {
        fact(&v, "Car", buf);
        fact(&v, "VIN", d->vin);
    } else if (buf[0] != '\0') {
        fact(&v, "Car", buf);
        fact(&v, "VIN", "not read");
    } else {
        fact(&v, "Car", "VIN not read");
    }
    fact(&v, "Protocol", elm_protocol_name(d->link.protocol));
    if (d->have_readiness) {
        snprintf(buf, sizeof buf, "%s (%d stored code%s)", d->readiness.mil_on ? "ON" : "off",
                 d->readiness.dtc_count, d->readiness.dtc_count == 1 ? "" : "s");
        fact(&v, "CEL", buf);
        snprintf(buf, sizeof buf, "%d ready, %d not ready",
                 obd_readiness_ready_count(&d->readiness),
                 obd_readiness_not_ready_count(&d->readiness));
        fact(&v, "Readiness", buf);
    } else if (d->vin_only) {
        fact(&v, "Codes", "not read yet: DIAG > SCAN");
        return v.count;
    } else {
        fact(&v, "CEL", "not read");
    }
    snprintf(buf, sizeof buf, "%d stored, %d pending%s", d->stored.count, d->pending.count,
             d->permanent_unsupported ? "" : "");
    if (d->have_permanent) {
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof buf - n, ", %d permanent", d->permanent.count);
    }
    fact(&v, "Codes", buf);
    if (d->freeze.have) {
        snprintf(buf, sizeof buf, "%s (tap it on STORED)", d->freeze.code);
        fact(&v, "Freeze", buf);
    }
    if (d->result != OBD_LINK_OK || d->stopped_at != OBD_DIAG_STEP_DONE) {
        snprintf(buf, sizeof buf, "stopped: %s", obd_diag_step_str(d->stopped_at));
        fact(&v, "Scan", buf);
    }
    return v.count;
}
