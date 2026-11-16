/*
 * GaugeBoy app, 3DS side: consoles, touch screens and the worker thread.
 *
 * Screens: DIAG (default: scan, codes, readiness), SETUP (Wi-Fi slot,
 * SAVE LOG, the adapter test screen) and TEST (the M0 adapter test
 * screen). The bottom screen of DIAG and SETUP has the main menu
 * (LIVE / DIAG / TRIM / FUN / LOG / SETUP); M1 enables DIAG and SETUP.
 *
 * The UI thread never does network or file I/O after startup; the worker
 * thread runs the button actions and writes the log file.
 */
#include "spike_ui.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "data/code_history.h"
#include "data/config.h"
#include "data/diag_view.h"
#include "data/live_view.h"
#include "data/wifi_guide.h"
#include "data/textlog.h"
#include "spike_actions.h"
#include "transport/conntest_server.h"
#include "transport/net.h"
#include "transport/tcp_transport.h"
#include "ui/home_draw.h"
#include "obd/dtc.h"
#include "obd/obd_diag.h"
#include "obd/obd_live.h"
#include "version.h"

#define LOG_PATH      CONFIG_DIR "/spike-log.txt"
#define LOGS_DIR      CONFIG_DIR "/logs"   /* SAVE LOG copies go here */
#define QUEUE_MAX     8
#define WORKER_STACK  (64 * 1024)
#define FLASH_MS      150

#define TOP_COLS      TEXTLOG_COLS  /* 49 of 50: never print in the last column */
#define TOP_ROWS      30
#define LOG_FIRST_ROW 2
#define LOG_ROWS      (TOP_ROWS - LOG_FIRST_ROW)
#define BOTTOM_COLS   39            /* 39 of 40 */

#define BTN_SCROLL_OLDER (-1)
#define BTN_SCROLL_NEWER (-2)
#define BTN_SAVE_LOG     100   /* runs on the worker like a spike action */
#define BTN_WIFI_SLOT    101   /* worker: next Wi-Fi slot, saved to config.ini */
#define BTN_SETUP_SLOT   102   /* worker: read the slot, show what to enter */
#define BTN_SETUP_OK     (-3)  /* UI: close the setup screen */
#define BTN_DIAG_SCAN    104   /* worker: full DIAG scan */
#define BTN_CLEAR_CODES  105   /* worker: clear codes (only from the dialog), then rescan */
#define BTN_GUIDE_CHECK  106   /* worker: Wi-Fi guide check */
#define BTN_SET_MAKE     107   /* worker: save the car make, apply it to DIAG */
#define BTN_LIVE_SAVE    108   /* worker: save the LIVE PIDs to config.ini */
#define BTN_LIVE_TOGGLE  (-33) /* UI: LIVE START / STOP */
#define BTN_LIVE_PICK    (-80) /* UI: LIVE PID toggle (-80 - slot on the page) */
#define BTN_GUIDE        (-8)  /* UI: SETUP -> Wi-Fi guide */
#define BTN_CLEAR_ASK    (-5)  /* UI: open the clear-codes dialog */
#define BTN_CLEAR_CANCEL (-6)
#define BTN_CLEAR_YES    (-7)
#define BTN_BACK         (-4)  /* UI: TEST screen -> SETUP */
#define BTN_MENU_LIVE    (-10) /* UI: main menu */
#define BTN_MENU_DIAG    (-11)
#define BTN_MENU_TRIM    (-12)
#define BTN_MENU_FUN     (-13)
#define BTN_MENU_LOG     (-14)
#define BTN_MENU_SETUP   (-15)
#define BTN_HOME_CEL     (-16) /* UI: HOME status cards */
#define BTN_HOME_WIFI    (-17)
#define BTN_HOME_CLOSE   (-18) /* UI: HOME bar, same as START */
#define BTN_TAB_STORED   (-20) /* UI: DIAG tabs (-20 - tab) */
#define BTN_TAB_PENDING  (-21)
#define BTN_TAB_PERM     (-22)
#define BTN_TAB_READY    (-23)
#define BTN_TEST_SCREEN  (-30) /* UI: SETUP -> TEST */
#define BTN_MAKE_SCREEN  (-31) /* UI: SETUP -> car make picker */
#define BTN_MAKE_PICK    (-60) /* UI: -60 - button on the picker page */

/* Bottom-screen buttons in 8x8 console cells: 12-13x6 cells = 96-104x48 px.
 * The third row has the Wi-Fi slot buttons, the last one the log ones. */
typedef struct {
    const char *label;
    int action;          /* spike_action_t, or BTN_SCROLL_* */
    int col, row, w, h;
} button_t;

static const button_t buttons[] = {
    { NULL,         SPIKE_CONNECT,    0,  2, 12, 6 },
    { NULL,         SPIKE_ATZ,        13, 2, 13, 6 },
    { NULL,         SPIKE_SEARCH,     27, 2, 12, 6 },
    { NULL,         SPIKE_PROTOCOL,   0,  9, 12, 6 },
    { NULL,         SPIKE_VOLTAGE,    13, 9, 13, 6 },
    { NULL,         SPIKE_VIN,        27, 9, 12, 6 },
    { NULL,         BTN_WIFI_SLOT,    0, 16, 12, 6 },  /* "SLOT ANY" / "SLOT 2" */
    { "SETUP SLOT", BTN_SETUP_SLOT,   13, 16, 13, 6 },
    { "BACK",       BTN_BACK,         27, 16, 12, 6 },
    { "OLDER ^",    BTN_SCROLL_OLDER, 0, 23, 12, 6 },
    { "SAVE LOG",   BTN_SAVE_LOG,     13, 23, 13, 6 },
    { "NEWER v",    BTN_SCROLL_NEWER, 27, 23, 12, 6 },
};
#define BUTTON_COUNT ((int)(sizeof buttons / sizeof buttons[0]))

/* Layout (bottom screen, 8x8 cells, 40x30): big buttons, at least 40 px
 * (5 cells) each way; BACK bottom-left, the screen's action bottom-right
 * (gauge-boy-82l.8 / bxf.14). */

/* HOME: 3x2 grid of big buttons. */
static const button_t home_buttons[] = {
    { "LIVE",  BTN_MENU_LIVE,  0,  3, 12, 9 },
    { "DIAG",  BTN_MENU_DIAG,  13, 3, 13, 9 },
    { "TRIM",  BTN_MENU_TRIM,  27, 3, 12, 9 },
    { "FUN",   BTN_MENU_FUN,   0, 14, 12, 9 },
    { "LOG",   BTN_MENU_LOG,   13, 14, 13, 9 },
    { "SETUP", BTN_MENU_SETUP, 27, 14, 12, 9 },
};
#define HOME_BUTTON_COUNT ((int)(sizeof home_buttons / sizeof home_buttons[0]))

/* DIAG: tabs across the top, a paged list (3 rows of 5 cells), then
 * BACK < page > SCAN. */
#define BTN_PAGE_PREV  (-40)
#define BTN_PAGE_NEXT  (-41)
#define BTN_LIST_ROW   (-50)   /* -50 - row on the page */
#define LIST_FIRST_ROW 7
#define LIST_ROW_H     5
#define LIST_ROW_PITCH 6
#define LIST_PAGE_ROWS 3

static const button_t diag_buttons[] = {
    { "STORED",  BTN_TAB_STORED,  0,  1, 9, 5 },
    { "PENDING", BTN_TAB_PENDING, 10, 1, 9, 5 },
    { "PERM",    BTN_TAB_PERM,    20, 1, 9, 5 },  /* hidden if unsupported */
    { "READY",   BTN_TAB_READY,   30, 1, 9, 5 },
    { "BACK",    BTN_BACK,        0,  25, 8, 5 },
    { "<",       BTN_PAGE_PREV,   9,  25, 5, 5 },
    { ">",       BTN_PAGE_NEXT,   17, 25, 5, 5 },
    { "CLEAR",   BTN_CLEAR_ASK,   23, 25, 8, 5 },
    { "SCAN",    BTN_DIAG_SCAN,   31, 25, 8, 5 },
};
#define DIAG_BUTTON_COUNT ((int)(sizeof diag_buttons / sizeof diag_buttons[0]))

/* SETUP: full-width list rows like DIAG's list, then BACK ADAPTER ...
 * WI-FI GUIDE. The rows (w 39) are drawn as list bars; the bottom row
 * (row 25) as buttons. */
#define SETUP_BAR_ROW 25
static const button_t setup_buttons[] = {
    { "Wi-Fi slot",                    BTN_WIFI_SLOT,   0,  2, 39, 5 },
    { "Check the slot's IP and DNS",   BTN_SETUP_SLOT,  0,  8, 39, 5 },
    { "Car make",                      BTN_MAKE_SCREEN, 0, 14, 39, 5 },
    { "Save this session's log",       BTN_SAVE_LOG,    0, 20, 39, 5 },
    { "BACK",                          BTN_BACK,        0, 25, 8, 5 },
    { "ADAPTER",                       BTN_TEST_SCREEN, 9, 25, 9, 5 },
    { "WI-FI GUIDE",                   BTN_GUIDE,       26, 25, 13, 5 },
};
#define SETUP_BUTTON_COUNT ((int)(sizeof setup_buttons / sizeof setup_buttons[0]))

typedef enum {
    SCREEN_HOME, SCREEN_DIAG, SCREEN_SETUP, SCREEN_TEST, SCREEN_GUIDE, SCREEN_MAKE, SCREEN_LIVE
} screen_t;

/* LIVE: PID toggles (2 columns x 3 rows a page), then BACK < page > START. */
#define LIVE_PAGE_SLOTS 6
static const button_t live_buttons[] = {
    { "BACK", BTN_BACK,        0,  25, 8, 5 },
    { "<",    BTN_PAGE_PREV,   9,  25, 5, 5 },
    { ">",    BTN_PAGE_NEXT,   17, 25, 5, 5 },
    { NULL,   BTN_LIVE_TOGGLE, 26, 25, 13, 5 },   /* START / STOP */
};
#define LIVE_BUTTON_COUNT ((int)(sizeof live_buttons / sizeof live_buttons[0]))

static button_t live_slot(int slot)
{
    button_t b = { NULL, BTN_LIVE_PICK - slot, (slot % 2) * 20, 2 + (slot / 2) * 6, 19, 5 };
    return b;
}

/* Car make picker: "Auto" then every make with its own code meanings,
 * 3 x 4 buttons (12-13 x 5 cells) a page, then BACK < page >. */
#define MAKE_PAGE_SLOTS 12
#define MAKE_COLS       3
static const button_t make_nav_buttons[] = {
    { "BACK", BTN_BACK,      0,  25, 8, 5 },
    { "<",    BTN_PAGE_PREV, 9,  25, 5, 5 },
    { ">",    BTN_PAGE_NEXT, 17, 25, 5, 5 },
};
#define MAKE_NAV_COUNT ((int)(sizeof make_nav_buttons / sizeof make_nav_buttons[0]))

static int make_choice_count(void)
{
    return 1 + obd_dtc_make_count();
}

/* Choice k: 0 = Auto (from the VIN), else a make name. */
static const char *make_choice_name(int k)
{
    return k == 0 ? "Auto" : obd_dtc_make_name(k - 1);
}

static int make_page_count(void)
{
    return (make_choice_count() + MAKE_PAGE_SLOTS - 1) / MAKE_PAGE_SLOTS;
}

/* The choice for a config value ("" = Auto; unknown names show as Auto). */
static int make_choice_of(const char *car_make)
{
    int idx = car_make[0] != '\0' ? obd_dtc_make_find(car_make) : -1;
    return idx < 0 ? 0 : idx + 1;
}

/* This page's buttons, then the nav buttons. Returns the count. */
static int make_screen_buttons(int page, const button_t **out)
{
    static button_t list[MAKE_PAGE_SLOTS + MAKE_NAV_COUNT];
    static char labels[MAKE_PAGE_SLOTS][14];
    int n = 0;
    for (int slot = 0; slot < MAKE_PAGE_SLOTS; slot++) {
        int k = page * MAKE_PAGE_SLOTS + slot;
        if (k >= make_choice_count()) {
            break;
        }
        int col = slot % MAKE_COLS;
        int w = col == MAKE_COLS - 1 ? 13 : 12;
        /* "Mercedes-Benz" is too wide: cut at the hyphen. */
        snprintf(labels[slot], sizeof labels[slot], "%.*s", w,  make_choice_name(k));
        char *dash = strchr(labels[slot], '-');
        if (strlen(make_choice_name(k)) > (size_t)w && dash != NULL) {
            *dash = '\0';
        }
        list[n++] = (button_t){ labels[slot], BTN_MAKE_PICK - slot, col * 13, 1 + (slot / MAKE_COLS) * 6,
                                w, 5 };
    }
    for (int i = 0; i < MAKE_NAV_COUNT; i++) {
        list[n++] = make_nav_buttons[i];
    }
    *out = list;
    return n;
}

/* Wi-Fi guide: BACK < step > CHECK. */
static const button_t guide_buttons[] = {
    { "BACK",  BTN_BACK,        0,  25, 8, 5 },
    { "<",     BTN_PAGE_PREV,   9,  25, 5, 5 },
    { ">",     BTN_PAGE_NEXT,   17, 25, 5, 5 },
    { "CHECK", BTN_GUIDE_CHECK, 27, 25, 12, 5 },
};
#define GUIDE_BUTTON_COUNT ((int)(sizeof guide_buttons / sizeof guide_buttons[0]))
#define GUIDE_CHECK_LINES 6

/* Clear-codes dialog (safety rule: clearing only from here). */
static const button_t clear_buttons[] = {
    { "CANCEL",      BTN_CLEAR_CANCEL, 0,  23, 15, 6 },
    { "CLEAR CODES", BTN_CLEAR_YES,    17, 23, 22, 6 },
};
#define CLEAR_BUTTON_COUNT ((int)(sizeof clear_buttons / sizeof clear_buttons[0]))

/* Shown instead of the buttons above while the setup screen is open. */
static const button_t confirm_buttons[] = {
    { "OK", BTN_SETUP_OK, 10, 23, 19, 6 },
};
#define CONFIRM_BUTTON_COUNT ((int)(sizeof confirm_buttons / sizeof confirm_buttons[0]))
#define CONFIRM_LINES 6

/* Shared between the UI thread and the worker; guarded by lock. */
static struct {
    LightLock lock;
    LightEvent wake;
    textlog_t log;
    int queue[QUEUE_MAX];
    int queue_head;
    int queue_count;
    int busy;          /* action running, or -1 */
    bool connected;
    int wifi_slot;     /* copy of app_cfg->wifi_slot for the SLOT label */
    bool confirm;      /* SETUP SLOT screen showing */
    int confirm_slot;
    char confirm_title[SPIKE_COMPARE_COLS];
    char confirm_lines[CONFIRM_LINES][SPIKE_COMPARE_COLS];
    int confirm_count;

    obd_diag_t diag;           /* last scan result */
    unsigned diag_version;     /* bumped when diag or scan_step changes */
    bool scanning;
    obd_diag_step_t scan_step;
    char diag_msg[40];         /* last DIAG result line (clear codes), or "" */
    char guide_check[GUIDE_CHECK_LINES][40]; /* last Wi-Fi guide CHECK result */
    bool guide_checking;

    /* LIVE screen. The UI sets live_on and live_gen (PIDs changed); the
     * worker polls and publishes. */
    bool live_on;
    unsigned live_gen;
    obd_live_t live;           /* latest values (copy of the worker's poller) */
    bool live_have;            /* live holds a poller that was started */
    int live_protocol;
    float live_rate;
    unsigned live_version;     /* bumped on every published step */
    obd_link_t live_link;      /* supported PIDs, for the toggles */
    bool live_link_ok;
    char live_msg[48];         /* status line, "" while polling fine */
    unsigned live_ui_version;  /* bumped when the bottom screen must change */

    volatile bool quit;
} shared;

static gb_config_t *app_cfg; /* main's settings; wifi_slot changes on the worker */

static FILE *log_file;  /* written under shared.lock */
static long session_start = -1; /* offset of this session in LOG_PATH */

static PrintConsole top_console;
static PrintConsole bottom_console;

static void log_sink(void *ctx, const char *line)
{
    (void)ctx;
    /* Called from the worker and connection-test threads too. */
    LightLock_Lock(&shared.lock);
    textlog_add(&shared.log, line);
    if (log_file != NULL) {
        fputs(line, log_file);
        fputc('\n', log_file);
        fflush(log_file);
    }
    LightLock_Unlock(&shared.lock);
}

static void conntest_log(void *ctx, const char *line)
{
    spike_logf(ctx, "Conntest: %s", line);
}

static void enqueue(int action)
{
    LightLock_Lock(&shared.lock);
    if (shared.queue_count < QUEUE_MAX) {
        shared.queue[(shared.queue_head + shared.queue_count) % QUEUE_MAX] = action;
        shared.queue_count++;
    }
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

/* SAVE LOG (worker thread): copy this session's part of LOG_PATH to a
 * new file in LOGS_DIR, so it can be copied off the SD card and shared. */
static void save_log(spike_t *s)
{
    static char buf[4096];

    LightLock_Lock(&shared.lock);
    long end = -1;
    if (log_file != NULL) {
        fflush(log_file);
        end = ftell(log_file);
    }
    LightLock_Unlock(&shared.lock);
    if (end < 0 || session_start < 0 || end < session_start) {
        spike_logf(s, "! Can't save the log: no log file (is the SD card in?)");
        return;
    }

    time_t now = time(NULL);
    char name[64];
    char path[128];
    spike_format_log_name(name, sizeof name, gmtime(&now));
    snprintf(path, sizeof path, "%s/%s", LOGS_DIR, name);
    mkdir(LOGS_DIR, 0777);

    FILE *in = fopen(LOG_PATH, "rb");
    FILE *out = in != NULL ? fopen(path, "wb") : NULL;
    long left = end - session_start;
    bool ok = in != NULL && out != NULL && fseek(in, session_start, SEEK_SET) == 0;
    while (ok && left > 0) {
        size_t want = left < (long)sizeof buf ? (size_t)left : sizeof buf;
        size_t got = fread(buf, 1, want, in);
        ok = got > 0 && fwrite(buf, 1, got, out) == got;
        left -= (long)got;
    }
    if (out != NULL && fclose(out) != 0) {
        ok = false;
    }
    if (in != NULL) {
        fclose(in);
    }
    if (ok) {
        spike_logf(s, "= Log saved: %s (%ld bytes)", path, end - session_start);
    } else {
        spike_logf(s, "! Could not save the log to %s", path);
    }
}

/* SLOT (worker thread): any -> 1 -> 2 -> 3 -> any, saved to config.ini. */
static void next_wifi_slot(spike_t *s)
{
    LightLock_Lock(&shared.lock);
    int slot = (shared.wifi_slot + 1) % (CONFIG_WIFI_SLOTS + 1);
    shared.wifi_slot = slot;
    app_cfg->wifi_slot = (uint8_t)slot;
    gb_config_t copy = *app_cfg;
    LightLock_Unlock(&shared.lock);

    net_set_wifi_slot(slot);
    bool saved = config_save(&copy, CONFIG_PATH);
    if (slot == 0) {
        spike_logf(s, "Wi-Fi slot: any saved connection in range%s",
                   saved ? "" : " (! not saved to config.ini)");
    } else {
        char name[40];
        net_slot_name(slot, name, sizeof name);
        spike_logf(s, "Wi-Fi slot: %d \"%s\"; CONNECT joins only this slot%s", slot, name,
                   saved ? "" : " (! not saved to config.ini)");
    }
}

/* Car make (worker): app_cfg->car_make was set by the UI; apply it to the
 * last scan and save config.ini. */
static void set_car_make(spike_t *s)
{
    LightLock_Lock(&shared.lock);
    gb_config_t copy = *app_cfg;
    obd_diag_apply_make(&shared.diag, copy.car_make);
    bool vin_wins = copy.car_make[0] != '\0' && shared.diag.maker != NULL &&
                    !shared.diag.make_chosen;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);

    bool saved = config_save(&copy, CONFIG_PATH);
    spike_logf(s, "Car make: %s%s%s",
               copy.car_make[0] != '\0' ? copy.car_make : "Auto (from the VIN)",
               vin_wins ? "; this car's VIN gives its make, so that is used" : "",
               saved ? "" : " (! not saved to config.ini)");
}

/* SETUP SLOT (worker): read the chosen slot and, if it differs from what
 * GaugeBoy needs, show NOW / NEW so the user can enter NEW in System
 * Settings. Read-only: writing slot settings from the app crashed the
 * 3DS (gauge-boy-7v5.31). */
static void setup_slot_preview(spike_t *s)
{
    LightLock_Lock(&shared.lock);
    int slot = shared.wifi_slot;
    char host[sizeof app_cfg->adapter_host];
    strcpy(host, app_cfg->adapter_host);
    LightLock_Unlock(&shared.lock);

    if (slot == 0) {
        spike_logf(s, "! SETUP SLOT: tap SLOT first to pick the adapter's slot");
        return;
    }
    char name[40];
    if (!net_slot_name(slot, name, sizeof name)) {
        spike_logf(s, "! SETUP SLOT: slot %d is empty; save the adapter's Wi-Fi in System Settings first", slot);
        return;
    }
    net_ip_settings_t now, plan;
    char err[80];
    if (!net_slot_read(slot, &now, err, sizeof err)) {
        spike_logf(s, "! SETUP SLOT: %s", err);
        return;
    }
    if (!spike_plan_slot(host, &plan)) {
        spike_logf(s, "! SETUP SLOT: adapter address %s isn't an IPv4 address", host);
        return;
    }
    if (spike_slot_matches(&now, &plan)) {
        spike_logf(s, "= Slot %d \"%s\" is set up for GaugeBoy", slot, name);
        return;
    }

    LightLock_Lock(&shared.lock);
    shared.confirm_slot = slot;
    snprintf(shared.confirm_title, sizeof shared.confirm_title, "Slot %d \"%.20s\" needs:", slot, name);
    shared.confirm_count = spike_format_slot_compare(&now, &plan, shared.confirm_lines, CONFIRM_LINES);
    shared.confirm = true;
    LightLock_Unlock(&shared.lock);
    spike_logf(s, "SETUP SLOT: slot %d needs changes; enter the NEW values in System Settings", slot);
}

/* Code history (worker thread): append rows to CONFIG_DIR/code-history.csv,
 * writing the header when the file is new. */
static void now_text(char *out, size_t len)
{
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);   /* the 3DS clock is local time */
    if (tm == NULL || strftime(out, len, "%Y-%m-%d %H:%M:%S", tm) == 0) {
        snprintf(out, len, "unknown");
    }
}

static void append_history(spike_t *s, const char *rows)
{
    if (rows[0] == '\0') {
        return;
    }
    const char *path = CONFIG_DIR "/" CODE_HISTORY_FILE;
    FILE *f = fopen(path, "a");
    if (f == NULL) {
        spike_logf(s, "! Could not write %s", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    bool ok = true;
    if (ftell(f) == 0) {
        ok = fputs(CODE_HISTORY_HEADER, f) >= 0;
    }
    ok = ok && fputs(rows, f) >= 0;
    if (fclose(f) != 0 || !ok) {
        spike_logf(s, "! Could not write %s", path);
    }
}

static void scan_progress(void *ctx, obd_diag_step_t step)
{
    (void)ctx;
    LightLock_Lock(&shared.lock);
    shared.scan_step = step;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
}

/* Worker: the adapter is reset (ATZ ...) once per connection, and the
 * protocol search result is reused while the connection lasts. */
static unsigned reset_at_connect = ~0u;
static obd_link_t last_link;
static unsigned last_link_connects = ~0u;

/* SCAN (worker): connect if needed, then the full DIAG scan. The adapter
 * is reset (ATZ ...) on the first scan after each new connection. */
static void diag_scan(spike_t *s)
{
    static obd_diag_t d;

    LightLock_Lock(&shared.lock);
    shared.scanning = true;
    shared.scan_step = OBD_DIAG_STEP_CONNECT;
    shared.diag_msg[0] = '\0';
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);

    spike_logf(s, "SCAN");
    if (spike_connect_if_needed(s)) {
        bool reset = s->transport->connects != reset_at_connect;
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        obd_diag_scan(s->elm, reset, tm != NULL ? tm->tm_year + 1900 : 0, &d,
                      spike_trace_command, scan_progress, s);
        char car_make[sizeof app_cfg->car_make];
        LightLock_Lock(&shared.lock);
        strcpy(car_make, app_cfg->car_make);
        LightLock_Unlock(&shared.lock);
        obd_diag_apply_make(&d, car_make);
        if (d.result != OBD_LINK_LOST) {
            reset_at_connect = s->transport->connects;
        }
        if (d.result == OBD_LINK_OK) {
            last_link = d.link;
            last_link_connects = s->transport->connects;
        }
        static char rows[4096];
        char when[24];
        now_text(when, sizeof when);
        if (code_history_scan_rows(&d, when, rows, sizeof rows) > 0) {
            append_history(s, rows);
        }
        if (d.result == OBD_LINK_OK) {
            spike_logf(s, "= Scan: CEL %s, %d stored, %d pending%s", 
                       d.have_readiness ? (d.readiness.mil_on ? "ON" : "off") : "?",
                       d.stored.count, d.pending.count,
                       d.stopped_at == OBD_DIAG_STEP_DONE ? "" : " (incomplete)");
        } else {
            spike_logf(s, "! Scan: %s", obd_link_result_str(d.result));
        }
    } else {
        memset(&d, 0, sizeof d);
        d.make = -1;
        d.result = OBD_LINK_NO_ADAPTER;
        spike_logf(s, "! Scan: can't reach the adapter (see SETUP > ADAPTER TEST)");
    }

    LightLock_Lock(&shared.lock);
    shared.diag = d;
    shared.scanning = false;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
}

/* CLEAR CODES (worker, only after the dialog): parked check, 04, then a
 * new scan to show the result. */
static void clear_codes(spike_t *s)
{
    static obd_link_t link;
    LightLock_Lock(&shared.lock);
    link = shared.diag.link;
    LightLock_Unlock(&shared.lock);

    obd_clear_result_t res = OBD_CLEAR_LOST;
    uint8_t nrc = 0;
    spike_logf(s, "CLEAR CODES (confirmed)");
    if (link.protocol != 0 && spike_connect_if_needed(s)) {
        res = obd_clear_codes(s->elm, &link, &nrc, spike_trace_command, s);
    }
    if (res == OBD_CLEAR_REFUSED) {
        spike_logf(s, "! %s (code %02X)", obd_clear_result_str(res), nrc);
    } else {
        spike_logf(s, "%s %s", res == OBD_CLEAR_DONE ? "=" : "!", obd_clear_result_str(res));
    }
    {
        static obd_diag_t before;
        char row[512];
        char when[24];
        LightLock_Lock(&shared.lock);
        before = shared.diag;
        LightLock_Unlock(&shared.lock);
        now_text(when, sizeof when);
        if (code_history_clear_row(&before, when, obd_clear_result_str(res), row, sizeof row) > 0) {
            append_history(s, row);
        }
    }
    if (res == OBD_CLEAR_DONE) {
        diag_scan(s);
    }
    LightLock_Lock(&shared.lock);
    snprintf(shared.diag_msg, sizeof shared.diag_msg, "%s", obd_clear_result_str(res));
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
}

/* Wi-Fi guide CHECK (worker): slot chosen and named, its IP / DNS as the
 * guide says, on Wi-Fi, adapter answering. */
static void guide_check(spike_t *s)
{
    char out[GUIDE_CHECK_LINES][40];
    memset(out, 0, sizeof out);
    int n = 0;
    LightLock_Lock(&shared.lock);
    int slot = shared.wifi_slot;
    char host[sizeof app_cfg->adapter_host];
    strcpy(host, app_cfg->adapter_host);
    shared.guide_checking = true;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);

    if (slot == 0) {
        snprintf(out[n++], 40, "! Slot: Any (pick it in SETUP)");
    } else {
        char name[40];
        if (!net_slot_name(slot, name, sizeof name)) {
            snprintf(out[n++], 40, "! Slot %d is empty (step 2)", slot);
        } else {
            snprintf(out[n++], 40, "OK Slot %d: %.28s", slot, name);
            net_ip_settings_t now, plan;
            char err[80];
            if (!net_slot_read(slot, &now, err, sizeof err) || !spike_plan_slot(host, &plan)) {
                snprintf(out[n++], 40, "! Can't read the slot's settings");
            } else if (spike_slot_matches(&now, &plan)) {
                snprintf(out[n++], 40, "OK IP and DNS as in steps 3-4");
            } else {
                snprintf(out[n++], 40, "! IP / DNS differ from steps 3-4");
                snprintf(out[n++], 40, "  (SETUP > Check the slot's IP...)");
            }
        }
    }
    char info[160];
    net_describe(info, sizeof info);
    bool on = strstr(info, "Wi-Fi on") != NULL;
    snprintf(out[n++], 40, "%s %s", on ? "OK" : "!", on ? "On Wi-Fi" : "Not on Wi-Fi yet");

    bool answered = false;
    char id[ELM_LINE_MAX] = "";
    if (spike_connect_if_needed(s)) {
        static elm_response_t r;
        if (elm_command(s->elm, "ATI", ELM_TIMEOUT_DEFAULT_MS, &r) == ELM_OK && r.line_count > 0) {
            answered = true;
            snprintf(id, sizeof id, "%s", r.lines[r.line_count - 1]);
        }
    }
    if (answered) {
        snprintf(out[n++], 40, "OK Adapter: %.27s", id);
    } else {
        snprintf(out[n++], 40, "! Adapter not answering");
    }
    for (int i = 0; i < n; i++) {
        spike_logf(s, "Guide check: %s", out[i]);
    }

    LightLock_Lock(&shared.lock);
    memcpy(shared.guide_check, out, sizeof out);
    shared.guide_checking = false;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
}

/* LIVE (worker): publish the poller and a status line. */
static void live_publish(const obd_live_t *lv, bool have, int protocol, uint64_t now,
                         const char *msg, bool ui_changed)
{
    LightLock_Lock(&shared.lock);
    if (lv != NULL) {
        shared.live = *lv;
        shared.live_rate = obd_live_rate(lv, now);
    }
    shared.live_have = have;
    shared.live_protocol = protocol;
    if (msg != NULL && strcmp(msg, shared.live_msg) != 0) {
        snprintf(shared.live_msg, sizeof shared.live_msg, "%s", msg);
        ui_changed = true;
    }
    if (ui_changed) {
        shared.live_ui_version++;
    }
    shared.live_version++;
    LightLock_Unlock(&shared.lock);
}

static void live_stop(const char *msg)
{
    LightLock_Lock(&shared.lock);
    shared.live_on = false;
    LightLock_Unlock(&shared.lock);
    live_publish(NULL, false, 0, 0, msg, true);
}

typedef enum { LIVE_OPEN_OK, LIVE_OPEN_RETRY, LIVE_OPEN_STOP } live_open_t;

/* Connect and find the protocol (or reuse this connection's DIAG scan). */
static live_open_t live_open(spike_t *s, obd_link_t *link)
{
    if (!spike_connect_if_needed(s)) {
        live_publish(NULL, false, 0, 0, "Can't reach the adapter; retrying", false);
        return LIVE_OPEN_RETRY;
    }
    unsigned connects = s->transport->connects;
    if (last_link.protocol != 0 && last_link_connects == connects) {
        *link = last_link;
        return LIVE_OPEN_OK;
    }
    live_publish(NULL, false, 0, 0, "Looking for the car...", false);
    bool reset = connects != reset_at_connect;
    obd_link_result_t r = obd_link_open_traced(s->elm, reset, link, spike_trace_command, s);
    if (r == OBD_LINK_LOST) {
        tcp_transport_close(s->transport);
        return LIVE_OPEN_RETRY;
    }
    reset_at_connect = connects;
    if (r != OBD_LINK_OK) {
        spike_logf(s, "! LIVE: %s", obd_link_result_str(r));
        return LIVE_OPEN_STOP;
    }
    last_link = *link;
    last_link_connects = connects;
    return LIVE_OPEN_OK;
}

/* LIVE (worker): one poll while the LIVE screen polls. */
static void live_poll(spike_t *s)
{
    static obd_live_t lv;
    static obd_link_t link;
    static bool ready;
    static unsigned gen_seen;

    LightLock_Lock(&shared.lock);
    bool on = shared.live_on;
    unsigned gen = shared.live_gen;
    uint8_t pids[CONFIG_LIVE_PIDS_MAX];
    int n = app_cfg->live_pid_count;
    memcpy(pids, app_cfg->live_pids, sizeof pids);
    LightLock_Unlock(&shared.lock);

    if (!on) {
        ready = false;   /* start again from the link next time */
        return;
    }
    if (!ready) {
        live_open_t o = live_open(s, &link);
        if (o == LIVE_OPEN_RETRY) {
            svcSleepThread(1000LL * 1000 * 1000);
            return;
        }
        if (o == LIVE_OPEN_STOP) {
            live_stop("Car not answering (is the engine running?)");
            return;
        }
        LightLock_Lock(&shared.lock);
        shared.live_link = link;
        shared.live_link_ok = true;
        LightLock_Unlock(&shared.lock);
    }
    if (!ready || gen != gen_seen) {
        int count = obd_live_init(&lv, &link, pids, n);
        char list[3 * OBD_LIVE_MAX + 1] = "";
        for (int i = 0; i < count; i++) {
            snprintf(list + strlen(list), sizeof list - strlen(list), " %02X",
                     (unsigned)lv.pid[i].pid);
        }
        spike_logf(s, "LIVE: polling %d PID%s:%s", count, count == 1 ? "" : "s", list);
        ready = true;
        gen_seen = gen;
        live_publish(&lv, true, link.protocol, tp_now_ms(), "", true);
    }

    obd_live_step_t st = obd_live_step(&lv, s->elm, NULL, NULL);
    if (st == OBD_LIVE_LOST) {
        spike_logf(s, "! LIVE: the adapter stopped answering; reconnecting");
        tcp_transport_close(s->transport);
        ready = false;
        live_publish(&lv, true, link.protocol, tp_now_ms(), "Connection lost; reconnecting", false);
        return;
    }
    if (st == OBD_LIVE_IDLE) {
        if (lv.count == 0) {
            live_publish(&lv, true, link.protocol, tp_now_ms(), "No PIDs chosen", false);
            svcSleepThread(200LL * 1000 * 1000);
            return;
        }
        spike_logf(s, "! LIVE: every PID stopped answering");
        live_publish(&lv, true, link.protocol, tp_now_ms(), "", false);
        live_stop("The car stopped answering");
        return;
    }
    live_publish(&lv, true, link.protocol, tp_now_ms(), "", false);
}

/* LIVE PIDs to config.ini (worker). */
static void live_save(spike_t *s)
{
    LightLock_Lock(&shared.lock);
    gb_config_t copy = *app_cfg;
    LightLock_Unlock(&shared.lock);
    bool saved = config_save(&copy, CONFIG_PATH);
    spike_logf(s, "LIVE: %d PIDs chosen%s", copy.live_pid_count,
               saved ? ", saved" : " (! not saved to config.ini)");
}

static void worker_main(void *arg)
{
    spike_t *spike = arg;

    while (!shared.quit) {
        LightLock_Lock(&shared.lock);
        bool polling = shared.live_on;
        LightLock_Unlock(&shared.lock);
        if (!polling) {
            LightEvent_WaitTimeout(&shared.wake, 200LL * 1000 * 1000);
        }
        for (;;) {
            int action = -1;
            LightLock_Lock(&shared.lock);
            if (shared.queue_count > 0 && !shared.quit) {
                action = shared.queue[shared.queue_head];
                shared.queue_head = (shared.queue_head + 1) % QUEUE_MAX;
                shared.queue_count--;
                shared.busy = action;
            }
            LightLock_Unlock(&shared.lock);
            if (action < 0) {
                break;
            }

            if (action == BTN_SAVE_LOG) {
                save_log(spike);
            } else if (action == BTN_WIFI_SLOT) {
                next_wifi_slot(spike);
            } else if (action == BTN_SETUP_SLOT) {
                setup_slot_preview(spike);
            } else if (action == BTN_DIAG_SCAN) {
                diag_scan(spike);
            } else if (action == BTN_CLEAR_CODES) {
                clear_codes(spike);
            } else if (action == BTN_GUIDE_CHECK) {
                guide_check(spike);
            } else if (action == BTN_SET_MAKE) {
                set_car_make(spike);
            } else if (action == BTN_LIVE_SAVE) {
                live_save(spike);
            } else {
                spike_run(spike, (spike_action_t)action);
            }

            LightLock_Lock(&shared.lock);
            shared.busy = -1;
            shared.connected = tcp_transport_connected(spike->transport);
            LightLock_Unlock(&shared.lock);
        }
        if (polling) {
            live_poll(spike);
            LightLock_Lock(&shared.lock);
            shared.connected = tcp_transport_connected(spike->transport);
            LightLock_Unlock(&shared.lock);
        }
    }
}

/* ---------- drawing (UI thread only) ---------- */

static void print_row(int row, int width, const char *text)
{
    printf("\x1b[%d;1H%-*.*s", row + 1, width, width, text);
}

static void draw_top(int scroll, int busy, bool connected)
{
    static char visible[LOG_ROWS][TEXTLOG_COLS + 1];
    char header[TOP_COLS + 1];
    int count;

    LightLock_Lock(&shared.lock);
    count = textlog_count(&shared.log);
    int first = count - LOG_ROWS - scroll;
    if (first < 0) {
        first = 0;
    }
    for (int i = 0; i < LOG_ROWS; i++) {
        strcpy(visible[i], textlog_line(&shared.log, first + i));
    }
    LightLock_Unlock(&shared.lock);

    spike_format_header(header, sizeof header, connected, busy);

    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, scroll > 0 ? "-- scrolled back: tap NEWER to follow --"
                                      : "-------------------------------------------------");
    for (int i = 0; i < LOG_ROWS; i++) {
        print_row(LOG_FIRST_ROW + i, TOP_COLS, visible[i]);
    }
}

static void draw_button_ex(const button_t *b, bool flash, bool busy, bool selected)
{
    char slot_label[12];
    const char *label = b->label;
    if (b->action == BTN_WIFI_SLOT) {
        LightLock_Lock(&shared.lock);
        int slot = shared.wifi_slot;
        LightLock_Unlock(&shared.lock);
        if (slot == 0) {
            snprintf(slot_label, sizeof slot_label, "SLOT ANY");
        } else {
            snprintf(slot_label, sizeof slot_label, "SLOT %d", slot);
        }
        label = slot_label;
    } else if (label == NULL) {
        label = spike_action_label((spike_action_t)b->action);
    }
    const char *colors = busy ? "\x1b[41;37m" : flash ? "\x1b[43;30m"
                       : selected ? "\x1b[42;30m" : "\x1b[44;37m";
    int label_len = (int)strlen(label);
    if (label_len > b->w) {
        label_len = b->w;
    }
    int pad = (b->w - label_len) / 2;

    printf("%s", colors);
    for (int r = 0; r < b->h; r++) {
        printf("\x1b[%d;%dH", b->row + r + 1, b->col + 1);
        if (r == b->h / 2) {
            printf("%*s%.*s%*s", pad, "", label_len, label, b->w - pad - label_len, "");
        } else {
            printf("%*s", b->w, "");
        }
    }
    printf("\x1b[0m");
}

static void draw_button(const button_t *b, bool flash, bool busy)
{
    draw_button_ex(b, flash, busy, false);
}

static void clear_rows(int first, int last)
{
    for (int r = first; r <= last; r++) {
        print_row(r, BOTTOM_COLS, "");
    }
}

/* DIAG / HOME top screen: header, title or scan progress, fact rows,
 * then the selected row's full text (DIAG). */
static diag_item_t items[DIAG_ITEMS_MAX];
static char top_lines[DIAG_VIEW_MAX][DIAG_VIEW_COLS + 1];

/* Rows of the current tab (from the last scan). */
static int diag_item_count(diag_tab_t tab)
{
    LightLock_Lock(&shared.lock);
    int n = diag_items(&shared.diag, tab, items, DIAG_ITEMS_MAX);
    LightLock_Unlock(&shared.lock);
    return n;
}

static void draw_diag_top(bool home, diag_tab_t tab, int selected, int busy, bool connected)
{
    char header[TOP_COLS + 1];
    char title[TOP_COLS + 1];
    char empty[TOP_COLS + 1];

    LightLock_Lock(&shared.lock);
    bool scanning = shared.scanning;
    obd_diag_step_t step = shared.scan_step;
    int nf = diag_facts(&shared.diag, TOP_COLS, top_lines, 10);
    int n = diag_items(&shared.diag, tab, items, DIAG_ITEMS_MAX);
    snprintf(empty, sizeof empty, "%s", diag_empty_text(&shared.diag, tab));
    LightLock_Unlock(&shared.lock);

    spike_format_header(header, sizeof header, connected, busy);
    if (scanning) {
        snprintf(title, sizeof title, ">> Scanning: %s...", obd_diag_step_str(step));
    } else if (home) {
        snprintf(title, sizeof title, "GaugeBoy: OBD-II Deluxe");
    } else {
        snprintf(title, sizeof title, "DIAG: %s (%d)", diag_tab_name(tab), n);
    }

    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    print_row(2, TOP_COLS, title);
    int row = 4;
    for (int i = 0; i < nf && row < TOP_ROWS; i++) {
        print_row(row++, TOP_COLS, top_lines[i]);
    }
    print_row(row++, TOP_COLS, "");
    if (home) {
        print_row(row++, TOP_COLS, "Tap DIAG, then SCAN with the engine running.");
    } else if (n == 0) {
        print_row(row++, TOP_COLS, empty);
    } else if (selected >= 0 && selected < n) {
        static char wrapped[8][DIAG_VIEW_COLS + 1];
        int w = diag_wrap(items[selected].detail, TOP_COLS, 2, wrapped, 8);
        for (int i = 0; i < w && row < TOP_ROWS; i++) {
            print_row(row++, TOP_COLS, wrapped[i]);
        }
    } else {
        print_row(row++, TOP_COLS, "Tap a row for its full description.");
    }
    while (row < TOP_ROWS) {
        print_row(row++, TOP_COLS, "");
    }
}

/* The SETUP SLOT confirmation: NOW / NEW table, SAVE and CANCEL. */
static void draw_confirm(void)
{
    char title[SPIKE_COMPARE_COLS];
    char lines[CONFIRM_LINES][SPIKE_COMPARE_COLS];
    LightLock_Lock(&shared.lock);
    int n = shared.confirm_count;
    memcpy(title, shared.confirm_title, sizeof title);
    memcpy(lines, shared.confirm_lines, sizeof lines);
    LightLock_Unlock(&shared.lock);

    consoleSelect(&bottom_console);
    printf("\x1b[0m");
    for (int r = 0; r < 30; r++) {
        print_row(r, BOTTOM_COLS, "");
    }
    print_row(0, BOTTOM_COLS, title);
    for (int i = 0; i < n; i++) {
        print_row(2 + i, BOTTOM_COLS, lines[i]);
    }
    print_row(9, BOTTOM_COLS, "Enter NEW in System Settings >");
    print_row(10, BOTTOM_COLS, "Internet Settings > Connection");
    print_row(11, BOTTOM_COLS, "Settings > this slot > Change Settings:");
    print_row(12, BOTTOM_COLS, " IP Address: Auto-Obtain No, then IP,");
    print_row(13, BOTTOM_COLS, "  Subnet Mask (Mask), Gateway (Gate)");
    print_row(14, BOTTOM_COLS, " DNS: Auto-Obtain No, then DNS 1, 2");
    print_row(16, BOTTOM_COLS, "The 3DS shows 3 digits per part:");
    print_row(17, BOTTOM_COLS, "192.168.0.11 = 192.168.000.011");
    print_row(19, BOTTOM_COLS, "Then tap SETUP SLOT again to check.");
    for (int i = 0; i < CONFIRM_BUTTON_COUNT; i++) {
        draw_button(&confirm_buttons[i], false, false);
    }
}

typedef struct {
    screen_t screen;
    diag_tab_t tab;
    int selected;              /* DIAG list row selected, -1 = none */
    int page;
    const char *notice;        /* one-line message, or NULL */
    bool permanent_hidden;
    bool clear_dialog;         /* clear-codes question showing */
    int home_selected;         /* HOME card tapped last (home_item_t), -1 = none */
    screen_t guide_back;       /* where BACK goes from the Wi-Fi guide */
    bool live_dirty;           /* LIVE PIDs changed: save on BACK */
} ui_state_t;

static bool home_gfx;          /* HOME drawn with citro2d (else the text layout) */

/* The action for a HOME card. */
static int home_item_action(int item)
{
    switch (item) {
    case HOME_CEL:     return BTN_HOME_CEL;
    case HOME_ADAPTER: return BTN_HOME_WIFI;
    case HOME_LIVE:    return BTN_MENU_LIVE;
    case HOME_DIAG:    return BTN_MENU_DIAG;
    case HOME_TRIM:    return BTN_MENU_TRIM;
    case HOME_FUN:     return BTN_MENU_FUN;
    case HOME_LOG:     return BTN_MENU_LOG;
    case HOME_SETUP:   return BTN_MENU_SETUP;
    case HOME_CLOSE:   return BTN_HOME_CLOSE;
    default:           return 0;
    }
}

/* Flashing, by button action (UI thread). */
#define FLASH_SLOTS 8
static struct { int action; uint64_t until; } flashes[FLASH_SLOTS];

static void flash(int action, uint64_t now)
{
    for (int i = 0; i < FLASH_SLOTS; i++) {
        if (flashes[i].until == 0 || flashes[i].action == action) {
            flashes[i].action = action;
            flashes[i].until = now + FLASH_MS;
            return;
        }
    }
}

static bool flashing(int action, uint64_t now)
{
    for (int i = 0; i < FLASH_SLOTS; i++) {
        if (flashes[i].until != 0 && flashes[i].action == action && now < flashes[i].until) {
            return true;
        }
    }
    return false;
}

/* True if a flash ran out (needs a redraw). */
static bool flashes_expired(uint64_t now)
{
    bool any = false;
    for (int i = 0; i < FLASH_SLOTS; i++) {
        if (flashes[i].until != 0 && now >= flashes[i].until) {
            flashes[i].until = 0;
            any = true;
        }
    }
    return any;
}

static void draw_list(const button_t *list, int count, uint64_t now, int busy, int selected,
                      bool hide_permanent)
{
    for (int i = 0; i < count; i++) {
        const button_t *b = &list[i];
        if (hide_permanent && b->action == BTN_TAB_PERM) {
            continue;
        }
        draw_button_ex(b, flashing(b->action, now), b->action >= 0 && b->action == busy,
                       b->action == selected);
    }
}

/* One DIAG list row: a 5-cell-high bar with the text in its middle line. */
static void draw_list_row(int slot, const char *text, bool selected)
{
    int top = LIST_FIRST_ROW + slot * LIST_ROW_PITCH;
    printf("%s", selected ? "\x1b[42;30m" : "\x1b[47;30m");
    for (int r = 0; r < LIST_ROW_H; r++) {
        printf("\x1b[%d;1H %-37.37s ", top + r + 1, r == LIST_ROW_H / 2 ? text : "");
    }
    printf("\x1b[0m");
}

static int page_count(int n)
{
    return n == 0 ? 1 : (n + LIST_PAGE_ROWS - 1) / LIST_PAGE_ROWS;
}

/* LIVE: the PIDs the car offers (empty until the protocol is known) and
 * how many toggle pages they take. */
static int live_choices_now(uint8_t *out)
{
    LightLock_Lock(&shared.lock);
    static obd_link_t link;
    bool ok = shared.live_link_ok;
    link = shared.live_link;
    LightLock_Unlock(&shared.lock);
    return ok ? live_choices(&link, out, OBD_LIVE_MAX) : 0;
}

static int live_page_count(void)
{
    uint8_t choices[OBD_LIVE_MAX];
    int n = live_choices_now(choices);
    return n == 0 ? 1 : (n + LIVE_PAGE_SLOTS - 1) / LIVE_PAGE_SLOTS;
}

/* LIVE top screen: the values, refresh rates and a status line. */
static void draw_live_top(int busy, bool connected)
{
    static obd_live_t lv;
    static char lines[LIVE_VIEW_MAX][LIVE_VIEW_COLS + 1];
    char header[TOP_COLS + 1];
    char title[TOP_COLS + 8];   /* print_row cuts it to the width */
    char msg[sizeof shared.live_msg];
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have = shared.live_have;
    int protocol = shared.live_protocol;
    float rate = shared.live_rate;
    bool on = shared.live_on;
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);

    int n = live_view_lines(have ? &lv : NULL, have ? protocol : 0, rate, OBD_UNITS_US, TOP_COLS,
                            lines, LIVE_VIEW_MAX);
    spike_format_header(header, sizeof header, connected, busy);
    snprintf(title, sizeof title, "LIVE  %s", lines[0]);
    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    print_row(2, TOP_COLS, title);
    for (int i = 1; i < TOP_ROWS - 3; i++) {
        print_row(2 + i, TOP_COLS, i < n ? lines[i] : "");
    }
    print_row(TOP_ROWS - 1, TOP_COLS, msg[0] != '\0' ? msg
                                      : on ? "Polling. Fewer PIDs refresh faster."
                                           : "Stopped. Tap START to poll.");
}

/* Wi-Fi guide top screen: step title and text. */
static void draw_guide_top(int step, const char *host, int busy, bool connected)
{
    char header[TOP_COLS + 1];
    char title[TOP_COLS + 1];
    static char lines[TOP_ROWS][DIAG_VIEW_COLS + 1];
    int n = wifi_guide_lines(step, host, TOP_COLS, lines, TOP_ROWS - 4);
    spike_format_header(header, sizeof header, connected, busy);
    snprintf(title, sizeof title, "Wi-Fi setup %d/%d: %s", step + 1, WIFI_GUIDE_STEPS,
             wifi_guide_title(step));
    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    print_row(2, TOP_COLS, title);
    print_row(3, TOP_COLS, "");
    for (int i = 0; i < TOP_ROWS - 4; i++) {
        print_row(4 + i, TOP_COLS, i < n ? lines[i] : "");
    }
}

/* The clear-codes question. Red CLEAR CODES, plain CANCEL. */
static void draw_clear_dialog(void)
{
    static const char *const text[] = {
        "Clear the trouble codes?",
        "",
        "This turns OFF the check engine light",
        "and RESETS the readiness monitors. The",
        "car then needs more driving before an",
        "emissions test will pass.",
        "",
        "If the fault is still there, the codes",
        "and the light will come back.",
        "",
        "Park first. Ignition on, engine off",
        "works on most cars.",
    };
    consoleSelect(&bottom_console);
    printf("\x1b[0m");
    clear_rows(0, 29);
    for (size_t i = 0; i < sizeof text / sizeof text[0]; i++) {
        print_row(2 + (int)i, BOTTOM_COLS, text[i]);
    }
    const button_t *cancel = &clear_buttons[0];
    const button_t *yes = &clear_buttons[1];
    draw_button_ex(cancel, false, false, false);
    /* Red: destructive. */
    printf("\x1b[41;37m");
    for (int r = 0; r < yes->h; r++) {
        printf("\x1b[%d;%dH%*s", yes->row + r + 1, yes->col + 1, yes->w, "");
    }
    printf("\x1b[%d;%dH%s", yes->row + yes->h / 2 + 1, yes->col + 6, yes->label);
    printf("\x1b[0m");
}

static void draw_bottom(const gb_config_t *cfg, const ui_state_t *ui, uint64_t now, int busy,
                        bool full)
{
    char line[BOTTOM_COLS + 1];

    LightLock_Lock(&shared.lock);
    bool confirm = shared.confirm;
    bool cel_on = shared.diag.have_readiness && shared.diag.readiness.mil_on;
    bool scanning = shared.scanning;
    LightLock_Unlock(&shared.lock);
    static bool cleared_after_confirm = true;
    if (confirm) {
        draw_confirm();
        cleared_after_confirm = false;
        return;
    }
    if (ui->clear_dialog) {
        draw_clear_dialog();
        cleared_after_confirm = false;
        return;
    }
    if (ui->screen == SCREEN_HOME && home_gfx) {
        home_view_t v;
        LightLock_Lock(&shared.lock);
        v.cel_known = shared.diag.have_readiness;
        v.connected = shared.connected;
        LightLock_Unlock(&shared.lock);
        v.cel_on = cel_on;
        v.selected = ui->home_selected;
        v.pressed = -1;
        for (int i = 0; i < HOME_ITEM_COUNT; i++) {
            if (flashing(home_item_action(i), now)) {
                v.pressed = i;
            }
        }
        v.notice = ui->notice;
        home_draw(&v);
        return;
    }

    consoleSelect(&bottom_console);
    printf("\x1b[0m");
    if (!cleared_after_confirm || full) {
        printf("\x1b[2J");   /* all 40 columns: HOME's graphics reach the last one */
        cleared_after_confirm = true;
    }

    if (ui->screen == SCREEN_TEST) {
        snprintf(line, sizeof line, "Adapter test  %s:%u", cfg->adapter_host,
                 (unsigned)cfg->adapter_port);
        print_row(0, BOTTOM_COLS, line);
        for (int i = 0; i < BUTTON_COUNT; i++) {
            draw_button(&buttons[i], flashing(buttons[i].action, now),
                        buttons[i].action >= 0 && buttons[i].action == busy);
        }
        print_row(29, BOTTOM_COLS, "BACK: to SETUP  Up/Down L/R: scroll");
        return;
    }

    /* Row 0: CEL lamp (amber when on) and the adapter address. */
    printf("\x1b[1;1H%s CEL %s \x1b[0m", cel_on ? "\x1b[43;30m" : "\x1b[0m",
           cel_on ? "ON " : "off");
    snprintf(line, sizeof line, " %s:%u", cfg->adapter_host, (unsigned)cfg->adapter_port);
    printf("%-30.30s", line);

    if (ui->screen == SCREEN_HOME) {
        draw_list(home_buttons, HOME_BUTTON_COUNT, now, busy, 0, false);
        print_row(25, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        print_row(29, BOTTOM_COLS, "START: exit");
        return;
    }
    if (ui->screen == SCREEN_GUIDE) {
        print_row(1, BOTTOM_COLS, "Wi-Fi setup guide");
        for (int i = 0; i < WIFI_GUIDE_STEPS; i++) {
            char t[BOTTOM_COLS + 1];
            snprintf(t, sizeof t, "%s %d. %s", i == ui->page ? ">" : " ", i + 1,
                     wifi_guide_title(i));
            print_row(3 + i, BOTTOM_COLS, t);
        }
        char check[GUIDE_CHECK_LINES][40];
        LightLock_Lock(&shared.lock);
        memcpy(check, shared.guide_check, sizeof check);
        bool checking = shared.guide_checking;
        LightLock_Unlock(&shared.lock);
        print_row(10, BOTTOM_COLS, checking ? "Checking..." : check[0][0] ? "Last CHECK:"
                                                        : "Tap CHECK after step 5.");
        for (int i = 0; i < GUIDE_CHECK_LINES; i++) {
            print_row(12 + i, BOTTOM_COLS, checking ? "" : check[i]);
        }
        draw_list(guide_buttons, GUIDE_BUTTON_COUNT, now, busy, 0, false);
        snprintf(line, sizeof line, "%d/%d", ui->page + 1, WIFI_GUIDE_STEPS);
        printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
        return;
    }
    if (ui->screen == SCREEN_SETUP) {
        print_row(1, BOTTOM_COLS, "");
        for (int i = 0; i < SETUP_BUTTON_COUNT; i++) {
            const button_t *b = &setup_buttons[i];
            if (b->row == SETUP_BAR_ROW) {
                draw_button(b, flashing(b->action, now), false);
                continue;
            }
            char text[64];
            if (b->action == BTN_WIFI_SLOT) {
                LightLock_Lock(&shared.lock);
                int slot = shared.wifi_slot;
                LightLock_Unlock(&shared.lock);
                char value[12];
                if (slot == 0) {
                    snprintf(value, sizeof value, "Any");
                } else {
                    snprintf(value, sizeof value, "%d", slot);
                }
                snprintf(text, sizeof text, "%-29s%7s", b->label, value);
            } else if (b->action == BTN_MAKE_SCREEN) {
                char make[sizeof cfg->car_make];
                LightLock_Lock(&shared.lock);
                strcpy(make, cfg->car_make);
                LightLock_Unlock(&shared.lock);
                snprintf(text, sizeof text, "%-22s%14s", b->label, make[0] != '\0' ? make : "Auto");
            } else {
                snprintf(text, sizeof text, "%-36s", b->label);
            }
            bool active = flashing(b->action, now) || (b->action >= 0 && b->action == busy);
            const char *colors = active ? "\x1b[43;30m" : "\x1b[47;30m";
            printf("%s", colors);
            for (int r = 0; r < b->h; r++) {
                printf("\x1b[%d;1H %-37.37s ", b->row + r + 1, r == b->h / 2 ? text : "");
            }
            printf("\x1b[0m");
        }
        /* Between ADAPTER (cells 9-17) and WI-FI GUIDE (26-38), middle row. */
        printf("\x1b[28;20H%-7.7s", "L/R log");
        return;
    }
    if (ui->screen == SCREEN_MAKE) {
        LightLock_Lock(&shared.lock);
        int chosen = make_choice_of(cfg->car_make);
        LightLock_Unlock(&shared.lock);
        const button_t *list;
        int count = make_screen_buttons(ui->page, &list);
        int slot = chosen - ui->page * MAKE_PAGE_SLOTS;
        draw_list(list, count, now, busy,
                  slot >= 0 && slot < MAKE_PAGE_SLOTS ? BTN_MAKE_PICK - slot : 1, false);
        snprintf(line, sizeof line, "%d/%d", ui->page + 1, make_page_count());
        printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
        print_row(24, BOTTOM_COLS, "For cars that don't report a VIN.");
        printf("\x1b[28;24H%-15.15s", "Auto: from VIN");
        return;
    }

    if (ui->screen == SCREEN_LIVE) {
        uint8_t choices[OBD_LIVE_MAX];
        int nc = live_choices_now(choices);
        uint8_t pids[CONFIG_LIVE_PIDS_MAX];
        char msg[sizeof shared.live_msg];
        LightLock_Lock(&shared.lock);
        int np = cfg->live_pid_count;
        memcpy(pids, cfg->live_pids, sizeof pids);
        bool on = shared.live_on;
        memcpy(msg, shared.live_msg, sizeof msg);
        LightLock_Unlock(&shared.lock);

        print_row(1, BOTTOM_COLS, "Tap a PID to poll it; fewer = faster");
        clear_rows(2, 24);   /* the page's toggles change */
        for (int slot = 0; slot < LIVE_PAGE_SLOTS; slot++) {
            int k = ui->page * LIVE_PAGE_SLOTS + slot;
            if (k >= nc) {
                break;
            }
            button_t b = live_slot(slot);
            b.label = obd_pid_info(choices[k])->label;
            draw_button_ex(&b, flashing(b.action, now), false,
                           live_pid_chosen(pids, np, choices[k]));
        }
        if (nc == 0) {
            print_row(12, BOTTOM_COLS, msg[0] != '\0' ? msg
                                       : on ? "Looking for the car..."
                                            : "Tap START with the engine running.");
        }
        for (int i = 0; i < LIVE_BUTTON_COUNT; i++) {
            button_t b = live_buttons[i];
            if (b.action == BTN_LIVE_TOGGLE) {
                b.label = on ? "STOP" : "START";
            }
            draw_button_ex(&b, flashing(b.action, now), false,
                           b.action == BTN_LIVE_TOGGLE && on);
        }
        snprintf(line, sizeof line, "%d/%d", ui->page + 1,
                 nc == 0 ? 1 : (nc + LIVE_PAGE_SLOTS - 1) / LIVE_PAGE_SLOTS);
        printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
        return;
    }

    /* DIAG */
    if (ui->permanent_hidden) {
        for (int r = 1; r < 6; r++) {
            printf("\x1b[%d;21H%-9s", r + 1, "");
        }
    }
    draw_list(diag_buttons, DIAG_BUTTON_COUNT, now, busy, BTN_TAB_STORED - (int)ui->tab,
              ui->permanent_hidden);
    int n = diag_item_count(ui->tab);
    for (int slot = 0; slot < LIST_PAGE_ROWS; slot++) {
        int k = ui->page * LIST_PAGE_ROWS + slot;
        if (k < n) {
            draw_list_row(slot, items[k].row, k == ui->selected);
        } else {
            int top = LIST_FIRST_ROW + slot * LIST_ROW_PITCH;
            for (int r = 0; r < LIST_ROW_H; r++) {
                print_row(top + r, BOTTOM_COLS, "");
            }
            if (k == 0) {
                LightLock_Lock(&shared.lock);
                const char *e = diag_empty_text(&shared.diag, ui->tab);
                LightLock_Unlock(&shared.lock);
                print_row(top + LIST_ROW_H / 2, BOTTOM_COLS, scanning ? "  Please wait..." : e);
            }
        }
    }
    snprintf(line, sizeof line, "%d/%d", ui->page + 1, page_count(n));
    printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
    char msg[sizeof shared.diag_msg];
    LightLock_Lock(&shared.lock);
    memcpy(msg, shared.diag_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);
    print_row(6, BOTTOM_COLS, ui->notice != NULL ? ui->notice : msg);
}

static int button_at(const button_t *list, int count, int px, int py)
{
    int col = px / 8;
    int row = py / 8;
    for (int i = 0; i < count; i++) {
        const button_t *b = &list[i];
        if (col >= b->col && col < b->col + b->w && row >= b->row && row < b->row + b->h) {
            return i;
        }
    }
    return -1;
}

static int clamp_scroll(int scroll)
{
    LightLock_Lock(&shared.lock);
    int max = textlog_count(&shared.log) - LOG_ROWS;
    LightLock_Unlock(&shared.lock);
    if (max < 0) {
        max = 0;
    }
    return scroll < 0 ? 0 : scroll > max ? max : scroll;
}

/* ---------- startup ---------- */

static void open_log_file(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(CONFIG_DIR, 0777);
    log_file = fopen(LOG_PATH, "a");
    if (log_file != NULL) {
        fseek(log_file, 0, SEEK_END);
        session_start = ftell(log_file);
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        char when[32] = "unknown time";
        if (tm != NULL) {
            strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tm);
        }
        fprintf(log_file, "\n=== GaugeBoy %s session, %s ===\n", GB_VERSION_TEXT, when);
        fflush(log_file);
    }
}

static void load_config(spike_t *spike, gb_config_t *cfg)
{
    int warnings = 0;
    if (config_load(cfg, CONFIG_PATH, &warnings)) {
        spike_logf(spike, "Loaded %s", CONFIG_PATH);
        if (warnings > 0) {
            spike_logf(spike, "! %d bad line(s) in config.ini; defaults used for them", warnings);
        }
    } else if (config_write_default(CONFIG_PATH)) {
        spike_logf(spike, "Created %s with defaults", CONFIG_PATH);
    } else {
        spike_logf(spike, "! Could not read or create %s; using defaults", CONFIG_PATH);
    }
}

void spike_ui_run(void)
{
    static tcp_transport_t transport;
    static elm_t elm;
    static spike_t spike;
    gb_config_t cfg;

    consoleInit(GFX_TOP, &top_console);
    consoleInit(GFX_BOTTOM, &bottom_console);

    LightLock_Init(&shared.lock);
    LightEvent_Init(&shared.wake, RESET_ONESHOT);
    textlog_init(&shared.log);
    shared.busy = -1;
    shared.quit = false;

    open_log_file();
    spike_init(&spike, &transport, &elm, log_sink, NULL);
    spike_logf(&spike, "GaugeBoy %s", GB_VERSION_TEXT);
    home_gfx = home_draw_init();
    if (!home_gfx) {
        spike_logf(&spike, "! HOME graphics could not start (fonts in romfs); using text HOME");
    }
    if (log_file == NULL) {
        spike_logf(&spike, "! Could not open %s; log is on screen only", LOG_PATH);
    }

    load_config(&spike, &cfg);
    app_cfg = &cfg;
    shared.wifi_slot = cfg.wifi_slot;
    net_set_wifi_slot(cfg.wifi_slot);
    tcp_transport_init(&transport, cfg.adapter_host, cfg.adapter_port);

    bool net_ok = net_init();
    if (!net_ok) {
        spike_logf(&spike, "! Network service did not start (is Wi-Fi on?)");
    }
    if (net_ok && !ct_server_start(conntest_log, &spike)) {
        spike_logf(&spike, "! Could not start the connection-test answer thread");
    }
    spike.net_up = net_bring_up;
    spike.net_info = net_describe;
    {
        char info[160];
        net_describe(info, sizeof info);
        spike_logf(&spike, "Network: %s", info);

        char names[CONFIG_WIFI_SLOTS][40];
        for (int i = 0; i < CONFIG_WIFI_SLOTS; i++) {
            net_slot_name(i + 1, names[i], sizeof names[i]);
        }
        char using[16];
        if (cfg.wifi_slot == 0) {
            snprintf(using, sizeof using, "any");
        } else {
            snprintf(using, sizeof using, "slot %u", (unsigned)cfg.wifi_slot);
        }
        spike_logf(&spike, "Wi-Fi slots: 1 \"%s\", 2 \"%s\", 3 \"%s\"; using %s (tap SLOT)",
                   names[0], names[1], names[2], using);
        /* Read-only check of each saved slot's IP settings (gauge-boy-7v5.30). */
        for (int i = 0; i < CONFIG_WIFI_SLOTS; i++) {
            char ipset[200];
            if (strcmp(names[i], "(not set up)") != 0) {
                net_slot_ip_settings(i + 1, ipset, sizeof ipset);
                spike_logf(&spike, "Slot %d: %s", i + 1, ipset);
            }
        }
    }
    spike_logf(&spike, "Adapter %s:%u. Join its Wi-Fi in System Settings,",
               cfg.adapter_host, (unsigned)cfg.adapter_port);
    spike_logf(&spike, "then DIAG > SCAN (or SETUP > ADAPTER TEST).");
    shared.diag.make = -1;

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    int worker_prio = prio + 1 > 0x3F ? 0x3F : (int)prio + 1;
    Thread worker = threadCreate(worker_main, &spike, WORKER_STACK, worker_prio, -2, false);
    if (worker == NULL) {
        spike_logf(&spike, "! Could not start the worker thread; buttons will not work");
    }

    ui_state_t ui = { SCREEN_HOME, DIAG_TAB_STORED, -1, 0, NULL, false, false, -1, SCREEN_SETUP,
                      false };
    uint64_t notice_until = 0;
    int scroll = 0;                /* log (SETUP, TEST) */
    unsigned drawn_version = ~0u;
    unsigned drawn_diag_version = ~0u;
    unsigned drawn_live_version = ~0u;
    unsigned drawn_live_ui_version = ~0u;
    uint64_t live_drawn_at = 0;
    int drawn_scroll = -1;
    int drawn_busy = -2;
    bool drawn_connected = false;
    bool drawn_confirm = false;
    bool top_dirty = true;
    bool bottom_dirty = true;
    bool bottom_full = true;
    bool close_app = false;

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        uint64_t now = tp_now_ms();

        if (down & KEY_START) {
            break;
        }
        LightLock_Lock(&shared.lock);
        bool confirming = shared.confirm;
        bool perm_hidden = shared.diag.permanent_unsupported;
        LightLock_Unlock(&shared.lock);
        if (perm_hidden != ui.permanent_hidden) {
            ui.permanent_hidden = perm_hidden;
            if (perm_hidden && ui.tab == DIAG_TAB_PERMANENT) {
                ui.tab = DIAG_TAB_STORED;
            }
            bottom_dirty = bottom_full = top_dirty = true;
        }

        int action = 0;
        bool tapped = false;
        if ((down & KEY_TOUCH) && ui.clear_dialog) {
            touchPosition tp;
            hidTouchRead(&tp);
            int i = button_at(clear_buttons, CLEAR_BUTTON_COUNT, tp.px, tp.py);
            if (i >= 0) {
                if (clear_buttons[i].action == BTN_CLEAR_YES && worker != NULL) {
                    enqueue(BTN_CLEAR_CODES);
                }
                ui.clear_dialog = false;
                bottom_full = true;
            }
        } else if ((down & KEY_TOUCH) && confirming) {
            touchPosition tp;
            hidTouchRead(&tp);
            int i = button_at(confirm_buttons, CONFIRM_BUTTON_COUNT, tp.px, tp.py);
            if (i >= 0 && confirm_buttons[i].action == BTN_SETUP_OK) {
                LightLock_Lock(&shared.lock);
                shared.confirm = false;
                LightLock_Unlock(&shared.lock);
            }
        } else if (down & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            const button_t *list = buttons;
            int count = BUTTON_COUNT;
            switch (ui.screen) {
            case SCREEN_HOME:  list = home_buttons;  count = HOME_BUTTON_COUNT;  break;
            case SCREEN_DIAG:  list = diag_buttons;  count = DIAG_BUTTON_COUNT;  break;
            case SCREEN_SETUP: list = setup_buttons; count = SETUP_BUTTON_COUNT; break;
            case SCREEN_GUIDE: list = guide_buttons; count = GUIDE_BUTTON_COUNT; break;
            case SCREEN_MAKE:  count = make_screen_buttons(ui.page, &list);   break;
            case SCREEN_LIVE:  list = live_buttons;  count = LIVE_BUTTON_COUNT;  break;
            case SCREEN_TEST:  break;
            }
            int i = ui.screen == SCREEN_HOME && home_gfx ? -1 : button_at(list, count, tp.px, tp.py);
            int item = ui.screen == SCREEN_HOME && home_gfx ? home_hit(tp.px, tp.py) : -1;
            if (item >= 0) {
                action = home_item_action(item);
                if (item != HOME_CLOSE) {
                    ui.home_selected = item;
                }
                tapped = true;
            } else if (i >= 0 && !(ui.permanent_hidden && list[i].action == BTN_TAB_PERM)) {
                action = list[i].action;
                tapped = true;
            } else if (ui.screen == SCREEN_LIVE) {
                for (int slot = 0; slot < LIVE_PAGE_SLOTS; slot++) {
                    button_t b = live_slot(slot);
                    if (button_at(&b, 1, tp.px, tp.py) == 0) {
                        action = b.action;
                        tapped = true;
                    }
                }
            } else if (ui.screen == SCREEN_DIAG) {
                /* A list row? */
                int row = tp.py / 8;
                for (int slot = 0; slot < LIST_PAGE_ROWS; slot++) {
                    int top = LIST_FIRST_ROW + slot * LIST_ROW_PITCH;
                    if (row >= top && row < top + LIST_ROW_H) {
                        action = BTN_LIST_ROW - slot;
                        tapped = true;
                    }
                }
            }
        }

        /* Optional physical shortcuts: B back, X scan, D-pad / L R. */
        if (ui.clear_dialog && (down & KEY_B)) {
            ui.clear_dialog = false;   /* B cancels; there is no button shortcut to clear */
            bottom_full = true;
        } else if (!confirming && !tapped && !ui.clear_dialog) {
            if ((down & KEY_B) && ui.screen != SCREEN_HOME) { action = BTN_BACK; tapped = true; }
            else if ((down & KEY_X) && ui.screen == SCREEN_DIAG) { action = BTN_DIAG_SCAN; tapped = true; }
            else if ((down & KEY_L) && (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_MAKE || ui.screen == SCREEN_LIVE)) { action = BTN_PAGE_PREV; tapped = true; }
            else if ((down & KEY_R) && (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_MAKE || ui.screen == SCREEN_LIVE)) { action = BTN_PAGE_NEXT; tapped = true; }
        }

        if (tapped) {
            flash(action, now);
            bottom_dirty = true;
            int n = ui.screen == SCREEN_DIAG ? diag_item_count(ui.tab) : 0;
            switch (action) {
            case BTN_SCROLL_OLDER:
                scroll = clamp_scroll(scroll + LOG_ROWS / 2);
                break;
            case BTN_SCROLL_NEWER:
                scroll = clamp_scroll(scroll - LOG_ROWS / 2);
                break;
            case BTN_HOME_CLOSE:
                close_app = true;
                break;
            case BTN_HOME_CEL:
            case BTN_MENU_DIAG:
                ui.screen = SCREEN_DIAG;
                bottom_full = top_dirty = true;
                break;
            case BTN_MENU_SETUP:
                ui.screen = SCREEN_SETUP;
                bottom_full = top_dirty = true;
                break;
            case BTN_MENU_LIVE:
                ui.screen = SCREEN_LIVE;
                ui.page = 0;
                LightLock_Lock(&shared.lock);
                shared.live_on = true;
                shared.live_gen++;
                shared.live_msg[0] = '\0';
                shared.live_ui_version++;
                LightLock_Unlock(&shared.lock);
                LightEvent_Signal(&shared.wake);
                bottom_full = top_dirty = true;
                break;
            case BTN_LIVE_TOGGLE:
                LightLock_Lock(&shared.lock);
                shared.live_on = !shared.live_on;
                if (shared.live_on) {
                    shared.live_gen++;
                    shared.live_msg[0] = '\0';
                }
                shared.live_ui_version++;
                LightLock_Unlock(&shared.lock);
                LightEvent_Signal(&shared.wake);
                top_dirty = true;
                break;
            case BTN_MENU_TRIM:
            case BTN_MENU_FUN:
            case BTN_MENU_LOG:
                ui.notice = action == BTN_MENU_TRIM  ? "TRIM: fuel trims come in version 0.2"
                          : action == BTN_MENU_FUN   ? "FUN: fun modes come in version 0.6"
                          :                            "LOG: logging comes in version 0.4";
                notice_until = now + 3000;
                break;
            case BTN_TAB_STORED:
            case BTN_TAB_PENDING:
            case BTN_TAB_PERM:
            case BTN_TAB_READY:
                ui.tab = (diag_tab_t)(BTN_TAB_STORED - action);
                ui.page = 0;
                ui.selected = -1;
                top_dirty = true;
                break;
            case BTN_PAGE_PREV:
                if (ui.page > 0) { ui.page--; ui.selected = -1; top_dirty = true; }
                bottom_full |= ui.screen == SCREEN_MAKE;   /* the last page is shorter */
                break;
            case BTN_PAGE_NEXT: {
                int pages = ui.screen == SCREEN_GUIDE ? WIFI_GUIDE_STEPS
                          : ui.screen == SCREEN_MAKE  ? make_page_count()
                          : ui.screen == SCREEN_LIVE  ? live_page_count()
                          :                             page_count(n);
                if (ui.page + 1 < pages) { ui.page++; ui.selected = -1; top_dirty = true; }
                bottom_full |= ui.screen == SCREEN_MAKE;
                break;
            }
            case BTN_GUIDE:
            case BTN_HOME_WIFI:
                ui.guide_back = action == BTN_GUIDE ? SCREEN_SETUP : SCREEN_HOME;
                ui.screen = SCREEN_GUIDE;
                ui.page = 0;
                bottom_full = top_dirty = true;
                break;
            case BTN_TEST_SCREEN:
                ui.screen = SCREEN_TEST;
                bottom_full = top_dirty = true;
                break;
            case BTN_MAKE_SCREEN:
                LightLock_Lock(&shared.lock);
                ui.page = make_choice_of(cfg.car_make) / MAKE_PAGE_SLOTS;
                LightLock_Unlock(&shared.lock);
                ui.screen = SCREEN_MAKE;
                bottom_full = top_dirty = true;
                break;
            case BTN_BACK:
                if (ui.screen == SCREEN_LIVE) {
                    LightLock_Lock(&shared.lock);
                    shared.live_on = false;
                    shared.live_ui_version++;
                    LightLock_Unlock(&shared.lock);
                    if (ui.live_dirty && worker != NULL) {
                        enqueue(BTN_LIVE_SAVE);
                    }
                    ui.live_dirty = false;
                    ui.page = 0;
                }
                if (ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_DIAG ||
                    ui.screen == SCREEN_MAKE) {
                    ui.page = 0;
                }
                ui.screen = ui.screen == SCREEN_GUIDE ? ui.guide_back
                          : (ui.screen == SCREEN_TEST || ui.screen == SCREEN_MAKE) ? SCREEN_SETUP
                          : SCREEN_HOME;
                bottom_full = top_dirty = true;
                break;
            case BTN_CLEAR_ASK: {
                LightLock_Lock(&shared.lock);
                bool scanned = shared.diag.link.protocol != 0 && !shared.scanning;
                LightLock_Unlock(&shared.lock);
                if (scanned) {
                    ui.clear_dialog = true;
                    bottom_full = true;
                } else {
                    ui.notice = "CLEAR: tap SCAN first";
                    notice_until = now + 3000;
                }
                break;
            }
            case BTN_DIAG_SCAN:
                ui.page = 0;
                ui.selected = -1;
                if (worker != NULL) {
                    enqueue(action);
                }
                break;
            default:
                if (ui.screen == SCREEN_LIVE && action <= BTN_LIVE_PICK &&
                    action > BTN_LIVE_PICK - LIVE_PAGE_SLOTS) {
                    uint8_t choices[OBD_LIVE_MAX];
                    int nc = live_choices_now(choices);
                    int k = ui.page * LIVE_PAGE_SLOTS + (BTN_LIVE_PICK - action);
                    if (k < nc) {
                        LightLock_Lock(&shared.lock);
                        cfg.live_pid_count = (uint8_t)live_toggle(cfg.live_pids, cfg.live_pid_count,
                                                                  CONFIG_LIVE_PIDS_MAX, choices[k]);
                        shared.live_gen++;
                        LightLock_Unlock(&shared.lock);
                        ui.live_dirty = true;
                        LightEvent_Signal(&shared.wake);
                    }
                } else if (ui.screen == SCREEN_MAKE && action <= BTN_MAKE_PICK &&
                    action > BTN_MAKE_PICK - MAKE_PAGE_SLOTS) {
                    int k = ui.page * MAKE_PAGE_SLOTS + (BTN_MAKE_PICK - action);
                    if (k < make_choice_count()) {
                        LightLock_Lock(&shared.lock);
                        snprintf(cfg.car_make, sizeof cfg.car_make, "%s",
                                 k == 0 ? "" : make_choice_name(k));
                        LightLock_Unlock(&shared.lock);
                        if (worker != NULL) {
                            enqueue(BTN_SET_MAKE);
                        }
                        ui.screen = SCREEN_SETUP;
                        ui.page = 0;
                        bottom_full = top_dirty = true;
                    }
                } else if (action <= BTN_LIST_ROW && action > BTN_LIST_ROW - LIST_PAGE_ROWS) {
                    int k = ui.page * LIST_PAGE_ROWS + (BTN_LIST_ROW - action);
                    if (k < n) {
                        ui.selected = k;
                        top_dirty = true;
                    }
                } else if (action >= 0 && worker != NULL) {
                    enqueue(action);
                }
                break;
            }
        }
        if (close_app) {
            break;   /* the HOME bar: same as START */
        }
        if (ui.notice != NULL && now >= notice_until) {
            ui.notice = NULL;
            bottom_dirty = true;
        }

        /* Log scrolling on SETUP / TEST. */
        if (ui.screen == SCREEN_SETUP || ui.screen == SCREEN_TEST) {
            int step = 0;
            if (down & KEY_DUP)   step = 1;
            if (down & KEY_DDOWN) step = -1;
            if (down & KEY_L)     step = LOG_ROWS;
            if (down & KEY_R)     step = -LOG_ROWS;
            if (step != 0) {
                scroll = clamp_scroll(scroll + step);
            }
        }

        LightLock_Lock(&shared.lock);
        unsigned version = shared.log.version;
        unsigned diag_version = shared.diag_version;
        unsigned live_version = shared.live_version;
        unsigned live_ui_version = shared.live_ui_version;
        int busy = shared.busy;
        bool connected = shared.connected;
        bool confirm_now = shared.confirm;
        LightLock_Unlock(&shared.lock);
        if (confirm_now != drawn_confirm) {
            drawn_confirm = confirm_now;
            bottom_dirty = true;
        }
        if (live_ui_version != drawn_live_ui_version) {
            drawn_live_ui_version = live_ui_version;
            if (ui.screen == SCREEN_LIVE) {
                bottom_dirty = true;
            }
        }
        /* LIVE values change many times a second: redraw at most 10 times. */
        bool live_due = ui.screen == SCREEN_LIVE && live_version != drawn_live_version &&
                        now - live_drawn_at >= 100;

        bool shows_log = ui.screen == SCREEN_SETUP || ui.screen == SCREEN_TEST ||
                         ui.screen == SCREEN_MAKE;
        if (top_dirty || busy != drawn_busy || connected != drawn_connected || live_due ||
            (shows_log ? (version != drawn_version || scroll != drawn_scroll)
                       : ui.screen != SCREEN_LIVE && diag_version != drawn_diag_version)) {
            if (ui.screen == SCREEN_LIVE) {
                draw_live_top(busy, connected);
                drawn_live_version = live_version;
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_GUIDE) {
                draw_guide_top(ui.page, cfg.adapter_host, busy, connected);
            } else if (shows_log) {
                draw_top(scroll, busy, connected);
            } else {
                draw_diag_top(ui.screen == SCREEN_HOME, ui.tab, ui.selected, busy, connected);
            }
            bottom_dirty |= busy != drawn_busy || diag_version != drawn_diag_version ||
                            connected != drawn_connected;
            drawn_version = version;
            drawn_diag_version = diag_version;
            drawn_scroll = scroll;
            drawn_busy = busy;
            drawn_connected = connected;
            top_dirty = false;
        }
        if (flashes_expired(now)) {
            bottom_dirty = true;
        }
        if (bottom_dirty || bottom_full) {
            draw_bottom(&cfg, &ui, now, busy, bottom_full);
            bottom_dirty = bottom_full = false;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    /* Shutdown: stop the worker (waits for a running command to time out). */
    shared.quit = true;
    LightEvent_Signal(&shared.wake);
    if (worker != NULL) {
        threadJoin(worker, 15ULL * 1000 * 1000 * 1000);
        threadFree(worker);
    }
    tcp_transport_close(&transport);
    ct_server_stop();
    if (net_ok) {
        net_exit();
    }
    home_draw_exit();
    spike_logf(&spike, "Session end");
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}
