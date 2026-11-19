#include "diag_ui.h"

#include <stdio.h>
#include <string.h>

#include "obd/dtc.h"

/* DIAG's buttons: System Settings' large button, 236 x 36 on a 40 px pitch,
 * centred in the 198 px above the tabs. */
#define HOME_X     42
#define HOME_W     236
#define HOME_H     36
#define HOME_PITCH 40
#define HOME_TOP   ((DIAG_LIST_H - (DIAG_HOME_BUTTONS - 1) * HOME_PITCH - HOME_H) / 2)
/* A page's rows, like the readiness list in the mockup. */
#define ROW_X      20
#define ROW_W      262
#define ROW_H      32
#define ROW_PITCH  36
#define ROW_TOP    6
/* The scroll bar (as LIVE's). */
#define TRACK_X    302
#define TRACK_Y    6
#define TRACK_H    190
/* System Settings' tabs: 160 x 38 sprites; their touch zones are the 128 px
 * each label sits in, from the bottom edge up to 202. */
#define TAB_W      128
#define TAB_Y      202

static bool in(diag_rect_t r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

/* ---------- DIAG's first page ---------- */

diag_rect_t diag_home_button(int i)
{
    diag_rect_t r = { HOME_X, HOME_TOP + i * HOME_PITCH, HOME_W, HOME_H };
    return r;
}

const char *diag_home_label(int i)
{
    static const char *const labels[DIAG_HOME_BUTTONS] = { "Stored", "Pending", "Permanent",
                                                           "Readiness" };
    return i >= 0 && i < DIAG_HOME_BUTTONS ? labels[i] : "";
}

diag_rect_t diag_tab(bool right)
{
    diag_rect_t r = { right ? DIAG_SCREEN_W - TAB_W : 0, TAB_Y, TAB_W, DIAG_SCREEN_H - TAB_Y };
    return r;
}

diag_hit_t diag_home_hit(int px, int py)
{
    if (in(diag_tab(false), px, py)) {
        return DIAG_HIT_BACK;
    }
    if (in(diag_tab(true), px, py)) {
        return DIAG_HIT_RIGHT;
    }
    for (int i = 0; i < DIAG_HOME_BUTTONS; i++) {
        if (in(diag_home_button(i), px, py)) {
            return (diag_hit_t)(DIAG_HIT_STORED + i);
        }
    }
    return DIAG_HIT_NONE;
}

diag_hit_t diag_page_tab_hit(int px, int py, bool right_tab)
{
    if (in(diag_tab(false), px, py)) {
        return DIAG_HIT_BACK;
    }
    if (right_tab && in(diag_tab(true), px, py)) {
        return DIAG_HIT_RIGHT;
    }
    return DIAG_HIT_NONE;
}

/* ---------- a page's list ---------- */

diag_rect_t diag_list_row(int i, int scroll)
{
    diag_rect_t r = { ROW_X, ROW_TOP + i * ROW_PITCH - scroll, ROW_W, ROW_H };
    return r;
}

int diag_list_max_scroll(int n)
{
    int content = ROW_TOP + n * ROW_PITCH;
    return content > DIAG_LIST_H ? content - DIAG_LIST_H : 0;
}

int diag_list_clamp(int scroll, int n)
{
    int max = diag_list_max_scroll(n);
    return scroll < 0 ? 0 : scroll > max ? max : scroll;
}

int diag_list_hit(int px, int py, int scroll, int n)
{
    if (py < 0 || py >= DIAG_LIST_H) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (in(diag_list_row(i, scroll), px, py)) {
            return i;
        }
    }
    return -1;
}

diag_rect_t diag_list_track(void)
{
    diag_rect_t r = { TRACK_X, TRACK_Y, 8, TRACK_H };
    return r;
}

int diag_list_grip_y(int scroll, int n)
{
    int max = diag_list_max_scroll(n);
    int travel = TRACK_H - DIAG_GRIP_H;
    return TRACK_Y + (max > 0 ? (diag_list_clamp(scroll, n) * travel + max / 2) / max : 0);
}

bool diag_list_bar_hit(int px, int py)
{
    return px >= ROW_X + ROW_W + 8 && px < DIAG_SCREEN_W && py >= 0 && py < DIAG_LIST_H;
}

int diag_list_grip_grab(int py, int scroll, int n)
{
    int top = diag_list_grip_y(scroll, n);
    return py >= top && py < top + DIAG_GRIP_H ? py - top : DIAG_GRIP_H / 2;
}

int diag_list_scroll_for_grip(int grip_top, int n)
{
    int max = diag_list_max_scroll(n), travel = TRACK_H - DIAG_GRIP_H;
    if (max <= 0 || travel <= 0) {
        return 0;
    }
    return diag_list_clamp(((grip_top - TRACK_Y) * max + travel / 2) / travel, n);
}

/* ---------- the clear-codes question ---------- */

diag_rect_t diag_dialog_button(bool clear)
{
    diag_rect_t r = { clear ? 164 : 28, 150, 128, 44 };
    return r;
}

int diag_dialog_hit(int px, int py)
{
    if (in(diag_dialog_button(false), px, py)) {
        return 1;
    }
    if (in(diag_dialog_button(true), px, py)) {
        return 2;
    }
    return 0;
}

/* ---------- top-screen text ---------- */

static diag_fact_t *fact(diag_fact_t *out, int *n, int max, const char *tag, diag_ink_t ink,
                         const char *value, const char *detail)
{
    if (*n >= max) {
        return NULL;
    }
    diag_fact_t *f = &out[(*n)++];
    memset(f, 0, sizeof *f);
    snprintf(f->tag, sizeof f->tag, "%s", tag);
    snprintf(f->value, sizeof f->value, "%s", value);
    snprintf(f->detail, sizeof f->detail, "%s", detail);
    f->ink = ink;
    return f;
}

static void distance_text(int km, obd_units_t units, char *out, size_t len)
{
    char num[24];
    float v = obd_unit_convert(OBD_UNIT_KM, (float)km, units);
    obd_format_grouped((long long)(v + 0.5f), num, sizeof num);
    snprintf(out, len, "%s %s", num, obd_unit_label(OBD_UNIT_KM, units));
}

static void duration_text(int minutes, char *out, size_t len)
{
    if (minutes < 60) {
        snprintf(out, len, "%d min", minutes);
    } else if (minutes < 48 * 60) {
        snprintf(out, len, "%d h %02d min", minutes / 60, minutes % 60);
    } else {
        snprintf(out, len, "%d days", minutes / (24 * 60));
    }
}

/* "2 codes" / "1 code" / "None" / "Not read", the first codes in detail. */
static void codes_fact(diag_fact_t *out, int *n, int max, const char *tag, bool have,
                       const obd_code_list_t *list, diag_ink_t ink)
{
    char value[24], detail[64] = "";
    if (!have) {
        fact(out, n, max, tag, DIAG_INK_GREY, "Not read", "");
        return;
    }
    if (list->count == 0) {
        fact(out, n, max, tag, DIAG_INK_WHITE, "None", "");
        return;
    }
    snprintf(value, sizeof value, "%d%s code%s", list->count, list->truncated ? "+" : "",
             list->count == 1 && !list->truncated ? "" : "s");
    for (int i = 0; i < list->count && i < 4; i++) {
        size_t used = strlen(detail);
        snprintf(detail + used, sizeof detail - used, "%s%s", i ? ", " : "", list->code[i].code);
    }
    if (list->count > 4) {
        size_t used = strlen(detail);
        snprintf(detail + used, sizeof detail - used, " ...");
    }
    fact(out, n, max, tag, ink, value, detail);
}

const char *diag_monitor_short(const char *name)
{
    static const char *const map[][2] = {
        { "Evaporative system", "Evap" },
        { "Oxygen sensor", "O2 sensor" },
        { "Oxygen sensor heater", "O2 heater" },
        { "Secondary air system", "Secondary air" },
        { "EGR / VVT system", "EGR / VVT" },
        { "NOx / SCR aftertreatment", "NOx / SCR" },
        { "Heated catalyst", "Heated cat." },
    };
    if (name == NULL) {
        return "";
    }
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (strcmp(name, map[i][0]) == 0) {
            return map[i][1];
        }
    }
    return name;
}

/* The monitors not ready, "Catalyst, Evap" (all of them). */
static void not_ready_names(const obd_readiness_t *rd, char *out, size_t len)
{
    out[0] = '\0';
    for (int i = 0; i < OBD_MONITOR_COUNT; i++) {
        const obd_monitor_t *m = &rd->monitor[i];
        if (m->name != NULL && m->state == OBD_MONITOR_INCOMPLETE) {
            size_t used = strlen(out);
            snprintf(out + used, len - used, "%s%s", used ? ", " : "", diag_monitor_short(m->name));
        }
    }
}

bool diag_inspection(const obd_diag_t *d, char *detail, size_t len)
{
    detail[0] = '\0';
    if (!d->have_readiness) {
        snprintf(detail, len, "readiness not read");
        return false;
    }
    if (d->readiness.mil_on) {
        snprintf(detail, len, "the CEL is on");
        return false;
    }
    bool older = d->model_year >= 1996 && d->model_year <= 2000;
    int allowed = older ? 2 : 1;
    int not_ready = obd_readiness_not_ready_count(&d->readiness);
    if (not_ready > allowed) {
        snprintf(detail, len, "%d not ready, %d allowed", not_ready, allowed);
        return false;
    }
    snprintf(detail, len, not_ready == 0 ? "all monitors ready"
                          : older    ? "2 not ready are allowed (1996-2000)"
                                     : "1 not ready is allowed");
    return true;
}

int diag_home_facts(const obd_diag_t *d, const char *nickname, obd_units_t units,
                    diag_fact_t *out, int max, char *note, size_t note_len)
{
    int n = 0;
    char car[96], buf[64];
    note[0] = '\0';
    diag_car_text(d, car, sizeof car);
    if (nickname != NULL && nickname[0] != '\0') {
        fact(out, &n, max, "Car", DIAG_INK_WHITE, nickname, car);
    } else if (car[0] != '\0') {
        fact(out, &n, max, "Car", DIAG_INK_WHITE, car, "");
    }
    if (d->link.protocol == 0) {
        if (n == 0) {
            fact(out, &n, max, "Car", DIAG_INK_GREY, "Not scanned yet", "");
        }
        if (d->result != OBD_LINK_OK) {
            fact(out, &n, max, "Last scan", DIAG_INK_AMBER, obd_link_result_str(d->result), "");
        }
        snprintf(note, note_len, "Tap Scan with the engine running.");
        return n;
    }
    if (n == 0) {
        fact(out, &n, max, "Car", DIAG_INK_GREY, "Not known", "VIN not read");
    }
    if (d->vin_only || !d->have_readiness) {
        fact(out, &n, max, "CEL", DIAG_INK_GREY, "Not read", "");
        if (d->vin_only) {
            snprintf(note, note_len, "Codes not read yet: tap Scan.");
            return n;
        }
    } else {
        diag_fact_t *f;
        if (d->readiness.mil_on) {
            buf[0] = '\0';
            char part[32];
            if (d->have_mil_km && d->mil_km > 0) {
                distance_text(d->mil_km, units, part, sizeof part);
                snprintf(buf, sizeof buf, "for %s", part);
            } else if (d->have_mil_min && d->mil_min > 0) {
                duration_text(d->mil_min, part, sizeof part);
                snprintf(buf, sizeof buf, "for %s", part);
            }
            f = fact(out, &n, max, "CEL", DIAG_INK_AMBER, "On", buf);
        } else {
            f = fact(out, &n, max, "CEL", DIAG_INK_WHITE, "Off", "");
        }
        if (f != NULL) {
            f->cel_icon = d->readiness.mil_on;
        }
    }
    codes_fact(out, &n, max, "Stored", d->have_stored, &d->stored, DIAG_INK_RED);
    codes_fact(out, &n, max, "Pending", d->have_pending, &d->pending, DIAG_INK_AMBER);
    if (d->permanent_unsupported) {
        fact(out, &n, max, "Permanent", DIAG_INK_GREY, "Not supported", "normal before 2010");
    } else {
        codes_fact(out, &n, max, "Permanent", d->have_permanent, &d->permanent, DIAG_INK_AMBER);
    }
    if (d->have_readiness) {
        int ready = obd_readiness_ready_count(&d->readiness);
        int total = ready + obd_readiness_not_ready_count(&d->readiness);
        char names[64];
        not_ready_names(&d->readiness, names, sizeof names);
        snprintf(buf, sizeof buf, "%d of %d", ready, total);
        char detail[80];
        snprintf(detail, sizeof detail, "%s%s", names[0] ? names : "all ready",
                 names[0] ? " not ready" : "");
        fact(out, &n, max, "Readiness", DIAG_INK_WHITE, buf, detail);
    } else {
        fact(out, &n, max, "Readiness", DIAG_INK_GREY, "Not read", "");
    }
    fact(out, &n, max, "Protocol", DIAG_INK_WHITE, elm_protocol_name(d->link.protocol), "");
    if (d->result != OBD_LINK_OK || d->stopped_at != OBD_DIAG_STEP_DONE) {
        snprintf(note, note_len, "The scan stopped early (%s): tap Scan again.",
                 obd_diag_step_str(d->stopped_at));
    }
    return n;
}

const char *diag_page_title(diag_tab_t tab)
{
    switch (tab) {
    case DIAG_TAB_STORED:    return "STORED CODES";
    case DIAG_TAB_PENDING:   return "PENDING CODES";
    case DIAG_TAB_PERMANENT: return "PERMANENT CODES";
    default:                 return "READINESS";
    }
}

static const obd_code_list_t *page_codes(const obd_diag_t *d, diag_tab_t tab, bool *have)
{
    switch (tab) {
    case DIAG_TAB_STORED:    *have = d->have_stored;    return &d->stored;
    case DIAG_TAB_PENDING:   *have = d->have_pending;   return &d->pending;
    case DIAG_TAB_PERMANENT: *have = d->have_permanent; return &d->permanent;
    default:                 *have = false;             return NULL;
    }
}

int diag_page_facts(const obd_diag_t *d, diag_tab_t tab, diag_fact_t *out, int max, char *note,
                    size_t note_len)
{
    int n = 0;
    char buf[64], detail[64];
    note[0] = '\0';
    if (tab == DIAG_TAB_READINESS) {
        if (!d->have_readiness) {
            fact(out, &n, max, "Readiness", DIAG_INK_GREY, "Not read", "");
            snprintf(note, note_len, "%s", d->link.protocol == 0 ? "Tap Scan on DIAG first." : "");
            return n;
        }
        int ready = obd_readiness_ready_count(&d->readiness);
        int not_ready = obd_readiness_not_ready_count(&d->readiness);
        snprintf(buf, sizeof buf, "%d of %d", ready, ready + not_ready);
        fact(out, &n, max, "Ready", DIAG_INK_WHITE, buf, "monitors complete");
        char names[64];
        not_ready_names(&d->readiness, names, sizeof names);
        if (names[0] != '\0') {
            fact(out, &n, max, "Not ready", DIAG_INK_AMBER, names, "drive more to finish them");
        } else {
            fact(out, &n, max, "Not ready", DIAG_INK_GREEN, "None", "");
        }
        bool pass = diag_inspection(d, detail, sizeof detail);
        fact(out, &n, max, "Inspection", pass ? DIAG_INK_WHITE : DIAG_INK_AMBER,
             pass ? "Likely to pass" : "Likely to fail", detail);
        snprintf(note, note_len, "Clearing codes resets every monitor to Not ready.");
        return n;
    }

    static const char *const what[] = {
        "Stored codes turn on the check engine light.",
        ("A pending code is a fault seen once. If it's seen again, it becomes a stored code "
         "and turns on the CEL."),
        "Permanent codes stay until the car's own tests pass again: clearing can't remove them.",
    };
    static const char *const kind[] = { "set the CEL", "seen once", "the car clears them" };
    bool have;
    const obd_code_list_t *list = page_codes(d, tab, &have);
    const char *tag = tab == DIAG_TAB_STORED ? "Stored" : tab == DIAG_TAB_PENDING ? "Pending"
                                                                                   : "Permanent";
    if (tab == DIAG_TAB_PERMANENT && d->permanent_unsupported) {
        fact(out, &n, max, tag, DIAG_INK_GREY, "Not supported", "normal before 2010");
        snprintf(note, note_len, "%s", what[tab]);
        return n;
    }
    if (!have) {
        fact(out, &n, max, tag, DIAG_INK_GREY, "Not read", "");
        snprintf(note, note_len, "%s", d->link.protocol == 0 ? "Tap Scan on DIAG first." : what[tab]);
        return n;
    }
    if (list->count == 0) {
        fact(out, &n, max, tag, DIAG_INK_WHITE, "None", "");
        snprintf(note, note_len, "%s", what[tab]);
        return n;
    }
    snprintf(buf, sizeof buf, "%d%s code%s", list->count, list->truncated ? "+" : "",
             list->count == 1 && !list->truncated ? "" : "s");
    fact(out, &n, max, tag, tab == DIAG_TAB_STORED ? DIAG_INK_RED : DIAG_INK_AMBER, buf, kind[tab]);
    /* The modules they came from (known when the scan read them with headers on). */
    char mods[64] = "";
    for (int i = 0; i < list->count; i++) {
        if (!list->code[i].has_ecu) {
            continue;
        }
        char mod[24];
        obd_diag_module_name(d->link.protocol, list->code[i].ecu, mod, sizeof mod);
        if (strstr(mods, mod) == NULL) {
            size_t used = strlen(mods);
            snprintf(mods + used, sizeof mods - used, "%s%s", used ? ", " : "", mod);
        }
    }
    if (mods[0] != '\0') {
        fact(out, &n, max, "From", DIAG_INK_WHITE, mods, "");
    }
    if (tab == DIAG_TAB_STORED && d->freeze.have) {
        fact(out, &n, max, "Freeze frame", DIAG_INK_WHITE, d->freeze.code, "tap it for the values");
    }
    snprintf(note, note_len, "Tap a code for its description%s.",
             tab == DIAG_TAB_STORED && d->freeze.have ? " and freeze frame" : "");
    return n;
}

int diag_page_rows(const obd_diag_t *d, diag_tab_t tab, diag_row_t *out, int max)
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
            diag_row_t *r = &out[n++];
            memset(r, 0, sizeof *r);
            snprintf(r->name, sizeof r->name, "%s", diag_monitor_short(m->name));
            bool ok = m->state == OBD_MONITOR_COMPLETE;
            snprintf(r->right, sizeof r->right, "%s", ok ? "Complete" : "Not ready");
            r->ink = ok ? DIAG_INK_GREEN : DIAG_INK_AMBER;
        }
        return n;
    }
    bool have;
    const obd_code_list_t *list = page_codes(d, tab, &have);
    if (list == NULL || !have || (tab == DIAG_TAB_PERMANENT && d->permanent_unsupported)) {
        return 0;
    }
    for (int i = 0; i < list->count && n < max; i++) {
        diag_row_t *r = &out[n++];
        memset(r, 0, sizeof *r);
        snprintf(r->name, sizeof r->name, "%s", list->code[i].code);
        snprintf(r->right, sizeof r->right, "%s", obd_dtc_text_for(list->code[i].code, d->make));
        r->code = true;
        r->ink = DIAG_INK_GREY;
    }
    return n;
}

bool diag_code_detail(const obd_diag_t *d, diag_tab_t tab, int i, obd_units_t units,
                      diag_code_t *out)
{
    memset(out, 0, sizeof *out);
    bool have;
    const obd_code_list_t *list = page_codes(d, tab, &have);
    if (list == NULL || !have || i < 0 || i >= list->count) {
        return false;
    }
    const obd_code_t *c = &list->code[i];
    snprintf(out->code, sizeof out->code, "%s", c->code);
    snprintf(out->kind, sizeof out->kind, "%s", tab == DIAG_TAB_STORED    ? "Stored"
                                                : tab == DIAG_TAB_PENDING ? "Pending"
                                                                          : "Permanent");
    if (c->has_ecu) {
        obd_diag_module_name(d->link.protocol, c->ecu, out->module, sizeof out->module);
    }
    snprintf(out->text, sizeof out->text, "%s", obd_dtc_text_for(c->code, d->make));
    if (tab == DIAG_TAB_STORED && d->freeze.have && strcmp(c->code, d->freeze.code) == 0) {
        out->have_freeze = true;
        for (int k = 0; k < d->freeze.count && k < OBD_FREEZE_MAX; k++) {
            const obd_pid_info_t *info = obd_pid_info(d->freeze.pid[k]);
            snprintf(out->freeze_label[k], sizeof out->freeze_label[k], "%s",
                     info != NULL ? info->label : "?");
            obd_pid_format(d->freeze.pid[k], d->freeze.value[k], units, out->freeze_value[k],
                           sizeof out->freeze_value[k]);
            out->freeze_count++;
        }
    }
    return true;
}
