#include "diag_view.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "obd/dtc.h"

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

int diag_view_lines(const obd_diag_t *d, diag_tab_t tab, int cols,
                    char lines[][DIAG_VIEW_COLS + 1], int max)
{
    view_t v = { lines, max, 0, cols > DIAG_VIEW_COLS ? DIAG_VIEW_COLS : cols };
    if (d->link.protocol == 0) {
        addf(&v, "Tap SCAN to read the car (engine running, adapter Wi-Fi joined).");
        return v.count;
    }

    if (d->have_vin) {
        if (d->maker != NULL) {
            addf(&v, "%d %s  VIN %s", d->model_year, d->maker, d->vin);
        } else {
            addf(&v, "Model year %d  VIN %s", d->model_year, d->vin);
        }
    } else {
        addf(&v, "VIN not read");
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
