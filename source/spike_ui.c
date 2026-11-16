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
#include "data/live_views.h"
#include "data/trim_view.h"
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
#define BTN_SET_VEHICLE  109   /* worker: save make, model, year; apply to DIAG */
#define BTN_SET_UNITS    110   /* worker: save the units */
#define BTN_IDENTIFY     111   /* worker (not a button): find the car after a connect */
#define IDENTIFY_RETRY_MS 20000u
#define BTN_LIVE_TOGGLE  (-33) /* UI: LIVE START / STOP */
#define BTN_LIVE_PICK    (-80) /* UI: Custom PID toggle (-80 - slot on the page) */
#define BTN_LIVE_EDIT    (-34) /* UI: LIVE Custom view -> PID toggles */
#define BTN_DIAG_TRIM    (-35) /* UI: DIAG -> TRIM (lean / rich codes stored) */
#define BTN_LIVE_VIEW    (-90) /* UI: LIVE view list (-90 - view) */
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
#define BTN_SETUP_TESTS   (-110) /* UI: SETUP menu -> its four pages */
#define BTN_SETUP_WIFI    (-111)
#define BTN_SETUP_VEHICLE (-112)
#define BTN_SETUP_OTHER   (-113)
#define BTN_MODEL_EDIT    (-114) /* UI: Vehicle: type the model (keyboard) */
#define BTN_YEAR_PREV     (-115) /* UI: Vehicle: model year down / up */
#define BTN_YEAR_NEXT     (-116)
#define BTN_UNITS         (-117) /* UI: Other: US / metric */
#define BTN_ABOUT         (-118) /* UI: Other -> About */
#define BTN_NONE          (-119) /* a bar that only shows a value */

/* Bottom-screen buttons in 8x8 console cells: 12-13x6 cells = 96-104x48 px.
 * The third row has the Wi-Fi slot buttons, the last one the log ones. */
typedef struct {
    const char *label;
    int action;          /* spike_action_t, or BTN_SCROLL_* */
    int col, row, w, h;
} button_t;

/* SETUP > TESTS: adapter and car tests, the log's buttons. */
static const button_t buttons[] = {
    { NULL,         SPIKE_ATZ,        0,  2, 12, 6 },
    { NULL,         SPIKE_SEARCH,     13, 2, 13, 6 },
    { NULL,         SPIKE_PROTOCOL,   27, 2, 12, 6 },
    { NULL,         SPIKE_VOLTAGE,    0,  9, 12, 6 },
    { NULL,         SPIKE_VIN,        13, 9, 13, 6 },
    { NULL,         SPIKE_SPEED,      27, 9, 12, 6 },
    { NULL,         SPIKE_LIVE_TEST,  0, 16, 12, 6 },
    { "SAVE LOG",   BTN_SAVE_LOG,     13, 16, 13, 6 },
    { "BACK",       BTN_BACK,         0, 23, 12, 6 },
    { "OLDER ^",    BTN_SCROLL_OLDER, 13, 23, 13, 6 },
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

/* The same with a TRIM button, when a lean or rich code is stored or
 * pending: the bottom row narrows (every button still 5 cells, 40 px). */
#define DIAG_PAGE_COL_TRIM 13   /* "n/m" between < and > (1-based column) */
static const button_t diag_trim_buttons[] = {
    { "STORED",  BTN_TAB_STORED,  0,  1, 9, 5 },
    { "PENDING", BTN_TAB_PENDING, 10, 1, 9, 5 },
    { "PERM",    BTN_TAB_PERM,    20, 1, 9, 5 },
    { "READY",   BTN_TAB_READY,   30, 1, 9, 5 },
    { "BACK",    BTN_BACK,        0,  25, 6, 5 },
    { "<",       BTN_PAGE_PREV,   7,  25, 5, 5 },
    { ">",       BTN_PAGE_NEXT,   15, 25, 5, 5 },
    { "TRIM",    BTN_DIAG_TRIM,   21, 25, 5, 5 },
    { "CLEAR",   BTN_CLEAR_ASK,   27, 25, 6, 5 },
    { "SCAN",    BTN_DIAG_SCAN,   34, 25, 5, 5 },
};
#define DIAG_TRIM_BUTTON_COUNT ((int)(sizeof diag_trim_buttons / sizeof diag_trim_buttons[0]))

/* SETUP: a menu of four big buttons, each a page; BACK -> HOME. */
static const button_t setup_buttons[] = {
    { "TESTS",   BTN_SETUP_TESTS,   0,  3, 19, 9 },
    { "WI-FI",   BTN_SETUP_WIFI,    20, 3, 19, 9 },
    { "VEHICLE", BTN_SETUP_VEHICLE, 0, 14, 19, 9 },
    { "OTHER",   BTN_SETUP_OTHER,   20, 14, 19, 9 },
    { "BACK",    BTN_BACK,          0, 25, 8, 5 },
};
#define SETUP_BUTTON_COUNT ((int)(sizeof setup_buttons / sizeof setup_buttons[0]))

/* SETUP pages: full-width bars (label left, value right) like DIAG's list,
 * BACK bottom-left (-> the SETUP menu). */
#define BAR_ROWS_LAST 24
static const button_t wifi_buttons[] = {
    { "Connect to the adapter",      SPIKE_CONNECT,  0,  2, 39, 5 },
    { "Wi-Fi slot",                  BTN_WIFI_SLOT,  0,  8, 39, 5 },
    { "Check the slot's IP and DNS", BTN_SETUP_SLOT, 0, 14, 39, 5 },
    { "Wi-Fi setup guide",           BTN_GUIDE,      0, 20, 39, 5 },
    { "BACK",                        BTN_BACK,       0, 25, 8, 5 },
};
#define WIFI_BUTTON_COUNT ((int)(sizeof wifi_buttons / sizeof wifi_buttons[0]))

static const button_t vehicle_buttons[] = {
    { "Make",              BTN_MAKE_SCREEN, 0,  2, 39, 5 },
    { "Model",             BTN_MODEL_EDIT,  0,  8, 39, 5 },
    { "Year",              BTN_NONE,        0, 14, 27, 5 },
    { "<",                 BTN_YEAR_PREV,   28, 14, 5, 5 },
    { ">",                 BTN_YEAR_NEXT,   34, 14, 5, 5 },
    { "VIN (tap to read)", BTN_DIAG_SCAN,   0, 20, 39, 5 },
    { "BACK",              BTN_BACK,        0, 25, 8, 5 },
};
#define VEHICLE_BUTTON_COUNT ((int)(sizeof vehicle_buttons / sizeof vehicle_buttons[0]))

static const button_t other_buttons[] = {
    { "Units",          BTN_UNITS, 0, 2, 39, 5 },
    { "About GaugeBoy", BTN_ABOUT, 0, 8, 39, 5 },
    { "BACK",           BTN_BACK,  0, 25, 8, 5 },
};
#define OTHER_BUTTON_COUNT ((int)(sizeof other_buttons / sizeof other_buttons[0]))

static const button_t about_buttons[] = {
    { "BACK", BTN_BACK, 0, 25, 8, 5 },
};
#define ABOUT_BUTTON_COUNT 1

typedef enum {
    SCREEN_HOME, SCREEN_DIAG, SCREEN_SETUP, SCREEN_TEST, SCREEN_GUIDE, SCREEN_MAKE,
    SCREEN_LIVE_MENU,          /* LIVE: the view list */
    SCREEN_LIVE,               /* LIVE: one view polling */
    SCREEN_LIVE_EDIT,          /* LIVE: the Custom view's PID toggles */
    SCREEN_TRIM,               /* fuel trims, both banks */
    SCREEN_SETUP_WIFI,         /* SETUP > WI-FI (SCREEN_TEST is SETUP > TESTS) */
    SCREEN_SETUP_VEHICLE,      /* SETUP > VEHICLE */
    SCREEN_SETUP_OTHER,        /* SETUP > OTHER */
    SCREEN_ABOUT               /* SETUP > OTHER > About */
} screen_t;

/* TRIM: BACK and START / STOP (the bars and hints are on the top screen). */
static const button_t trim_buttons[] = {
    { "BACK", BTN_BACK,        0,  25, 8, 5 },
    { NULL,   BTN_LIVE_TOGGLE, 26, 25, 13, 5 },   /* START / STOP */
};
#define TRIM_BUTTON_COUNT ((int)(sizeof trim_buttons / sizeof trim_buttons[0]))

/* LIVE view screen: BACK (to the list), < view >, START / STOP; EDIT in
 * the Custom view. */
static const button_t live_buttons[] = {
    { "BACK", BTN_BACK,        0,  25, 8, 5 },
    { "<",    BTN_PAGE_PREV,   9,  25, 5, 5 },
    { ">",    BTN_PAGE_NEXT,   17, 25, 5, 5 },
    { NULL,   BTN_LIVE_TOGGLE, 26, 25, 13, 5 },   /* START / STOP */
};
#define LIVE_BUTTON_COUNT ((int)(sizeof live_buttons / sizeof live_buttons[0]))
static const button_t live_edit_button = { "EDIT PIDS", BTN_LIVE_EDIT, 10, 14, 19, 5 };

/* Custom PID toggles (2 columns x 3 rows a page), then BACK < page >. */
#define LIVE_PAGE_SLOTS 6
static const button_t live_edit_buttons[] = {
    { "BACK", BTN_BACK,      0,  25, 8, 5 },
    { "<",    BTN_PAGE_PREV, 9,  25, 5, 5 },
    { ">",    BTN_PAGE_NEXT, 17, 25, 5, 5 },
};
#define LIVE_EDIT_BUTTON_COUNT ((int)(sizeof live_edit_buttons / sizeof live_edit_buttons[0]))

/* LIVE view list: one button per view (3 columns x 3 rows, 12-13 x 5
 * cells), then BACK. */
static button_t live_menu_buttons[LIVE_VIEW_COUNT + 1];
static int live_menu_count(void)
{
    static const int col[3] = { 0, 13, 27 };
    static const int width[3] = { 12, 13, 12 };
    for (int v = 0; v < LIVE_VIEW_COUNT; v++) {
        button_t b = { live_view_name(v), BTN_LIVE_VIEW - v, col[v % 3], 3 + (v / 3) * 7,
                       width[v % 3], 5 };
        live_menu_buttons[v] = b;
    }
    button_t back = { "BACK", BTN_BACK, 0, 25, 8, 5 };
    live_menu_buttons[LIVE_VIEW_COUNT] = back;
    return LIVE_VIEW_COUNT + 1;
}

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
    uint8_t live_want[OBD_LIVE_MAX];       /* the view's PIDs (the UI sets them) */
    uint32_t live_want_period[OBD_LIVE_MAX];
    int live_want_count;
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
/* SETUP > VEHICLE (worker): save make, model and year, apply them to DIAG. */
static void set_vehicle(spike_t *s)
{
    LightLock_Lock(&shared.lock);
    gb_config_t copy = *app_cfg;
    obd_diag_apply_make(&shared.diag, app_cfg->car_make);
    obd_diag_apply_vehicle(&shared.diag, app_cfg->car_model, app_cfg->car_year);
    bool vin_wins = copy.car_make[0] != '\0' && shared.diag.maker != NULL &&
                    !shared.diag.make_chosen;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);

    bool saved = config_save(&copy, CONFIG_PATH);
    char year[8] = "Auto";
    if (copy.car_year != 0) {
        snprintf(year, sizeof year, "%u", (unsigned)copy.car_year);
    }
    spike_logf(s, "Vehicle: make %s, model %s, year %s%s%s",
               copy.car_make[0] != '\0' ? copy.car_make : "Auto",
               copy.car_model[0] != '\0' ? copy.car_model : "Auto", year,
               vin_wins ? "; this car's VIN gives its make, so that is used" : "",
               saved ? "" : " (! not saved to config.ini)");
}

/* SETUP > OTHER (worker): save the units; the log uses them too. */
static void set_units(spike_t *s)
{
    LightLock_Lock(&shared.lock);
    gb_config_t copy = *app_cfg;
    LightLock_Unlock(&shared.lock);
    s->units = copy.metric ? OBD_UNITS_METRIC : OBD_UNITS_US;
    bool saved = config_save(&copy, CONFIG_PATH);
    spike_logf(s, "Units: %s%s", copy.metric ? "metric (km/h, C, kPa)" : "US (mph, F, psi, inHg)",
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
        spike_logf(s, "! SETUP SLOT: pick the adapter's slot first (SETUP > WI-FI > Wi-Fi slot)");
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
static unsigned identified_connects = ~0u;   /* connection whose car is known */
static uint64_t identify_next_ms;            /* next try while it isn't answering */
static bool identify_told;

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
        LightLock_Lock(&shared.lock);
        obd_diag_apply_make(&d, app_cfg->car_make);
        obd_diag_apply_vehicle(&d, app_cfg->car_model, app_cfg->car_year);
        LightLock_Unlock(&shared.lock);
        if (reset && obd_link_adapter_ready(d.result)) {
            reset_at_connect = s->transport->connects;
        }
        if (d.result == OBD_LINK_OK) {
            last_link = d.link;
            last_link_connects = s->transport->connects;
            identified_connects = s->transport->connects;   /* the scan read the VIN */
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
            if (d.link.failed_command != NULL) {
                spike_logf(s, "! Scan: %s (%s: %s)", obd_link_result_str(d.result),
                           d.link.failed_command, elm_status_str(d.link.elm_status));
            } else {
                spike_logf(s, "! Scan: %s", obd_link_result_str(d.result));
            }
        }
    } else {
        memset(&d, 0, sizeof d);
        d.make = -1;
        d.result = OBD_LINK_NO_ADAPTER;
        spike_logf(s, "! Scan: can't reach the adapter (see SETUP > WI-FI)");
    }

    LightLock_Lock(&shared.lock);
    shared.diag = d;
    shared.scanning = false;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
}

/* Find the car (worker, idle, once per adapter connection): protocol and
 * VIN, decoded into the DIAG state so HOME, DIAG and SETUP > VEHICLE show
 * it. Retries every IDENTIFY_RETRY_MS while the car doesn't answer. */
static void identify_car(spike_t *s)
{
    unsigned connects = s->transport->connects;
    static obd_link_t link;
    obd_link_result_t r = OBD_LINK_OK;
    if (last_link.protocol != 0 && last_link_connects == connects) {
        link = last_link;   /* LIVE or a scan already found it */
    } else {
        bool reset = connects != reset_at_connect;
        r = obd_link_open_traced(s->elm, reset, &link, spike_trace_command, s);
        if (r == OBD_LINK_LOST) {
            tcp_transport_close(s->transport);
            return;
        }
        if (reset && obd_link_adapter_ready(r)) {
            reset_at_connect = connects;
        }
        if (r == OBD_LINK_OK) {
            last_link = link;
            last_link_connects = connects;
        }
    }
    if (r != OBD_LINK_OK) {
        if (!identify_told) {
            spike_logf(s, "Car: not answering yet (%s); trying again every %u s",
                       obd_link_result_str(r), IDENTIFY_RETRY_MS / 1000u);
            identify_told = true;
        }
        identify_next_ms = tp_now_ms() + IDENTIFY_RETRY_MS;
        return;
    }

    static obd_diag_t v;
    memset(&v, 0, sizeof v);
    v.make = -1;
    v.link = link;
    v.result = OBD_LINK_OK;
    v.vin_only = true;
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    elm_status_t st = obd_diag_read_vin(s->elm, tm != NULL ? tm->tm_year + 1900 : 0, &v,
                                        spike_trace_command, s);
    if (st == ELM_ERR_IO || st == ELM_ERR_TIMEOUT) {
        tcp_transport_close(s->transport);
        return;
    }
    identified_connects = connects;
    identify_told = false;

    LightLock_Lock(&shared.lock);
    const obd_diag_t *old = &shared.diag;
    bool same_car = old->link.protocol != 0 &&
                    (old->have_vin ? v.have_vin && strcmp(old->vin, v.vin) == 0 : !v.have_vin);
    if (!same_car) {
        shared.diag = v;   /* a new car: its codes need a SCAN */
        obd_diag_apply_make(&shared.diag, app_cfg->car_make);
        obd_diag_apply_vehicle(&shared.diag, app_cfg->car_model, app_cfg->car_year);
        shared.diag_version++;
    }
    obd_diag_t d = shared.diag;
    LightLock_Unlock(&shared.lock);

    const char *make = d.model != NULL && d.model_make != NULL ? d.model_make : d.maker;
    if (d.have_vin) {
        spike_logf(s, "= Car: %d %s%s%s (from the VIN)%s", d.model_year, make != NULL ? make : "",
                   d.model != NULL ? " " : "", d.model != NULL ? d.model : "",
                   same_car ? ", as scanned" : "");
    } else {
        spike_logf(s, "= Car: no VIN over OBD (%s); set it in SETUP > VEHICLE",
                   elm_protocol_name(link.protocol));
    }
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
                snprintf(out[n++], 40, "  (SETUP > WI-FI > Check the slot)");
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
    if (reset && obd_link_adapter_ready(r)) {
        reset_at_connect = connects;
    }
    if (r != OBD_LINK_OK) {
        if (link->failed_command != NULL) {
            spike_logf(s, "! LIVE: %s (%s: %s)", obd_link_result_str(r), link->failed_command,
                       elm_status_str(link->elm_status));
        } else {
            spike_logf(s, "! LIVE: %s", obd_link_result_str(r));
        }
        return LIVE_OPEN_STOP;
    }
    last_link = *link;
    last_link_connects = connects;
    return LIVE_OPEN_OK;
}

static bool lost_status(elm_status_t st)
{
    return st == ELM_ERR_IO || st == ELM_ERR_TIMEOUT;
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
    uint8_t pids[OBD_LIVE_MAX];
    uint32_t periods[OBD_LIVE_MAX];
    int n = shared.live_want_count;
    memcpy(pids, shared.live_want, sizeof pids);
    memcpy(periods, shared.live_want_period, sizeof periods);
    LightLock_Unlock(&shared.lock);

    if (!on) {
        if (ready) {
            if (lost_status(obd_live_restore_header(&lv, s->elm))) {
                tcp_transport_close(s->transport);
            }
            spike_live_summary(s, &lv);
        }
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
        if (ready) {
            obd_live_restore_header(&lv, s->elm);
            spike_live_summary(s, &lv);   /* the old PID set's rates */
        }
        int count = obd_live_init(&lv, &link, pids, n);
        for (int i = 0; i < n; i++) {
            if (periods[i] != LIVE_PERIOD_DEFAULT) {
                obd_live_set_period(&lv, pids[i], periods[i]);
            }
        }
        lv.use_count = true;
        lv.use_multi = true;   /* CAN cars: several PIDs a request */
        lv.use_engine_header = true;   /* CAN cars: the engine ECU only */
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

    bool was_polling = false;
    while (!shared.quit) {
        LightLock_Lock(&shared.lock);
        bool polling = shared.live_on;
        LightLock_Unlock(&shared.lock);
        if (was_polling && !polling) {
            live_poll(spike);   /* polling stopped: log the summary */
        }
        was_polling = polling;
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
            } else if (action == BTN_SET_MAKE || action == BTN_SET_VEHICLE) {
                set_vehicle(spike);
            } else if (action == BTN_SET_UNITS) {
                set_units(spike);
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
        } else if (tcp_transport_connected(spike->transport) &&
                   spike->transport->connects != identified_connects &&
                   tp_now_ms() >= identify_next_ms) {
            LightLock_Lock(&shared.lock);
            bool idle = shared.queue_count == 0;
            if (idle) {
                shared.busy = BTN_IDENTIFY;
            }
            LightLock_Unlock(&shared.lock);
            if (idle) {
                identify_car(spike);
                LightLock_Lock(&shared.lock);
                shared.busy = -1;
                shared.connected = tcp_transport_connected(spike->transport);
                LightLock_Unlock(&shared.lock);
            }
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
    print_row(1, TOP_COLS, scroll > 0 ? "-- scrolled back: NEWER or R to follow --"
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
    print_row(3, TOP_COLS, "");   /* LIVE writes a note here */
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
    bool live_dirty;           /* Custom PIDs or the view changed: save */
    int live_view;             /* live_view_id_t shown on SCREEN_LIVE */
    screen_t trim_back;        /* where BACK goes from TRIM (HOME or DIAG) */
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

/* DIAG offers TRIM when the last scan found a lean or rich code. */
static bool diag_trim_offered(void)
{
    LightLock_Lock(&shared.lock);
    bool offered = diag_has_trim_codes(&shared.diag);
    LightLock_Unlock(&shared.lock);
    return offered;
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

/* LIVE: show view v and (re)start polling its PIDs. Remembered in
 * config.ini (saved on BACK). */
/* Start polling these PIDs (call with shared.lock held). */
static void live_start_locked(const uint8_t *pids, const uint32_t *periods, int n)
{
    memcpy(shared.live_want, pids, (size_t)n);
    memcpy(shared.live_want_period, periods, (size_t)n * sizeof periods[0]);
    shared.live_want_count = n;
    shared.live_on = true;
    shared.live_gen++;
    shared.live_msg[0] = '\0';
    shared.live_ui_version++;
}

/* TRIM: the four trims fast enough to watch, RPM and speed for the hints. */
static void trim_start(void)
{
    static const uint8_t pids[] = { OBD_PID_STFT_B1, OBD_PID_STFT_B2, OBD_PID_LTFT_B1,
                                    OBD_PID_LTFT_B2, 0x0C, 0x0D };
    static const uint32_t periods[] = { 0, 0, 1000, 1000, 1000, 2000 };
    LightLock_Lock(&shared.lock);
    live_start_locked(pids, periods, (int)sizeof pids);
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

static void live_select_view(ui_state_t *ui, int v)
{
    uint8_t pids[OBD_LIVE_MAX];
    uint32_t periods[OBD_LIVE_MAX];
    LightLock_Lock(&shared.lock);
    int n = live_view_request(v, app_cfg->live_pids, app_cfg->live_pid_count, pids, periods,
                              OBD_LIVE_MAX);
    live_start_locked(pids, periods, n);
    if (strcmp(app_cfg->live_view, live_view_key(v)) != 0) {
        snprintf(app_cfg->live_view, sizeof app_cfg->live_view, "%s", live_view_key(v));
        ui->live_dirty = true;
    }
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
    ui->live_view = v;
}

/* One trim row: label, value, and the bar in its zone's color. */
static void trim_row(int row, const char *label, bool have, float pct)
{
    char value[16], bar[TRIM_BAR_WIDTH + 1];
    if (have) {
        snprintf(value, sizeof value, "%+.1f %%", (double)pct);
        trim_bar(pct, TRIM_BAR_WIDTH, bar);
    } else {
        snprintf(value, sizeof value, "-");
        trim_bar(0.0f, TRIM_BAR_WIDTH, bar);
    }
    trim_zone_t z = trim_zone(pct);
    const char *color = !have ? "\x1b[0m" : z == TRIM_ZONE_GREEN ? "\x1b[32m"
                      : z == TRIM_ZONE_AMBER ? "\x1b[33m" : "\x1b[31m";
    printf("\x1b[%d;1H%-14.14s%-9.9s%s%s\x1b[0m ", row + 1, label, value, color, bar);
}

/* TRIM top screen: both banks' short, long and total trims as bars, the
 * engine state and the hints. */
static void draw_trim_top(int busy, bool connected)
{
    static obd_live_t lv;
    char header[TOP_COLS + 1], title[TOP_COLS + 16], msg[sizeof shared.live_msg];
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have = shared.live_have;
    int protocol = shared.live_protocol;
    float rate = shared.live_rate;
    bool on = shared.live_on;
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);

    trim_state_t ts;
    if (have) {
        trim_state_from_live(&lv, &ts);
    } else {
        memset(&ts, 0, sizeof ts);
        ts.banks = 2;   /* until the car says otherwise */
    }
    spike_format_header(header, sizeof header, connected, busy);
    if (have && protocol != 0) {
        snprintf(title, sizeof title, "TRIM  %s, %.1f requests/s", elm_protocol_name(protocol),
                 (double)rate);
    } else {
        snprintf(title, sizeof title, "TRIM  Fuel trims");
    }
    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    print_row(2, TOP_COLS, title);
    print_row(3, TOP_COLS, "+ adds fuel (was lean)   - takes fuel (was rich)");
    print_row(4, TOP_COLS, "                       -25 %       0       +25 %");
    int row = 5;
    for (int b = 0; b < (ts.banks > 0 ? ts.banks : 1); b++) {
        const trim_bank_t *k = &ts.bank[b];
        float total = 0.0f;
        bool have_total = trim_total(k, &total);
        char label[16];
        snprintf(label, sizeof label, "Bank %d short", b + 1);
        trim_row(row++, label, k->have_short, k->short_term);
        trim_row(row++, "       long", k->have_long, k->long_term);
        trim_row(row++, "       total", have_total, total);
        print_row(row++, TOP_COLS, "");
    }
    char line[64];
    if (ts.have_rpm) {
        snprintf(line, sizeof line, "Engine %.0f rpm%s", (double)ts.rpm,
                 trim_at_idle(&ts) ? ", at idle" : ts.rpm < 400.0f ? ", not running" : "");
    } else {
        snprintf(line, sizeof line, "Engine: waiting for RPM");
    }
    print_row(row++, TOP_COLS, line);
    print_row(row++, TOP_COLS, "");
    const char *hints[TRIM_HINTS_MAX];
    int nh = have ? trim_hints(&ts, hints, TRIM_HINTS_MAX) : 0;
    static char wrapped[8][DIAG_VIEW_COLS + 1];
    for (int i = 0; i < nh && row < TOP_ROWS - 2; i++) {
        int w = diag_wrap(hints[i], TOP_COLS - 2, 2, wrapped, 8);
        for (int j = 0; j < w && row < TOP_ROWS - 2; j++) {
            char text[DIAG_VIEW_COLS + 8];   /* print_row cuts it to the width */
            snprintf(text, sizeof text, "%s%.*s", j == 0 ? "- " : "  ", DIAG_VIEW_COLS, wrapped[j]);
            print_row(row++, TOP_COLS, text);
        }
    }
    while (row < TOP_ROWS - 2) {
        print_row(row++, TOP_COLS, "");
    }
    print_row(TOP_ROWS - 2, TOP_COLS, nh > 0 ? "Hints, not diagnoses." : "");
    print_row(TOP_ROWS - 1, TOP_COLS, msg[0] != '\0' ? msg
                                      : on ? "Polling." : "Stopped. Tap START to poll.");
}

/* LIVE top screen: the view, the values, refresh rates and a status line. */
static void draw_live_top(int view, int busy, bool connected)
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

    int n = live_view_lines(have ? &lv : NULL, have ? protocol : 0, rate,
                            app_cfg->metric ? OBD_UNITS_METRIC : OBD_UNITS_US, TOP_COLS,
                            lines, LIVE_VIEW_MAX);
    spike_format_header(header, sizeof header, connected, busy);
    snprintf(title, sizeof title, "%s  %s", live_view_name(view), lines[0]);
    /* Under the title: what the view is for, or the refresh warning. */
    char note[TOP_COLS + 8];
    float fast = have ? live_fast_interval(&lv, rate) : 0.0f;
    if (live_view_warns(view) && have && lv.count > 0) {
        if (fast > 0.0f) {
            snprintf(note, sizeof note, "%d PIDs: fast ones every %.1f s. Fewer = faster.",
                     lv.count, (double)fast);
        } else {
            snprintf(note, sizeof note, "%d PIDs: fewer PIDs refresh faster.", lv.count);
        }
    } else {
        snprintf(note, sizeof note, "%s", live_view_purpose(view));
    }
    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    print_row(2, TOP_COLS, title);
    print_row(3, TOP_COLS, note);
    for (int i = 2; i < TOP_ROWS - 3; i++) {
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

/* A SETUP page bar: label left, value right, b->w cells wide. */
static void draw_bar(const button_t *b, const char *value, bool active)
{
    int inner = b->w - 2;
    char text[48];
    int vlen = (int)strlen(value);
    if (vlen > inner - 1) {
        vlen = inner - 1;
    }
    snprintf(text, sizeof text, "%-*.*s%*.*s", inner - vlen, inner - vlen, b->label, vlen, vlen,
             value);
    printf("%s", active ? "\x1b[43;30m" : "\x1b[47;30m");
    for (int r = 0; r < b->h; r++) {
        printf("\x1b[%d;%dH %-*.*s ", b->row + r + 1, b->col + 1, inner, inner,
               r == b->h / 2 ? text : "");
    }
    printf("\x1b[0m");
}

/* The value shown on a SETUP page bar. */
static void bar_value(int action, const gb_config_t *cfg, char *out, size_t len)
{
    LightLock_Lock(&shared.lock);
    const obd_diag_t *d = &shared.diag;
    out[0] = '\0';
    switch (action) {
    case SPIKE_CONNECT:
        snprintf(out, len, "%s", shared.connected ? "Connected" : "Not connected");
        break;
    case BTN_WIFI_SLOT:
        if (shared.wifi_slot == 0) {
            snprintf(out, len, "Any");
        } else {
            snprintf(out, len, "%d", shared.wifi_slot);
        }
        break;
    case BTN_SETUP_SLOT:
    case BTN_GUIDE:
    case BTN_ABOUT:
        snprintf(out, len, ">");
        break;
    case BTN_MAKE_SCREEN:
        if (d->maker != NULL && !d->make_chosen) {
            snprintf(out, len, "%s (VIN)", d->maker);
        } else {
            snprintf(out, len, "%s", cfg->car_make[0] != '\0' ? cfg->car_make : "Auto");
        }
        break;
    case BTN_MODEL_EDIT:
        if (d->model != NULL && !d->model_chosen) {
            snprintf(out, len, "%s (VIN)", d->model);
        } else {
            snprintf(out, len, "%s", cfg->car_model[0] != '\0' ? cfg->car_model : "Auto");
        }
        break;
    case BTN_NONE:   /* the year */
        if (d->have_vin && d->model_year > 0) {
            snprintf(out, len, "%d (VIN)", d->model_year);
        } else if (cfg->car_year != 0) {
            snprintf(out, len, "%u", (unsigned)cfg->car_year);
        } else {
            snprintf(out, len, "Auto");
        }
        break;
    case BTN_DIAG_SCAN:
        snprintf(out, len, "%s", shared.scanning ? "reading..." : d->have_vin ? d->vin
                                                                              : "not read");
        break;
    case BTN_UNITS:
        snprintf(out, len, "%s", cfg->metric ? "Metric" : "US");
        break;
    default:
        break;
    }
    LightLock_Unlock(&shared.lock);
}

/* About GaugeBoy (top screen). */
static void draw_about_top(int busy, bool connected)
{
    char header[TOP_COLS + 1];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf("\x1b[0m\x1b[2J");
    print_row(0, TOP_COLS, header);
    static const char *const about[] = {
        "",
        "GaugeBoy: OBD-II Deluxe",
        "Version " GB_VERSION_TEXT,
        "",
        "Reads a car's OBD-II data (check engine light,",
        "trouble codes, live data, fuel trims) through a",
        "Wi-Fi ELM327 adapter.",
        "",
        "Unofficial homebrew. Not made, endorsed or",
        "supported by Nintendo.",
        "",
        "MIT license. github.com/ly-man-do/gauge-boy",
        "",
        "Trouble code text: Wal33D/dtc-database (MIT).",
        "VIN models: NHTSA vPIC (US government, public",
        "domain). Fonts and the rest: CREDITS.md.",
        "",
        "Not for use by the driver while driving.",
    };
    for (int i = 0; i < (int)(sizeof about / sizeof about[0]); i++) {
        print_row(2 + i, TOP_COLS, about[i]);
    }
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
        snprintf(line, sizeof line, "SETUP > TESTS  %s:%u", cfg->adapter_host,
                 (unsigned)cfg->adapter_port);
        print_row(0, BOTTOM_COLS, line);
        for (int i = 0; i < BUTTON_COUNT; i++) {
            draw_button(&buttons[i], flashing(buttons[i].action, now),
                        buttons[i].action >= 0 && buttons[i].action == busy);
        }
        print_row(29, BOTTOM_COLS, "Results go to the log. L/R: scroll it");
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
        clear_rows(1, 29);
        print_row(1, BOTTOM_COLS, "SETUP");
        draw_list(setup_buttons, SETUP_BUTTON_COUNT, now, busy, 0, false);
        printf("\x1b[28;11H%-28.28s", "L/R: scroll the log above");
        return;
    }
    if (ui->screen == SCREEN_SETUP_WIFI || ui->screen == SCREEN_SETUP_VEHICLE ||
        ui->screen == SCREEN_SETUP_OTHER) {
        const button_t *list = ui->screen == SCREEN_SETUP_WIFI ? wifi_buttons
                             : ui->screen == SCREEN_SETUP_VEHICLE ? vehicle_buttons
                             : other_buttons;
        int count = ui->screen == SCREEN_SETUP_WIFI ? WIFI_BUTTON_COUNT
                  : ui->screen == SCREEN_SETUP_VEHICLE ? VEHICLE_BUTTON_COUNT
                  : OTHER_BUTTON_COUNT;
        print_row(1, BOTTOM_COLS, ui->screen == SCREEN_SETUP_WIFI ? "SETUP > WI-FI"
                                : ui->screen == SCREEN_SETUP_VEHICLE ? "SETUP > VEHICLE"
                                : "SETUP > OTHER");
        clear_rows(2, 29);
        for (int i = 0; i < count; i++) {
            const button_t *b = &list[i];
            bool active = flashing(b->action, now) || (b->action >= 0 && b->action == busy);
            if (b->w < 39 && b->action != BTN_NONE) {
                draw_button(b, flashing(b->action, now), b->action >= 0 && b->action == busy);
                continue;
            }
            char value[40];
            bar_value(b->action, cfg, value, sizeof value);
            draw_bar(b, value, active);
        }
        if (ui->screen == SCREEN_SETUP_VEHICLE) {
            printf("\x1b[27;11H%-28.28s", "Values from the VIN win;");
            printf("\x1b[28;11H%-28.28s", "yours are for cars with no");
            printf("\x1b[29;11H%-28.28s", "VIN. Tap a row to change it.");
        } else {
            printf("\x1b[28;11H%-28.28s", "L/R: scroll the log above");
        }
        return;
    }
    if (ui->screen == SCREEN_ABOUT) {
        clear_rows(1, 29);
        print_row(1, BOTTOM_COLS, "SETUP > OTHER > About");
        print_row(3, BOTTOM_COLS, "About GaugeBoy is on the top screen.");
        draw_list(about_buttons, ABOUT_BUTTON_COUNT, now, busy, 0, false);
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

    if (ui->screen == SCREEN_TRIM) {
        LightLock_Lock(&shared.lock);
        bool on = shared.live_on;
        LightLock_Unlock(&shared.lock);
        clear_rows(1, 24);
        print_row(2, BOTTOM_COLS, "Fuel trims: how much fuel the engine");
        print_row(3, BOTTOM_COLS, "computer adds (+) or takes away (-).");
        print_row(5, BOTTOM_COLS, "Short: right now. Long: learned.");
        print_row(6, BOTTOM_COLS, "Total: both together.");
        printf("\x1b[9;1H\x1b[32m Green\x1b[0m  within 10 %%");
        printf("\x1b[10;1H\x1b[33m Amber\x1b[0m  10 to 20 %%");
        printf("\x1b[11;1H\x1b[31m Red  \x1b[0m  beyond 20 %%");
        print_row(13, BOTTOM_COLS, "Bars run from -25 to +25 %; < or >");
        print_row(14, BOTTOM_COLS, "at the end means beyond that.");
        print_row(16, BOTTOM_COLS, "Check trims warm, at idle, then at");
        print_row(17, BOTTOM_COLS, "a steady 2500 rpm (parked).");
        for (int i = 0; i < TRIM_BUTTON_COUNT; i++) {
            button_t b = trim_buttons[i];
            if (b.action == BTN_LIVE_TOGGLE) {
                b.label = on ? "STOP" : "START";
            }
            draw_button_ex(&b, flashing(b.action, now), false,
                           b.action == BTN_LIVE_TOGGLE && on);
        }
        return;
    }
    if (ui->screen == SCREEN_LIVE_MENU) {
        print_row(1, BOTTOM_COLS, "Pick a view: a few PIDs for one job");
        clear_rows(2, 24);
        for (int i = 0; i < LIVE_VIEW_COUNT + 1; i++) {
            const button_t *b = &live_menu_buttons[i];
            draw_button_ex(b, flashing(b->action, now), false,
                           b->action == BTN_LIVE_VIEW - ui->live_view);
        }
        return;
    }
    if (ui->screen == SCREEN_LIVE) {
        char msg[sizeof shared.live_msg];
        LightLock_Lock(&shared.lock);
        bool on = shared.live_on;
        memcpy(msg, shared.live_msg, sizeof msg);
        LightLock_Unlock(&shared.lock);
        clear_rows(1, 24);
        snprintf(line, sizeof line, "View: %s", live_view_name(ui->live_view));
        print_row(2, BOTTOM_COLS, line);
        print_row(3, BOTTOM_COLS, live_view_purpose(ui->live_view));
        print_row(5, BOTTOM_COLS, "< > or L / R: other views");
        if (live_view_warns(ui->live_view)) {
            print_row(7, BOTTOM_COLS, "Many PIDs refresh slowly: the top");
            print_row(8, BOTTOM_COLS, "screen says how often.");
        }
        if (ui->live_view == LIVE_VIEW_CUSTOM) {
            draw_button(&live_edit_button, flashing(live_edit_button.action, now), false);
        }
        if (msg[0] != '\0') {
            print_row(21, BOTTOM_COLS, msg);
        }
        for (int i = 0; i < LIVE_BUTTON_COUNT; i++) {
            button_t b = live_buttons[i];
            if (b.action == BTN_LIVE_TOGGLE) {
                b.label = on ? "STOP" : "START";
            }
            draw_button_ex(&b, flashing(b.action, now), false,
                           b.action == BTN_LIVE_TOGGLE && on);
        }
        snprintf(line, sizeof line, "%d/%d", ui->live_view + 1, LIVE_VIEW_COUNT);
        printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
        return;
    }
    if (ui->screen == SCREEN_LIVE_EDIT) {
        uint8_t choices[OBD_LIVE_MAX];
        int nc = live_choices_now(choices);
        uint8_t pids[CONFIG_LIVE_PIDS_MAX];
        LightLock_Lock(&shared.lock);
        int np = cfg->live_pid_count;
        memcpy(pids, cfg->live_pids, sizeof pids);
        LightLock_Unlock(&shared.lock);

        print_row(1, BOTTOM_COLS, "Custom: tap a PID; fewer = faster");
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
            print_row(12, BOTTOM_COLS, "The car's PIDs show here once LIVE");
            print_row(13, BOTTOM_COLS, "has found the car.");
        }
        for (int i = 0; i < LIVE_EDIT_BUTTON_COUNT; i++) {
            const button_t *b = &live_edit_buttons[i];
            draw_button(b, flashing(b->action, now), false);
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
    bool trim = diag_trim_offered();
    clear_rows(25, 29);   /* the row's layout changes with the TRIM button */
    draw_list(trim ? diag_trim_buttons : diag_buttons, trim ? DIAG_TRIM_BUTTON_COUNT : DIAG_BUTTON_COUNT,
              now, busy, BTN_TAB_STORED - (int)ui->tab, ui->permanent_hidden);
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
    printf("\x1b[%d;%dH%-3.3s", 27 + 1, trim ? DIAG_PAGE_COL_TRIM : 15, line);
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

/* SETUP > VEHICLE > Model: the system keyboard (UI thread; the keyboard
 * applet takes both screens until it closes). Empty = auto. True if the
 * model changed. */
static bool edit_model(gb_config_t *cfg)
{
    char text[sizeof cfg->car_model];
    LightLock_Lock(&shared.lock);
    snprintf(text, sizeof text, "%s", cfg->car_model);
    LightLock_Unlock(&shared.lock);

    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, (int)sizeof text - 1);
    swkbdSetHintText(&kb, "Model, e.g. Tracker (empty: from the VIN)");
    swkbdSetInitialText(&kb, text);
    swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);
    char out[sizeof cfg->car_model];
    if (swkbdInputText(&kb, out, sizeof out) != SWKBD_BUTTON_CONFIRM) {
        return false;
    }
    config_clean_text(out);
    if (strcmp(out, text) == 0) {
        return false;
    }
    LightLock_Lock(&shared.lock);
    snprintf(cfg->car_model, sizeof cfg->car_model, "%s", out);
    LightLock_Unlock(&shared.lock);
    return true;
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
    spike.units = cfg.metric ? OBD_UNITS_METRIC : OBD_UNITS_US;
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
        spike_logf(&spike, "Wi-Fi slots: 1 \"%s\", 2 \"%s\", 3 \"%s\"; using %s (SETUP > WI-FI)",
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
    spike_logf(&spike, "then DIAG > SCAN (or SETUP > TESTS).");
    shared.diag.make = -1;

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    int worker_prio = prio + 1 > 0x3F ? 0x3F : (int)prio + 1;
    Thread worker = threadCreate(worker_main, &spike, WORKER_STACK, worker_prio, -2, false);
    if (worker == NULL) {
        spike_logf(&spike, "! Could not start the worker thread; buttons will not work");
    }

    ui_state_t ui = { SCREEN_HOME, DIAG_TAB_STORED, -1, 0, NULL, false, false, -1, SCREEN_SETUP,
                      false, LIVE_VIEW_ENGINE, SCREEN_HOME };
    live_menu_count();
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
            case SCREEN_DIAG:
                if (diag_trim_offered()) {
                    list = diag_trim_buttons;
                    count = DIAG_TRIM_BUTTON_COUNT;
                } else {
                    list = diag_buttons;
                    count = DIAG_BUTTON_COUNT;
                }
                break;
            case SCREEN_SETUP: list = setup_buttons; count = SETUP_BUTTON_COUNT; break;
            case SCREEN_SETUP_WIFI:    list = wifi_buttons;    count = WIFI_BUTTON_COUNT;    break;
            case SCREEN_SETUP_VEHICLE: list = vehicle_buttons; count = VEHICLE_BUTTON_COUNT; break;
            case SCREEN_SETUP_OTHER:   list = other_buttons;   count = OTHER_BUTTON_COUNT;   break;
            case SCREEN_ABOUT:         list = about_buttons;   count = ABOUT_BUTTON_COUNT;   break;
            case SCREEN_GUIDE: list = guide_buttons; count = GUIDE_BUTTON_COUNT; break;
            case SCREEN_MAKE:  count = make_screen_buttons(ui.page, &list);   break;
            case SCREEN_LIVE:
                list = live_buttons;
                count = LIVE_BUTTON_COUNT;
                if (ui.live_view == LIVE_VIEW_CUSTOM &&
                    button_at(&live_edit_button, 1, tp.px, tp.py) == 0) {
                    list = &live_edit_button;
                    count = 1;
                }
                break;
            case SCREEN_LIVE_EDIT: list = live_edit_buttons; count = LIVE_EDIT_BUTTON_COUNT; break;
            case SCREEN_TRIM:      list = trim_buttons;      count = TRIM_BUTTON_COUNT;      break;
            case SCREEN_LIVE_MENU: list = live_menu_buttons; count = LIVE_VIEW_COUNT + 1;  break;
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
            } else if (ui.screen == SCREEN_LIVE_EDIT) {
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
            else if ((down & KEY_L) && (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_MAKE || ui.screen == SCREEN_LIVE || ui.screen == SCREEN_LIVE_EDIT)) { action = BTN_PAGE_PREV; tapped = true; }
            else if ((down & KEY_R) && (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_MAKE || ui.screen == SCREEN_LIVE || ui.screen == SCREEN_LIVE_EDIT)) { action = BTN_PAGE_NEXT; tapped = true; }
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
            case BTN_MENU_LIVE: {
                LightLock_Lock(&shared.lock);
                int v = live_view_find(cfg.live_view);
                LightLock_Unlock(&shared.lock);
                ui.live_view = v < 0 ? LIVE_VIEW_ENGINE : v;
                ui.screen = SCREEN_LIVE_MENU;
                ui.page = 0;
                bottom_full = top_dirty = true;
                break;
            }
            case BTN_LIVE_EDIT:
                ui.screen = SCREEN_LIVE_EDIT;
                ui.page = 0;
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
            case BTN_DIAG_TRIM:
                ui.trim_back = action == BTN_DIAG_TRIM ? SCREEN_DIAG : SCREEN_HOME;
                ui.screen = SCREEN_TRIM;
                trim_start();
                bottom_full = top_dirty = true;
                break;
            case BTN_MENU_FUN:
            case BTN_MENU_LOG:
                ui.notice = action == BTN_MENU_FUN   ? "FUN: fun modes come in version 0.6"
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
                if (ui.screen == SCREEN_LIVE) {   /* the previous view */
                    live_select_view(&ui, (ui.live_view + LIVE_VIEW_COUNT - 1) % LIVE_VIEW_COUNT);
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.page > 0) { ui.page--; ui.selected = -1; top_dirty = true; }
                bottom_full |= ui.screen == SCREEN_MAKE;   /* the last page is shorter */
                break;
            case BTN_PAGE_NEXT: {
                if (ui.screen == SCREEN_LIVE) {   /* the next view */
                    live_select_view(&ui, (ui.live_view + 1) % LIVE_VIEW_COUNT);
                    bottom_full = top_dirty = true;
                    break;
                }
                int pages = ui.screen == SCREEN_GUIDE ? WIFI_GUIDE_STEPS
                          : ui.screen == SCREEN_MAKE  ? make_page_count()
                          : ui.screen == SCREEN_LIVE_EDIT ? live_page_count()
                          :                             page_count(n);
                if (ui.page + 1 < pages) { ui.page++; ui.selected = -1; top_dirty = true; }
                bottom_full |= ui.screen == SCREEN_MAKE;
                break;
            }
            case BTN_SETUP_TESTS:
            case BTN_SETUP_WIFI:
            case BTN_SETUP_VEHICLE:
            case BTN_SETUP_OTHER:
                ui.screen = action == BTN_SETUP_TESTS ? SCREEN_TEST
                          : action == BTN_SETUP_WIFI  ? SCREEN_SETUP_WIFI
                          : action == BTN_SETUP_VEHICLE ? SCREEN_SETUP_VEHICLE
                          : SCREEN_SETUP_OTHER;
                bottom_full = top_dirty = true;
                break;
            case BTN_ABOUT:
                ui.screen = SCREEN_ABOUT;
                bottom_full = top_dirty = true;
                break;
            case BTN_MODEL_EDIT:
                if (edit_model(&cfg) && worker != NULL) {
                    enqueue(BTN_SET_VEHICLE);
                }
                bottom_full = top_dirty = true;   /* the keyboard covered both screens */
                break;
            case BTN_YEAR_PREV:
            case BTN_YEAR_NEXT: {
                time_t t = time(NULL);
                struct tm *tm = gmtime(&t);
                int last = (tm != NULL ? tm->tm_year + 1900 : 2026) + 1;
                LightLock_Lock(&shared.lock);
                cfg.car_year = (uint16_t)config_year_step(cfg.car_year, action == BTN_YEAR_NEXT ? 1 : -1,
                                                   last);
                LightLock_Unlock(&shared.lock);
                if (worker != NULL) {
                    enqueue(BTN_SET_VEHICLE);
                }
                break;
            }
            case BTN_UNITS:
                LightLock_Lock(&shared.lock);
                cfg.metric = !cfg.metric;
                LightLock_Unlock(&shared.lock);
                if (worker != NULL) {
                    enqueue(BTN_SET_UNITS);
                }
                break;
            case BTN_GUIDE:
            case BTN_HOME_WIFI:
                ui.guide_back = action == BTN_GUIDE ? SCREEN_SETUP_WIFI : SCREEN_HOME;
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
                if (ui.screen == SCREEN_TRIM) {
                    LightLock_Lock(&shared.lock);
                    shared.live_on = false;
                    shared.live_ui_version++;
                    LightLock_Unlock(&shared.lock);
                }
                if (ui.screen == SCREEN_LIVE_EDIT || ui.screen == SCREEN_LIVE) {
                    if (ui.screen == SCREEN_LIVE) {
                        LightLock_Lock(&shared.lock);
                        shared.live_on = false;
                        shared.live_ui_version++;
                        LightLock_Unlock(&shared.lock);
                    }
                    if (ui.live_dirty && worker != NULL) {
                        enqueue(BTN_LIVE_SAVE);
                    }
                    ui.live_dirty = false;
                    ui.page = 0;
                    ui.screen = ui.screen == SCREEN_LIVE_EDIT ? SCREEN_LIVE : SCREEN_LIVE_MENU;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_DIAG ||
                    ui.screen == SCREEN_MAKE) {
                    ui.page = 0;
                }
                ui.screen = ui.screen == SCREEN_GUIDE ? ui.guide_back
                          : ui.screen == SCREEN_TRIM ? ui.trim_back
                          : ui.screen == SCREEN_MAKE ? SCREEN_SETUP_VEHICLE
                          : ui.screen == SCREEN_ABOUT ? SCREEN_SETUP_OTHER
                          : (ui.screen == SCREEN_TEST || ui.screen == SCREEN_SETUP_WIFI ||
                             ui.screen == SCREEN_SETUP_VEHICLE ||
                             ui.screen == SCREEN_SETUP_OTHER) ? SCREEN_SETUP
                          : SCREEN_HOME;
                bottom_full = top_dirty = true;
                break;
            case BTN_CLEAR_ASK: {
                LightLock_Lock(&shared.lock);
                bool scanned = shared.diag.link.protocol != 0 && !shared.diag.vin_only &&
                               !shared.scanning;
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
                if (ui.screen == SCREEN_LIVE_MENU && action <= BTN_LIVE_VIEW &&
                    action > BTN_LIVE_VIEW - LIVE_VIEW_COUNT) {
                    live_select_view(&ui, BTN_LIVE_VIEW - action);
                    ui.screen = SCREEN_LIVE;
                    bottom_full = top_dirty = true;
                } else if (ui.screen == SCREEN_LIVE_EDIT && action <= BTN_LIVE_PICK &&
                    action > BTN_LIVE_PICK - LIVE_PAGE_SLOTS) {
                    uint8_t choices[OBD_LIVE_MAX];
                    int nc = live_choices_now(choices);
                    int k = ui.page * LIVE_PAGE_SLOTS + (BTN_LIVE_PICK - action);
                    if (k < nc) {
                        LightLock_Lock(&shared.lock);
                        cfg.live_pid_count = (uint8_t)live_toggle(cfg.live_pids, cfg.live_pid_count,
                                                                  CONFIG_LIVE_PIDS_MAX, choices[k]);
                        LightLock_Unlock(&shared.lock);
                        live_select_view(&ui, LIVE_VIEW_CUSTOM);   /* poll the new list */
                        ui.live_dirty = true;
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
                        ui.screen = SCREEN_SETUP_VEHICLE;
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

        /* Log scrolling on SETUP and its pages. */
        if (ui.screen == SCREEN_SETUP || ui.screen == SCREEN_TEST ||
            ui.screen == SCREEN_SETUP_WIFI || ui.screen == SCREEN_SETUP_VEHICLE ||
            ui.screen == SCREEN_SETUP_OTHER) {
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
            if (ui.screen == SCREEN_LIVE || ui.screen == SCREEN_LIVE_EDIT ||
                ui.screen == SCREEN_TRIM) {
                bottom_dirty = true;
            }
        }
        /* LIVE values change many times a second: redraw at most 10 times. */
        bool live_screen = ui.screen == SCREEN_LIVE || ui.screen == SCREEN_LIVE_EDIT ||
                           ui.screen == SCREEN_LIVE_MENU || ui.screen == SCREEN_TRIM;
        bool live_due = live_screen && live_version != drawn_live_version &&
                        now - live_drawn_at >= 100;

        bool shows_log = ui.screen == SCREEN_SETUP || ui.screen == SCREEN_TEST ||
                         ui.screen == SCREEN_MAKE || ui.screen == SCREEN_SETUP_WIFI ||
                         ui.screen == SCREEN_SETUP_VEHICLE || ui.screen == SCREEN_SETUP_OTHER;
        if (top_dirty || busy != drawn_busy || connected != drawn_connected || live_due ||
            (shows_log ? (version != drawn_version || scroll != drawn_scroll)
                       : !live_screen && diag_version != drawn_diag_version)) {
            if (ui.screen == SCREEN_TRIM) {
                draw_trim_top(busy, connected);
                drawn_live_version = live_version;
                live_drawn_at = now;
            } else if (live_screen) {
                draw_live_top(ui.live_view, busy, connected);
                drawn_live_version = live_version;
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_GUIDE) {
                draw_guide_top(ui.page, cfg.adapter_host, busy, connected);
            } else if (ui.screen == SCREEN_ABOUT) {
                draw_about_top(busy, connected);
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
