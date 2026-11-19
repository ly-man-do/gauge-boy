#include "live_view.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char (*lines)[LIVE_VIEW_COLS + 1];
    live_row_t *rows;        /* rows mode (live_view_rows) instead of lines */
    int max;
    int count;
    int cols;
} view_t;

static void add(view_t *v, const char *text)
{
    if (v->count < v->max) {
        char *line = v->lines[v->count++];
        snprintf(line, (size_t)v->cols + 1, "%s", text);
        size_t n = strlen(line);
        while (n > 0 && line[n - 1] == ' ') {
            line[--n] = '\0';
        }
    }
}

/* "Label       value          rate", or in rows mode the label and the value
 * split from its unit ("742 rpm" -> "742", "rpm"). */
static void row(view_t *v, const char *label, const char *value, const char *rate)
{
    if (v->rows != NULL) {
        if (v->count < v->max) {
            live_row_t *r = &v->rows[v->count++];
            snprintf(r->label, sizeof r->label, "%s", label);
            const char *sp = strchr(value, ' ');
            size_t n = sp != NULL ? (size_t)(sp - value) : strlen(value);
            snprintf(r->value, sizeof r->value, "%.*s", (int)(n < sizeof r->value ? n : sizeof r->value - 1), value);
            snprintf(r->unit, sizeof r->unit, "%s", sp != NULL ? sp + 1 : "");
        }
        return;
    }
    char line[96];
    snprintf(line, sizeof line, "%-11s %-16s %s", label, value, rate);
    add(v, line);
}

static void values(view_t *v, const obd_live_t *lv, obd_units_t units, float engine_l);

int live_view_lines(const obd_live_t *lv, int protocol, float rate, obd_units_t units,
                    float engine_l, int cols, char lines[][LIVE_VIEW_COLS + 1], int max)
{
    view_t v = { lines, NULL, max, 0, cols > LIVE_VIEW_COLS ? LIVE_VIEW_COLS : cols };
    char line[96];

    if (protocol != 0) {
        snprintf(line, sizeof line, "%s, %.1f requests/s", elm_protocol_name(protocol),
                 (double)rate);
    } else {
        snprintf(line, sizeof line, "Not polling");
    }
    add(&v, line);
    add(&v, "");
    if (lv == NULL) {
        add(&v, "No readings yet.");
        return v.count;
    }
    if (lv->count == 0) {
        add(&v, "No PIDs chosen: tap some below.");
    }
    values(&v, lv, units, engine_l);
    return v.count;
}

int live_view_rows(const obd_live_t *lv, obd_units_t units, float engine_l, live_row_t *rows,
                   int max)
{
    view_t v = { NULL, rows, max, 0, LIVE_VIEW_COLS };
    if (lv != NULL) {
        values(&v, lv, units, engine_l);
    }
    return v.count;
}

/* One row per polled PID, the battery, vacuum or boost, and the fuel estimate and economy. */
static void values(view_t *v, const obd_live_t *lv, obd_units_t units, float engine_l)
{
    char value[32], hz[24];
    bool map = false;
    for (int i = 0; i < lv->count; i++) {
        const obd_live_pid_t *p = &lv->pid[i];
        const obd_pid_info_t *info = obd_pid_info(p->pid);
        map = map || p->pid == OBD_PID_MAP || p->pid == OBD_PID_MAP_WIDE;
        if (p->have) {
            obd_pid_format(p->pid, p->value, units, value, sizeof value);
        } else {
            snprintf(value, sizeof value, "-");
        }
        if (p->dropped) {
            snprintf(hz, sizeof hz, "no data");
        } else if (p->reads >= 2 && p->hz >= 0.95f) {
            snprintf(hz, sizeof hz, "%.1f Hz", (double)p->hz);
        } else if (p->reads >= 2 && p->hz > 0.0f) {
            snprintf(hz, sizeof hz, "every %.0f s", (double)(1.0f / p->hz));
        } else {
            hz[0] = '\0';
        }
        row(v, info != NULL ? info->label : "?", value, hz);
    }
    if (lv->have_volts) {
        snprintf(value, sizeof value, "%.1f V", (double)lv->volts);
        row(v, "Battery", value, "adapter");
    }
    if (map) {
        float vac = 0.0f;
        if (obd_live_vacuum(lv, &vac) && vac < -1.0f) {
            row(v, "Boost", obd_boost_format(-vac, units, value, sizeof value), "");
        } else if (obd_live_vacuum(lv, &vac)) {
            bool saved = lv->engine_off_map_saved && obd_live_get(lv, 0x33) == NULL;
            row(v, "Vacuum", obd_vacuum_format(vac, units, value, sizeof value),
                saved ? "saved baro" : "");
        } else {
            row(v, "Vacuum", "-", "needs engine-off MAP");
        }
    }
    const obd_live_pid_t *speed = obd_live_get(lv, OBD_PID_SPEED);
    if (speed != NULL && !speed->dropped) {
        obd_fuel_src_t src = obd_live_fuel_source(lv, engine_l);
        float lph = 0.0f;
        bool have = obd_live_fuel_lph(lv, engine_l, &lph);
        if (src == OBD_FUEL_MAF || src == OBD_FUEL_MAP) {   /* the car's own has its row */
            row(v, "Fuel est.", have ? obd_fuel_rate_format(lph, units, value, sizeof value) : "-",
                src == OBD_FUEL_MAF ? "from MAF" : "from MAP");
        } else if (src == OBD_FUEL_NONE) {
            bool map = obd_live_get(lv, OBD_PID_MAP) != NULL ||
                       obd_live_get(lv, OBD_PID_MAP_WIDE) != NULL;
            row(v, "Fuel est.", "-", map && engine_l <= 0.0f ? "SETUP: engine size"
                                                               : "needs MAF or MAP");
        }
        if (src != OBD_FUEL_NONE) {
            if (have && speed->have) {
                obd_economy_format(lph, speed->value, units, value, sizeof value);
            } else {
                snprintf(value, sizeof value, "-");
            }
            row(v, "Economy", value, speed->have && speed->value < 5.0f ? "stopped" : "instant");
        }
    }
}

int live_choices(const obd_link_t *link, uint8_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < obd_pid_count() && n < max; i++) {
        uint8_t pid = obd_pid_at(i)->pid;
        if (obd_link_pid_supported(link, pid)) {
            out[n++] = pid;
        }
    }
    return n;
}

bool live_pid_chosen(const uint8_t *pids, int count, uint8_t pid)
{
    for (int i = 0; i < count; i++) {
        if (pids[i] == pid) {
            return true;
        }
    }
    return false;
}

int live_toggle(uint8_t *pids, int count, int max, uint8_t pid)
{
    for (int i = 0; i < count; i++) {
        if (pids[i] == pid) {
            memmove(&pids[i], &pids[i + 1], (size_t)(count - i - 1));
            return count - 1;
        }
    }
    if (count >= max) {
        return count;
    }
    pids[count] = pid;
    return count + 1;
}
