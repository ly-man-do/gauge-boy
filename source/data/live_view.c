#include "live_view.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char (*lines)[LIVE_VIEW_COLS + 1];
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

/* "Label       value          rate" */
static void row(view_t *v, const char *label, const char *value, const char *rate)
{
    char line[96];
    snprintf(line, sizeof line, "%-11s %-16s %s", label, value, rate);
    add(v, line);
}

int live_view_lines(const obd_live_t *lv, int protocol, float rate, obd_units_t units, int cols,
                    char lines[][LIVE_VIEW_COLS + 1], int max)
{
    view_t v = { lines, max, 0, cols > LIVE_VIEW_COLS ? LIVE_VIEW_COLS : cols };
    char line[96], value[32], hz[24];

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
    bool map = false;
    for (int i = 0; i < lv->count; i++) {
        const obd_live_pid_t *p = &lv->pid[i];
        const obd_pid_info_t *info = obd_pid_info(p->pid);
        map = map || p->pid == OBD_PID_MAP;
        if (p->have) {
            obd_pid_format(p->pid, p->value, units, value, sizeof value);
        } else {
            snprintf(value, sizeof value, "-");
        }
        if (p->dropped) {
            snprintf(hz, sizeof hz, "no data");
        } else if (p->reads >= 2) {
            snprintf(hz, sizeof hz, "%.1f Hz", (double)p->hz);
        } else {
            hz[0] = '\0';
        }
        row(&v, info != NULL ? info->label : "?", value, hz);
    }
    if (lv->have_volts) {
        snprintf(value, sizeof value, "%.1f V", (double)lv->volts);
        row(&v, "Battery", value, "adapter");
    }
    if (map) {
        float vac = 0.0f;
        if (obd_live_vacuum(lv, &vac)) {
            row(&v, "Vacuum", obd_vacuum_format(vac, units, value, sizeof value), "");
        } else {
            row(&v, "Vacuum", "-", "needs engine-off MAP");
        }
    }
    return v.count;
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
