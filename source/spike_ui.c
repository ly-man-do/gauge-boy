/*
 * Gauge Boy app, 3DS side: consoles, touch screens and the worker thread.
 *
 * Screens: DIAG (default: scan, codes, readiness), SETUP (Wi-Fi slot,
 * SAVE LOG, the adapter test screen) and TEST (the M0 adapter test
 * screen). The bottom screen of DIAG and SETUP has the main menu
 * (LIVE / DIAG / TRIM / TRACK / LOG / SETUP); M1 enables DIAG and SETUP.
 *
 * The UI thread never does network or file I/O after startup; the worker
 * thread runs the button actions and writes the log file.
 */
#include "spike_ui.h"

#include <3ds.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "data/baro_store.h"
#include "data/code_history.h"
#include "data/config.h"
#include "data/diag_view.h"
#include "data/drive_list.h"
#include "data/drive_log.h"
#include "data/garage.h"
#include "data/mileage.h"
#include "data/history.h"
#include "data/live_view.h"
#include "data/live_views.h"
#include "data/trim_view.h"
#include "data/wifi_guide.h"
#include "data/textlog.h"
#include "fun/achieve.h"
#include "fun/eco.h"
#include "fun/gmeter.h"
#include "fun/rev_sync.h"
#include "fun/shift_light.h"
#include "fun/time_attack.h"
#include "fun/track_bests.h"
#include "spike_actions.h"
#include "transport/conntest_server.h"
#include "transport/net.h"
#include "transport/tcp_transport.h"
#include "ui/graph_draw.h"
#include "ui/home_draw.h"
#include "icons.h"
#include "ui/diag_draw.h"
#include "ui/live_draw.h"
#include "ui/log_draw.h"
#include "ui/setup_draw.h"
#include "ui/track_draw.h"
#include "ui/trim_draw.h"
#include "obd/dtc.h"
#include "obd/obd_diag.h"
#include "obd/obd_live.h"
#include "version.h"

#define LOG_PATH      CONFIG_DIR "/spike-log.txt"
#define LOGS_DIR      CONFIG_DIR "/logs"   /* SAVE LOG copies go here */
#define DRIVES_DIR    CONFIG_DIR "/" DRIVE_LOG_DIR   /* REC drive logs */
#define REC_FLUSH_MS  5000u                  /* to the SD card at least this often */
#define REC_MAX_BYTES (50u * 1024 * 1024)    /* stop a drive log at 50 MB */
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
#define BTN_LOG_SCAN     112   /* worker: list the drive logs */
#define BTN_LOG_DELETE   113   /* worker: delete shared.drive_delete, then list again */
#define BTN_GRAPH_LOAD   114   /* worker: read the graph shared.graph_req asks for */
#define BTN_LOG_GRAPH    (-120) /* UI: LOG -> GRAPH of the selected drive */
#define BTN_GRAPH_EARLY  (-121) /* UI: GRAPH: move the window back in time */
#define BTN_GRAPH_LATE   (-122)
#define BTN_GRAPH_IN     (-123) /* zoom in / out on the time axis */
#define BTN_GRAPH_OUT    (-124)
#define BTN_GRAPH_ALL    (-125) /* the whole drive */
#define BTN_LOG_ASK      (-37) /* UI: LOG: open the delete question */
#define BTN_LOG_NO       (-38)
#define BTN_LOG_YES      (-39)
#define IDENTIFY_RETRY_MS 20000u
#define BTN_LIVE_TOGGLE  (-33) /* UI: LIVE START / STOP */
#define BTN_LIVE_PICK    (-80) /* UI: Custom PID toggle (-80 - slot on the page) */
#define BTN_LIVE_EDIT    (-34) /* UI: LIVE Custom view -> PID toggles */
#define BTN_DIAG_TRIM    (-35) /* UI: DIAG -> TRIM (lean / rich codes stored) */
#define BTN_LIVE_REC     (-36) /* UI: LIVE: record a drive on / off */
#define BTN_LIVE_VIEW    (-90) /* UI: LIVE view list (-90 - view) */
#define BTN_LIVE_STOP    (-42) /* UI: LIVE graphics: the toggle's Stop half */
#define BTN_LIVE_RECORD  (-43) /* UI: ... its Record half */
#define BTN_GUIDE        (-8)  /* UI: SETUP -> Wi-Fi guide */
#define BTN_CLEAR_ASK    (-5)  /* UI: open the clear-codes dialog */
#define BTN_CLEAR_CANCEL (-6)
#define BTN_CLEAR_YES    (-7)
#define BTN_BACK         (-4)  /* UI: TEST screen -> SETUP */
#define BTN_MENU_LIVE    (-10) /* UI: main menu */
#define BTN_MENU_DIAG    (-11)
#define BTN_MENU_TRIM    (-12)
#define BTN_MENU_TRACK   (-13)
#define BTN_MENU_LOG     (-14)
#define BTN_MENU_SETUP   (-15)
#define BTN_HOME_GARAGE     (-16) /* UI: HOME status cards */
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
#define BTN_ENGINE_BAR    (-141) /* Vehicle: the engine size (shows the value) */
#define BTN_ENGINE_PREV   (-142) /* UI: Vehicle: engine size down / up */
#define BTN_ENGINE_NEXT   (-143)
#define BTN_TRACK_TA      (-126) /* UI: TRACK -> TIME ATTACK */
#define BTN_TA_WARN_OK    (-127) /* UI: TIME ATTACK closed-course notice: OK */
#define BTN_TA_WARN_NO    (-128) /*     ... BACK */
#define BTN_TA_RUN        (-130) /* UI: TIME ATTACK run (-130 - ta_run_t) */
#define BTN_TA_OPEN       (-180) /* UI: TIME ATTACK (graphics): a run's screen (-180 - ta_run_t) */
#define BTN_TA_ARM        (-184) /* UI: ... its Start (parked only) */
#define BTN_TA_DISARM     (-185) /* UI: ... its Stop */
#define BTN_TRACK_SHIFT   (-129) /* UI: TRACK -> SHIFT LIGHT */
#define BTN_SHIFT_DOWN    (-134) /* UI: SHIFT LIGHT: shift point -100 / +100 rpm */
#define BTN_SHIFT_UP      (-135)
#define BTN_SHIFT_SAVE    115    /* worker: save the shift point to config.ini */
#define BTN_TRACK_GMETER  (-136) /* UI: TRACK -> G-METER */
#define BTN_GM_CAL        (-137) /* UI: G-METER: calibrate (parked) */
#define BTN_GM_RESET      (-138) /* UI: G-METER: reset the peaks (parked) */
#define BTN_TRACK_REVSYNC (-139) /* UI: TRACK -> REV SYNC */
#define BTN_RS_START      (-140) /* UI: REV SYNC: START / AGAIN */
#define BTN_RS_BEST       116    /* worker: high score (shared.hs_run): save the new one, read it */
#define BTN_TRACK_ECO     (-144) /* UI: TRACK -> ECO CHALLENGE */
#define BTN_ECO_RESET     (-145) /* UI: ECO: a new trip (parked) */
#define BTN_GARAGE_PREV   (-147) /* UI: GARAGE: the card before / after */
#define BTN_GARAGE_NEXT   (-148)
#define BTN_GARAGE_NICK   (-149) /* UI: GARAGE: type a nickname (keyboard) */
#define BTN_GARAGE_SCAN   117    /* worker: build the cards, count logged distance */
#define BTN_GARAGE_SAVE   118    /* worker: save shared.garage_nick for shared.garage_nick_key */
#define BTN_ACH_LOAD      119    /* worker: read achievements.csv */
#define BTN_ACH_BASE      200    /* worker: unlock achievement (action - BTN_ACH_BASE) */
#define BTN_TRACK_ACH     (-150) /* UI: TRACK -> ACHIEVEMENTS */
#define BTN_GARAGE_TRIP   (-151) /* UI: GARAGE -> TRIP METER */
#define BTN_GARAGE_MILES  (-152) /* UI: GARAGE -> MILEAGE LOG */
#define BTN_TRIP_RESET    (-153) /* UI: TRIP: start a new trip (parked) */
#define BTN_MILES_NEW     (-154) /* UI: MILEAGE: type a new entry */
#define BTN_MILES_DEL     (-155) /* UI: MILEAGE: delete the selected entry (tap twice) */
#define BTN_MILES_EDIT    (-156) /* UI: MILEAGE: edit the selected entry */
#define BTN_MILES_PREV    (-157) /* UI: MILEAGE: list page before / after */
#define BTN_MILES_NEXT    (-158)
#define BTN_DIAG_PAGE     (-170) /* UI: DIAG (graphics): a page (-170 - diag_tab_t) */
#define BTN_MILES_ROW     (-160) /* UI: MILEAGE: list row (-160 - slot) */
#define BTN_TRIP_SAVE     120    /* worker: trip.csv for shared.trip_key = shared.trip_km */
#define BTN_MILES_ADD     121    /* worker: add shared.miles_new for shared.miles_key */
#define BTN_MILES_DROP    122    /* worker: delete shared.miles_key's entry shared.miles_index */
#define BTN_MILES_SET     123    /* worker: replace that entry with shared.miles_new */

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
    { "TRACK", BTN_MENU_TRACK, 0, 14, 12, 9 },
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
    { "CHECK & CONNECT",  BTN_GUIDE_CHECK, 0, 2,  39, 5 },
    { "Wi-Fi slot",       BTN_WIFI_SLOT,   0, 8,  39, 5 },
    { "CONNECTION GUIDE", BTN_GUIDE,       0, 14, 39, 5 },
    { "BACK",             BTN_BACK,        0, 25, 8, 5 },
};
#define WIFI_BUTTON_COUNT ((int)(sizeof wifi_buttons / sizeof wifi_buttons[0]))

static const button_t vehicle_buttons[] = {
    { "Make",   BTN_MAKE_SCREEN, 0,  2, 39, 5 },
    { "Model",  BTN_MODEL_EDIT,  0,  7, 39, 5 },
    { "Year",   BTN_NONE,        0, 12, 27, 5 },
    { "<",      BTN_YEAR_PREV,   28, 12, 5, 5 },
    { ">",      BTN_YEAR_NEXT,   34, 12, 5, 5 },
    { "Engine", BTN_ENGINE_BAR,  0, 17, 27, 5 },
    { "<",      BTN_ENGINE_PREV, 28, 17, 5, 5 },
    { ">",      BTN_ENGINE_NEXT, 34, 17, 5, 5 },
    { "VIN",    BTN_DIAG_SCAN,   9, 25, 30, 5 },   /* tap to read */
    { "BACK",   BTN_BACK,        0, 25, 8, 5 },
};
#define VEHICLE_BUTTON_COUNT ((int)(sizeof vehicle_buttons / sizeof vehicle_buttons[0]))

static const button_t other_buttons[] = {
    { "Units",          BTN_UNITS, 0, 2, 39, 5 },
    { "About Gauge Boy", BTN_ABOUT, 0, 8, 39, 5 },
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
    SCREEN_ABOUT,              /* SETUP > OTHER > About */
    SCREEN_LOG,                /* LOG: recorded drives */
    SCREEN_GRAPH,              /* LOG > GRAPH: one value of a drive against time */
    SCREEN_TRACK,              /* TRACK: the track modes */
    SCREEN_TIME_ATTACK,        /* TRACK > TIME ATTACK: speed alone, timed runs */
    SCREEN_SHIFT,              /* TRACK > SHIFT LIGHT: RPM, LEDs on the top screen */
    SCREEN_GMETER,             /* TRACK > G-METER: the 3DS accelerometer */
    SCREEN_REVSYNC,            /* TRACK > REV SYNC: parked-only throttle blip game */
    SCREEN_ECO,                /* TRACK > ECO CHALLENGE: fuel economy and a smooth-driving score */
    SCREEN_GARAGE,             /* TRACK > GARAGE: a card per car */
    SCREEN_ACHIEVE,            /* TRACK > ACHIEVEMENTS */
    SCREEN_TRIP,               /* GARAGE > TRIP METER */
    SCREEN_MILEAGE,            /* GARAGE > MILEAGE LOG */
    SCREEN_DIAG_PAGE           /* DIAG (graphics): Stored, Pending, Permanent or Readiness */
} screen_t;

/* TRACK: one button per mode built so far, then BACK. */
static const button_t track_buttons[] = {
    { "TIME ATTACK", BTN_TRACK_TA,    0,  4, 19, 5 },
    { "SHIFT LIGHT", BTN_TRACK_SHIFT, 20, 4, 19, 5 },
    { "G-METER",     BTN_TRACK_GMETER,  0,  10, 19, 5 },
    { "REV SYNC",    BTN_TRACK_REVSYNC, 20, 10, 19, 5 },
    { "ECO",         BTN_TRACK_ECO,     0,  16, 19, 5 },
    { "BACK",        BTN_BACK,     0, 25, 8, 5 },
    { "ACHIEVEMENTS", BTN_TRACK_ACH,   9, 25, 30, 5 },
};
#define TRACK_BUTTON_COUNT ((int)(sizeof track_buttons / sizeof track_buttons[0]))

/* TIME ATTACK: the three runs (labels follow the units), then BACK. */
static const button_t ta_buttons[] = {
    { NULL,   BTN_TA_RUN - TA_RUN_LAUNCH,  0,  19, 12, 5 },
    { NULL,   BTN_TA_RUN - TA_RUN_BRAKE,   13, 19, 13, 5 },
    { NULL,   BTN_TA_RUN - TA_RUN_QUARTER, 27, 19, 12, 5 },
    { "BACK", BTN_BACK,                    0,  25, 8, 5 },
};
#define TA_BUTTON_COUNT ((int)(sizeof ta_buttons / sizeof ta_buttons[0]))

/* SHIFT LIGHT: the shift point down / up, then BACK. */
static const button_t shift_buttons[] = {
    { "-100", BTN_SHIFT_DOWN, 0,  19, 12, 5 },
    { "+100", BTN_SHIFT_UP,   27, 19, 12, 5 },
    { "BACK", BTN_BACK,       0,  25, 8, 5 },
};
#define SHIFT_BUTTON_COUNT ((int)(sizeof shift_buttons / sizeof shift_buttons[0]))

/* G-METER: CALIBRATE, RESET PEAKS, then BACK. */
static const button_t gm_buttons[] = {
    { "CALIBRATE",   BTN_GM_CAL,   0,  19, 19, 5 },
    { "RESET PEAKS", BTN_GM_RESET, 20, 19, 19, 5 },
    { "BACK",        BTN_BACK,     0,  25, 8, 5 },
};
#define GM_BUTTON_COUNT ((int)(sizeof gm_buttons / sizeof gm_buttons[0]))

/* ACHIEVEMENTS: BACK. */
static const button_t ach_buttons[] = {
    { "BACK", BTN_BACK, 0, 25, 8, 5 },
};
#define ACH_BUTTON_COUNT ((int)(sizeof ach_buttons / sizeof ach_buttons[0]))

/* GARAGE: < NICKNAME >, then BACK. */
static const button_t garage_buttons[] = {
    { "TRIP",        BTN_GARAGE_TRIP,  0,  13, 19, 5 },
    { "MILEAGE LOG", BTN_GARAGE_MILES, 20, 13, 19, 5 },
    { "<",        BTN_GARAGE_PREV, 0,  19, 8, 5 },
    { "NICKNAME", BTN_GARAGE_NICK, 9,  19, 21, 5 },
    { ">",        BTN_GARAGE_NEXT, 31, 19, 8, 5 },
    { "BACK",     BTN_BACK,        0,  25, 8, 5 },
};
#define GARAGE_BUTTON_COUNT ((int)(sizeof garage_buttons / sizeof garage_buttons[0]))

/* TRIP METER: RESET TRIP, BACK. MILEAGE LOG: NEW ENTRY, DELETE LAST, BACK. */
static const button_t trip_buttons[] = {
    { "RESET TRIP", BTN_TRIP_RESET, 0, 19, 19, 5 },
    { "BACK",       BTN_BACK,       0, 25, 8, 5 },
};
#define TRIP_BUTTON_COUNT ((int)(sizeof trip_buttons / sizeof trip_buttons[0]))
static const button_t miles_buttons[] = {
    { "NEW ENTRY", BTN_MILES_NEW,  24, 1,  15, 5 },
    { "BACK",      BTN_BACK,       0,  25, 8, 5 },
    { "<",         BTN_MILES_PREV, 9,  25, 5, 5 },
    { ">",         BTN_MILES_NEXT, 17, 25, 5, 5 },
    { "EDIT",      BTN_MILES_EDIT, 23, 25, 8, 5 },
    { "DELETE",    BTN_MILES_DEL,  32, 25, 7, 5 },
};
#define MILES_BUTTON_COUNT ((int)(sizeof miles_buttons / sizeof miles_buttons[0]))

/* ECO CHALLENGE: RESET TRIP, then BACK. */
static const button_t eco_buttons[] = {
    { "RESET TRIP", BTN_ECO_RESET, 0, 19, 19, 5 },
    { "BACK",       BTN_BACK,      0, 25, 8, 5 },
};
#define ECO_BUTTON_COUNT ((int)(sizeof eco_buttons / sizeof eco_buttons[0]))

/* REV SYNC: START, then BACK. */
static const button_t rs_buttons[] = {
    { "START", BTN_RS_START, 0,  19, 19, 5 },
    { "BACK",  BTN_BACK,     0,  25, 8, 5 },
};
#define RS_BUTTON_COUNT ((int)(sizeof rs_buttons / sizeof rs_buttons[0]))

/* The closed-course notice, once a session. */
static const button_t ta_warn_buttons[] = {
    { "BACK",         BTN_TA_WARN_NO, 0,  23, 14, 6 },
    { "I UNDERSTAND", BTN_TA_WARN_OK, 17, 23, 22, 6 },
};
#define TA_WARN_BUTTON_COUNT ((int)(sizeof ta_warn_buttons / sizeof ta_warn_buttons[0]))

/* TRIM: BACK and START / STOP (the bars and hints are on the top screen). */
static const button_t trim_buttons[] = {
    { "BACK", BTN_BACK,        0,  25, 8, 5 },
    { NULL,   BTN_LIVE_REC,    12, 25, 12, 5 },   /* REC / REC m:ss */
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
    { NULL,   BTN_LIVE_REC,    26, 19, 13, 5 },   /* REC / REC m:ss */
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
/* LOG: a paged list of drives (rows like DIAG's), BACK < page > DELETE. */
static const button_t log_buttons[] = {
    { "BACK",   BTN_BACK,      0,  25, 8, 5 },
    { "<",      BTN_PAGE_PREV, 9,  25, 5, 5 },
    { ">",      BTN_PAGE_NEXT, 17, 25, 5, 5 },
    { "GRAPH",  BTN_LOG_GRAPH, 23, 25, 8, 5 },
    { "DELETE", BTN_LOG_ASK,   32, 25, 7, 5 },
};
#define LOG_BUTTON_COUNT ((int)(sizeof log_buttons / sizeof log_buttons[0]))
/* GRAPH: move and zoom the time window, pick the value, the whole drive. */
static const button_t graph_buttons[] = {
    { "<<",     BTN_GRAPH_EARLY, 0,  3, 9, 6 },
    { "ZOOM -", BTN_GRAPH_OUT,   10, 3, 9, 6 },
    { "ZOOM +", BTN_GRAPH_IN,    20, 3, 9, 6 },
    { ">>",     BTN_GRAPH_LATE,  30, 3, 9, 6 },
    { "BACK",   BTN_BACK,        0,  25, 8, 5 },
    { "<",      BTN_PAGE_PREV,   9,  25, 5, 5 },
    { ">",      BTN_PAGE_NEXT,   17, 25, 5, 5 },
    { "ALL",    BTN_GRAPH_ALL,   28, 25, 11, 5 },
};
#define GRAPH_BUTTON_COUNT ((int)(sizeof graph_buttons / sizeof graph_buttons[0]))

static const button_t log_delete_buttons[] = {
    { "CANCEL", BTN_LOG_NO,  0,  23, 15, 6 },
    { "DELETE", BTN_LOG_YES, 17, 23, 22, 6 },
};

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
    int guide_step;            /* wifi_step_t in progress */
    bool guide_ok;             /* the last CHECK connected */

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
    int live_module;                       /* obd_module_t the view polls */
    uint32_t live_want_period[OBD_LIVE_MAX];
    int live_want_count;
    bool live_link_ok;
    char live_msg[48];         /* status line, "" while polling fine */
    unsigned live_ui_version;  /* bumped when the bottom screen must change */
    drive_info_t drives[DRIVE_LIST_MAX];   /* LOG: newest first */
    int drive_count;
    bool drives_listed;        /* the list has been read at least once */
    unsigned drive_version;    /* bumped when the list changes */
    char drive_delete[32];     /* LOG: the file to delete */
    struct {
        char file[32];
        int col;
        double t0, t1;
    } graph_req;               /* GRAPH: what to load */
    graph_cols_t graph;        /* GRAPH: the loaded columns */
    drive_column_t graph_columns[DRIVE_COL_MAX];
    int graph_ncols;           /* value columns in the file */
    int graph_col;             /* the one loaded */
    bool graph_loading;
    char graph_msg[48];        /* an error, or "" */
    unsigned graph_version;
    bool rec_want;             /* UI: record the LIVE view (REC) */
    bool rec_active;           /* worker: a drive log is open */
    uint64_t rec_started_ms;   /* when it was opened */
    char rec_file[40];         /* "drive-20261116-171230.csv" */
    /* TIME ATTACK. The UI sets ta_on, ta_gen and the run; the worker times. */
    bool ta_on;                /* speed alone, no ATRV */
    bool shift_on;             /* SHIFT LIGHT: RPM (and speed slowly), no ATRV */
    bool gmeter_on;            /* G-METER: speed, no ATRV */
    bool revsync_on;           /* REV SYNC: RPM and speed, no ATRV */
    bool eco_on;               /* ECO: speed, RPM, MAF / MAP, intake temperature, no ATRV */
    const char *hs_run;        /* UI: which high score BTN_RS_BEST handles ("revsync", "eco") */
    char ach_text[1024];       /* achievements.csv (worker) */
    int ach_new;               /* the latest unlock (achieve_id_t), -1 = none */
    unsigned ach_version;      /* bumped on each unlock */
    garage_t garage;           /* GARAGE: the cards (worker) */
    char garage_bests[4096];   /* bests.csv's text, for the cards */
    char garage_current[32];   /* the connected car's key, "" if not known */
    bool garage_scanning;
    char garage_msg[48];       /* progress or a problem, "" */
    unsigned garage_version;
    char garage_nick_key[32];  /* UI: the card a new nickname is for */
    char trip_text[2048];      /* trip.csv (worker) */
    char miles_text[8192];     /* mileage.csv (worker) */
    bool trip_on;              /* TRIP: odometer and speed polled, no ATRV */
    char trip_key[32];         /* UI: the car whose trip starts ... */
    float trip_km;             /* ... at this odometer reading */
    char miles_key[32];        /* UI: the car a mileage change is for */
    int miles_index;           /* ... its entry (place among the car's entries) */
    mileage_entry_t miles_new;
    char garage_nick[GARAGE_NICK_MAX + 1];
    int rs_save_score;         /* UI: a finished game's score for the worker, 0 = none */
    bool rs_car_known;         /* worker: the high score can be saved */
    bool rs_have_best;
    track_best_t rs_best;
    bool rs_record;            /* the last game beat it */
    unsigned ta_gen;           /* bumped to arm ta_want_run */
    int ta_want_run;           /* ta_run_t */
    bool ta_want_metric;
    ta_t ta;                   /* the worker's timer (copy) */
    bool ta_car_known;         /* bests can be saved */
    bool ta_have_best;         /* this car's best for the run */
    track_best_t ta_best;
    bool ta_record;            /* the latest result is a new best */
    bool ta_saved;             /* ... and it was written to bests.csv */
    unsigned ta_version;
    bool ta_armed;             /* timing (graphics: a run's Start); else polling speed alone */
    bool ta_have_bests[TA_RUN_COUNT];   /* this car's best for each run */
    track_best_t ta_bests[TA_RUN_COUNT];
    bool ta_have_last[TA_RUN_COUNT];    /* each run's last result this session */
    float ta_last[TA_RUN_COUNT];

    volatile bool quit;
} shared;

static gb_config_t *app_cfg; /* main's settings; wifi_slot changes on the worker */

static void achieve_earn(spike_t *s, achieve_id_t id);   /* worker */

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
    char engine[16] = "unknown";
    if (copy.engine_dl != 0) {
        snprintf(engine, sizeof engine, "%u.%u L", (unsigned)(copy.engine_dl / 10) % 100u,
                 (unsigned)(copy.engine_dl % 10));
    }
    spike_logf(s, "Vehicle: make %s, model %s, year %s, engine %s%s%s",
               copy.car_make[0] != '\0' ? copy.car_make : "Auto",
               copy.car_model[0] != '\0' ? copy.car_model : "Auto", year, engine,
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
    diag_view_set_units(s->units);
    bool saved = config_save(&copy, CONFIG_PATH);
    spike_logf(s, "Units: %s%s", copy.metric ? "metric (km/h, C, kPa)" : "US (mph, F, psi, inHg)",
               saved ? "" : " (! not saved to config.ini)");
}

/* SETUP SLOT (worker): read the chosen slot and, if it differs from what
 * Gauge Boy needs, show NOW / NEW so the user can enter NEW in System
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
        spike_logf(s, "= Slot %d \"%s\" is set up for Gauge Boy", slot, name);
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
    if (achieve_scan_ok(&d)) {
        achieve_earn(s, ACH_FIRST_SCAN);
    }
    if (achieve_scan_clean(&d)) {
        achieve_earn(s, ACH_CLEAN_BILL);
    }
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
        achieve_earn(s, ACH_CODE_BREAKER);
        diag_scan(s);
    }
    LightLock_Lock(&shared.lock);
    snprintf(shared.diag_msg, sizeof shared.diag_msg, "%s", obd_clear_result_str(res));
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
}

/* Wi-Fi guide CHECK (worker): slot chosen and named, its IP / DNS as the
 * guide says, on Wi-Fi, adapter answering. */
static void wifi_progress(wifi_step_t step)
{
    LightLock_Lock(&shared.lock);
    shared.guide_step = (int)step;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
}

/* CHECK & CONNECT (worker, SETUP > WI-FI and the guide): Wi-Fi (joining
 * the chosen slot if needed), the adapter (a fresh connection), the
 * adapter answering, then the car. Stops at the first step that fails and
 * says why; the slot's saved IP and DNS are read (never written) so a
 * failure can name them as the likely cause. */
static void guide_check(spike_t *s)
{
    static wifi_check_t c;
    memset(&c, 0, sizeof c);
    LightLock_Lock(&shared.lock);
    int slot = shared.wifi_slot;
    snprintf(c.host, sizeof c.host, "%s", app_cfg->adapter_host);
    c.port = app_cfg->adapter_port;
    shared.guide_checking = true;
    shared.guide_step = WIFI_STEP_WIFI;
    shared.diag_version++;
    LightLock_Unlock(&shared.lock);
    spike_logf(s, "CHECK & CONNECT");

    /* Wi-Fi: join the chosen slot, as CONNECT always did: it moves the 3DS
     * off another network (home Wi-Fi) onto the adapter's, and does nothing
     * if it's already there (gauge-boy-9fy). */
    char info[160], msg[160];
    msg[0] = '\0';
    bool joined = net_bring_up(msg, sizeof msg);
    if (msg[0] != '\0') {
        spike_logf(s, "%s%s", joined ? "" : "! ", msg);
    }
    net_describe(info, sizeof info);
    spike_logf(s, "Wi-Fi: %s", info);
    c.slot_any = slot == 0;
    const char *q1 = strchr(info, '"');
    const char *q2 = q1 != NULL ? strchr(q1 + 1, '"') : NULL;
    if (q2 != NULL) {
        snprintf(c.network, sizeof c.network, "%.*s", (int)(q2 - q1 - 1), q1 + 1);
    }
    bool on = strstr(info, "Wi-Fi on") != NULL;
    /* The slot to look at: the one chosen, else the one the 3DS is on. */
    const char *sp = strstr(info, "(slot ");
    c.slot = slot != 0 ? slot : sp != NULL ? atoi(sp + 6) : 0;
    net_ip_settings_t now, plan;
    char err[80];
    if (c.slot >= 1 && c.slot <= 3 && net_slot_read(c.slot, &now, err, sizeof err) &&
        spike_plan_slot(c.host, &plan)) {
        c.slot_read = true;
        c.slot_matches = spike_slot_matches(&now, &plan);
        snprintf(c.plan_ip, sizeof c.plan_ip, "%u.%u.%u.%u", plan.ip[0], plan.ip[1], plan.ip[2],
                 plan.ip[3]);
    }
    c.failed = WIFI_STEP_DONE;
    if (!on) {
        c.failed = WIFI_STEP_WIFI;
    }

    /* The adapter: a fresh attempt now, not after the reconnect wait. An
     * old connection may have gone stale (the 3DS slept, the adapter lost
     * power): if it doesn't answer, connect again once and ask again. */
    bool was_connected = tcp_transport_connected(s->transport);
    if (c.failed == WIFI_STEP_DONE) {
        wifi_progress(WIFI_STEP_ADAPTER);
        tcp_transport_t *t = s->transport;
        if (!tcp_transport_connected(t)) {
            t->next_attempt_ms = 0;
            tp_status_t st = tcp_transport_ensure_connected(t, TP_CONNECT_TIMEOUT_MS);
            if (st != TP_OK) {
                c.failed = WIFI_STEP_ADAPTER;
                c.tcp = st == TP_ERR_CONNECT ? WIFI_TCP_REFUSED
                      : st == TP_ERR_TIMEOUT ? WIFI_TCP_TIMEOUT : WIFI_TCP_OTHER;
                spike_logf(s, "! Can't reach %s:%u (%s)", c.host, c.port, tp_status_str(st));
            }
        }
    }
    if (c.failed == WIFI_STEP_DONE) {
        wifi_progress(WIFI_STEP_ANSWER);
        static elm_response_t r;
        bool answered = elm_command(s->elm, "ATI", ELM_TIMEOUT_DEFAULT_MS, &r) == ELM_OK &&
                        r.line_count > 0;
        if (!answered && was_connected) {
            spike_logf(s, "The old connection didn't answer; connecting again");
            tcp_transport_close(s->transport);
            s->transport->next_attempt_ms = 0;
            answered = tcp_transport_ensure_connected(s->transport, TP_CONNECT_TIMEOUT_MS) == TP_OK &&
                       elm_command(s->elm, "ATI", ELM_TIMEOUT_DEFAULT_MS, &r) == ELM_OK &&
                       r.line_count > 0;
        }
        if (answered) {
            snprintf(c.adapter, sizeof c.adapter, "%.23s", r.lines[r.line_count - 1]);
        } else {
            c.failed = WIFI_STEP_ANSWER;
            tcp_transport_close(s->transport);
        }
    }
    if (c.failed == WIFI_STEP_DONE) {
        wifi_progress(WIFI_STEP_CAR);
        identify_car(s);   /* protocol and VIN, as after any connect */
        LightLock_Lock(&shared.lock);
        c.car_ok = shared.diag.link.protocol != 0;
        diag_car_text(&shared.diag, c.car, sizeof c.car);
        if (c.car_ok && c.car[0] == '\0') {
            snprintf(c.car, sizeof c.car, "%.39s", elm_protocol_name(shared.diag.link.protocol));
        }
        LightLock_Unlock(&shared.lock);
    }

    static char lines[GUIDE_CHECK_LINES][WIFI_CHECK_COLS + 1];
    memset(lines, 0, sizeof lines);
    int n = wifi_check_lines(&c, lines, GUIDE_CHECK_LINES);
    for (int i = 0; i < n; i++) {
        spike_logf(s, "%s %s", c.failed == WIFI_STEP_DONE ? "=" : "!", lines[i]);
    }
    LightLock_Lock(&shared.lock);
    memcpy(shared.guide_check, lines, sizeof shared.guide_check);
    shared.guide_ok = c.failed == WIFI_STEP_DONE;
    shared.guide_checking = false;
    shared.connected = tcp_transport_connected(s->transport);
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

/* ---------- LOG: the drive list (worker) ---------- */

static void read_part(FILE *f, long at, char *buf, size_t want, size_t *got)
{
    *got = 0;
    if (fseek(f, at, SEEK_SET) == 0) {
        *got = fread(buf, 1, want, f);
    }
}

static void log_scan(spike_t *s)
{
    static char names[256][32];
    static drive_info_t list[DRIVE_LIST_MAX];
    static char head[DRIVE_HEAD_BYTES], tail[DRIVE_TAIL_BYTES];
    int n = 0;
    DIR *dir = opendir(DRIVES_DIR);
    if (dir != NULL) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL && n < 256) {
            if (drive_list_is_log_name(e->d_name)) {
                snprintf(names[n++], sizeof names[0], "%.31s", e->d_name);
            }
        }
        closedir(dir);
    }
    drive_list_sort(names, n);
    int count = n < DRIVE_LIST_MAX ? n : DRIVE_LIST_MAX;
    for (int i = 0; i < count; i++) {
        drive_info_t *d = &list[i];
        memset(d, 0, sizeof *d);
        snprintf(d->name, sizeof d->name, "%.31s", names[i]);
        d->duration_s = -1.0f;
        char path[96];
        snprintf(path, sizeof path, "%s/%s", DRIVES_DIR, d->name);
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            drive_info_parse_head(d, "", 0);
            continue;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        d->size = size > 0 ? (uint32_t)size : 0;
        size_t got = 0;
        read_part(f, 0, head, sizeof head, &got);
        drive_info_parse_head(d, head, got);
        long at = size > (long)sizeof tail ? size - (long)sizeof tail : 0;
        read_part(f, at, tail, sizeof tail, &got);
        d->duration_s = drive_info_parse_tail(tail, got);
        fclose(f);
    }
    LightLock_Lock(&shared.lock);
    memcpy(shared.drives, list, (size_t)count * sizeof list[0]);
    shared.drive_count = count;
    shared.drives_listed = true;
    shared.drive_version++;
    LightLock_Unlock(&shared.lock);
    spike_logf(s, "LOG: %d recorded drive%s%s", n, n == 1 ? "" : "s",
               n > count ? " (the newest 100 listed)" : "");
}

static void log_delete(spike_t *s)
{
    char name[32];
    LightLock_Lock(&shared.lock);
    memcpy(name, shared.drive_delete, sizeof name);
    name[sizeof name - 1] = '\0';
    LightLock_Unlock(&shared.lock);
    if (drive_list_is_log_name(name)) {
        char path[96];
        snprintf(path, sizeof path, "%s/%s", DRIVES_DIR, name);
        if (remove(path) == 0) {
            spike_logf(s, "LOG: deleted %s", name);
        } else {
            spike_logf(s, "! LOG: could not delete %s", name);
        }
    }
    log_scan(s);
}

/* Recent values of the polled PIDs (worker): for live graphs and track modes. */
static history_t live_history;

/* Vacuum on cars without a baro PID: the engine-off MAP saved per car. */
#define BARO_PATH CONFIG_DIR "/" BARO_STORE_FILE
static char baro_key[32];        /* this LIVE run's car, "" if not known */
static char baro_from[48];       /* "saved 2026-11-17", for the drive log */
static bool baro_written;        /* this run's fresh reading is saved */

static size_t read_small_file(const char *path, char *buf, size_t len)
{
    buf[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, len - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

/* ---------- achievements (worker) ---------- */

#define ACH_PATH CONFIG_DIR "/" ACHIEVE_FILE

static void achieve_load(void)
{
    static char text[1024];
    read_small_file(ACH_PATH, text, sizeof text);
    LightLock_Lock(&shared.lock);
    memcpy(shared.ach_text, text, sizeof shared.ach_text);
    LightLock_Unlock(&shared.lock);
}

/* Unlocks id if it's new: saves it, logs it and tells the UI. */
static void achieve_earn(spike_t *s, achieve_id_t id)
{
    static char text[1024], out[1024];
    read_small_file(ACH_PATH, text, sizeof text);
    char date[24];
    now_text(date, sizeof date);
    date[10] = '\0';
    bool is_new = false;
    size_t n = achieve_unlock(text, id, date, &is_new, out, sizeof out);
    if (!is_new) {
        return;
    }
    FILE *f = n > 0 ? fopen(ACH_PATH, "wb") : NULL;
    bool ok = f != NULL && fwrite(out, 1, n, f) == n;
    if (f != NULL && fclose(f) != 0) {
        ok = false;
    }
    spike_logf(s, "%sACHIEVEMENT: %s%s", ok ? "" : "! ", achieve_info(id)->name,
               ok ? "" : " (not saved to achievements.csv)");
    LightLock_Lock(&shared.lock);
    memcpy(shared.ach_text, ok ? out : text, sizeof shared.ach_text);
    shared.ach_new = (int)id;
    shared.ach_version++;
    LightLock_Unlock(&shared.lock);
}

/* At the start of LIVE: the car's saved engine-off MAP, unless PID 33 is
 * polled. */
static void baro_seed(spike_t *s, obd_live_t *lv)
{
    static char text[2048];
    if (s->transport->connects != identified_connects) {
        identify_car(s);   /* the car's VIN is the key: just 0902 on this link */
    }
    LightLock_Lock(&shared.lock);
    baro_store_key(&shared.diag, app_cfg->car_make, app_cfg->car_model, app_cfg->car_year,
                   baro_key, sizeof baro_key);
    LightLock_Unlock(&shared.lock);
    baro_from[0] = '\0';
    baro_written = false;
    bool polls_map = obd_live_get(lv, OBD_PID_MAP) != NULL ||
                     obd_live_get(lv, OBD_PID_MAP_WIDE) != NULL;
    if (baro_key[0] == '\0' || !polls_map || obd_live_get(lv, 0x33) != NULL) {
        return;
    }
    read_small_file(BARO_PATH, text, sizeof text);
    float kpa;
    char date[16];
    if (baro_store_get(text, baro_key, &kpa, date, sizeof date)) {
        obd_live_seed_baro(lv, kpa);
        snprintf(baro_from, sizeof baro_from, "engine-off MAP saved %s", date);
        spike_logf(s, "LIVE: vacuum uses this car's engine-off MAP from %s (%.1f kPa) until a "
                      "new one is read", date, (double)kpa);
    }
}

/* After each LIVE step: save a fresh engine-off MAP for this car. */
static void baro_save(spike_t *s, const obd_live_t *lv)
{
    static char text[2048], out[2048];
    if (baro_written || baro_key[0] == '\0' || !lv->have_engine_off_map ||
        lv->engine_off_map_saved) {
        return;
    }
    baro_written = true;
    char date[24];
    now_text(date, sizeof date);
    date[10] = '\0';   /* YYYY-MM-DD */
    read_small_file(BARO_PATH, text, sizeof text);
    size_t n = baro_store_set(text, baro_key, lv->engine_off_map_kpa, date, out, sizeof out);
    FILE *f = n > 0 ? fopen(BARO_PATH, "wb") : NULL;
    bool ok = f != NULL && fwrite(out, 1, n, f) == n;
    if (f != NULL && fclose(f) != 0) {
        ok = false;
    }
    snprintf(baro_from, sizeof baro_from, "engine-off MAP read %s", date);
    spike_logf(s, "%sLIVE: engine-off MAP %.1f kPa %s for this car's vacuum", ok ? "" : "! ",
               (double)lv->engine_off_map_kpa, ok ? "saved" : "not saved");
}

/* Odometer (PID A6, newer cars): the car's last reading, for the garage
 * card. Saved when first read in a LIVE run and again when it has gone up
 * by a kilometre or more. */
#define ODO_PATH CONFIG_DIR "/odometer.csv"
static float odo_saved_km = -1.0f;   /* this run's last saved reading, -1 = none */

static void odo_save(spike_t *s, const obd_live_t *lv)
{
    static char text[2048], out[2048];
    float km = 0.0f;
    if (baro_key[0] == '\0' || !obd_live_value(lv, 0xA6, &km) ||
        (odo_saved_km >= 0.0f && km < odo_saved_km + 1.0f)) {
        return;
    }
    char date[24];
    now_text(date, sizeof date);
    date[10] = '\0';
    read_small_file(ODO_PATH, text, sizeof text);
    size_t n = keyval_store_set(text, "car,km,date\n", baro_key, km, date, out, sizeof out);
    FILE *f = n > 0 ? fopen(ODO_PATH, "wb") : NULL;
    bool ok = f != NULL && fwrite(out, 1, n, f) == n;
    if (f != NULL && fclose(f) != 0) {
        ok = false;
    }
    if (odo_saved_km < 0.0f) {
        spike_logf(s, "%sLIVE: odometer %.1f km %s for this car", ok ? "" : "! ", (double)km,
                   ok ? "saved" : "not saved");
    }
    odo_saved_km = km;
}

/* GRAPH (worker): one value of a drive file, squeezed into the plot's
 * pixel columns for the requested window. Read in chunks, line by line. */
static void graph_load(spike_t *s)
{
    static graph_cols_t g;
    static drive_column_t cols[DRIVE_COL_MAX];
    static char buf[4096 + 512];
    char file[32];
    LightLock_Lock(&shared.lock);
    memcpy(file, shared.graph_req.file, sizeof file);
    file[sizeof file - 1] = '\0';
    int col = shared.graph_req.col;
    double t0 = shared.graph_req.t0, t1 = shared.graph_req.t1;
    LightLock_Unlock(&shared.lock);

    char path[96];
    snprintf(path, sizeof path, "%s/%s", DRIVES_DIR, file);
    graph_cols_init(&g, t0, t1, GRAPH_PLOT_W);
    const char *msg = NULL;
    int ncols = 0;
    FILE *f = drive_list_is_log_name(file) ? fopen(path, "rb") : NULL;
    if (f == NULL) {
        msg = "Can't open this drive.";
    } else {
        size_t got = fread(buf, 1, DRIVE_HEAD_BYTES, f);
        ncols = drive_columns(buf, got, cols, DRIVE_COL_MAX);
        if (ncols == 0) {
            msg = "Not a Gauge Boy drive log.";
        } else {
            if (col >= ncols || col < 0) {
                col = 0;
            }
            /* Lines from the start of the file; a partial last line waits
             * for the next chunk. */
            fseek(f, 0, SEEK_SET);
            size_t keep = 0;
            for (;;) {
                size_t n = fread(buf + keep, 1, 4096, f);
                size_t len = keep + n;
                size_t start = 0;
                for (size_t i = 0; i < len; i++) {
                    if (buf[i] != '\n') {
                        continue;
                    }
                    buf[i] = '\0';
                    double t;
                    float v;
                    if (drive_row_value(buf + start, col, &t, &v) && !isnan(v)) {
                        graph_cols_add(&g, t, v);
                    }
                    start = i + 1;
                }
                keep = len - start;
                if (keep > 512) {
                    keep = 0;   /* a line this long isn't ours: skip it */
                }
                memmove(buf, buf + start, keep);
                if (n == 0) {
                    break;
                }
            }
        }
        fclose(f);
    }
    LightLock_Lock(&shared.lock);
    shared.graph = g;
    memcpy(shared.graph_columns, cols, sizeof cols);
    shared.graph_ncols = ncols;
    shared.graph_col = col;
    shared.graph_loading = false;
    snprintf(shared.graph_msg, sizeof shared.graph_msg, "%s", msg != NULL ? msg : "");
    shared.graph_version++;
    LightLock_Unlock(&shared.lock);
    (void)s;
}

/* ---------- REC: drive logs (worker) ---------- */

static drive_log_t rec_log;
static FILE *rec_fp;
static uint64_t rec_flush_at;
static unsigned rec_seconds_shown;

static void rec_stop(spike_t *s, const char *why)
{
    if (rec_fp == NULL) {
        return;
    }
    bool ok = drive_log_flush(&rec_log);
    ok = fclose(rec_fp) == 0 && ok;
    rec_fp = NULL;
    LightLock_Lock(&shared.lock);
    uint64_t secs = (tp_now_ms() - shared.rec_started_ms) / 1000u;
    shared.rec_active = false;
    shared.live_ui_version++;
    char file[sizeof shared.rec_file];
    memcpy(file, shared.rec_file, sizeof file);
    LightLock_Unlock(&shared.lock);
    spike_logf(s, "%s REC stopped (%s): %s, %u rows, %u:%02u, %u KB%s", ok ? "=" : "!", why, file,
               (unsigned)rec_log.rows, (unsigned)(secs / 60), (unsigned)(secs % 60),
               (unsigned)((rec_log.bytes + 1023) / 1024), ok ? "" : " (! not all written)");
    enqueue(BTN_GARAGE_SCAN);   /* the logged distance, for 100 Miles Logged */
}

static void rec_refuse(spike_t *s, const char *msg, const char *path)
{
    spike_logf(s, msg, path);
    LightLock_Lock(&shared.lock);
    shared.rec_want = false;
    shared.live_ui_version++;
    LightLock_Unlock(&shared.lock);
}

static void rec_start(spike_t *s, const obd_live_t *lv, const obd_link_t *link)
{
    time_t now = time(NULL);
    char name[sizeof shared.rec_file];
    char path[96];
    drive_log_file_name(name, sizeof name, gmtime(&now));
    snprintf(path, sizeof path, "%s/%s", DRIVES_DIR, name);
    mkdir(DRIVES_DIR, 0777);
    rec_fp = fopen(path, "wb");
    if (rec_fp == NULL) {
        rec_refuse(s, "! REC: can't create %s (is the SD card in?)", path);
        return;
    }
    if (s->transport->connects != identified_connects) {
        identify_car(s);   /* the car for the header: just 0902 on this link */
    }
    char when[24], car[64], vin[OBD_VIN_LEN + 1];
    now_text(when, sizeof when);
    LightLock_Lock(&shared.lock);
    diag_car_text(&shared.diag, car, sizeof car);
    snprintf(vin, sizeof vin, "%s", shared.diag.have_vin ? shared.diag.vin : "");
    obd_units_t units = app_cfg->metric ? OBD_UNITS_METRIC : OBD_UNITS_US;
    LightLock_Unlock(&shared.lock);
    drive_log_meta_t meta = { GB_VERSION_TEXT, when, car, vin, elm_protocol_name(link->protocol),
                              obd_live_get(lv, 0x33) != NULL ? "PID 33" : baro_from,
                              (float)app_cfg->engine_dl / 10.0f };
    uint64_t t = tp_now_ms();
    if (!drive_log_open(&rec_log, drive_log_stdio_write, rec_fp, &meta, lv, units, 0, t)) {
        fclose(rec_fp);
        rec_fp = NULL;
        rec_refuse(s, "! REC: can't write %s (is the SD card full?)", path);
        return;
    }
    rec_flush_at = t + REC_FLUSH_MS;
    rec_seconds_shown = 0;
    LightLock_Lock(&shared.lock);
    shared.rec_active = true;
    shared.rec_started_ms = t;
    snprintf(shared.rec_file, sizeof shared.rec_file, "%s", name);
    shared.live_ui_version++;
    LightLock_Unlock(&shared.lock);
    spike_logf(s, "REC: recording %d PIDs to %s", lv->count, path);
    achieve_earn(s, ACH_ON_THE_RECORD);
}

/* After each LIVE step: start, sample, flush or stop as REC asks. */
static void rec_step(spike_t *s, const obd_live_t *lv, const obd_link_t *link)
{
    LightLock_Lock(&shared.lock);
    bool want = shared.rec_want;
    LightLock_Unlock(&shared.lock);
    if (want && rec_fp == NULL) {
        rec_start(s, lv, link);
    } else if (!want && rec_fp != NULL) {
        rec_stop(s, "REC tapped");
    }
    if (rec_fp == NULL) {
        return;
    }
    uint64_t now = tp_now_ms();
    drive_log_sample(&rec_log, lv, now);
    if (now >= rec_flush_at) {
        drive_log_flush(&rec_log);
        fflush(rec_fp);
        rec_flush_at = now + REC_FLUSH_MS;
    }
    const char *why = rec_log.failed ? "SD card write failed"
                    : rec_log.bytes >= REC_MAX_BYTES ? "file reached 50 MB" : NULL;
    if (why != NULL) {
        rec_stop(s, why);
        LightLock_Lock(&shared.lock);
        shared.rec_want = false;
        LightLock_Unlock(&shared.lock);
        return;
    }
    LightLock_Lock(&shared.lock);
    unsigned secs = (unsigned)((now - shared.rec_started_ms) / 1000u);
    if (secs != rec_seconds_shown) {
        rec_seconds_shown = secs;
        shared.live_ui_version++;   /* the REC button's time */
    }
    LightLock_Unlock(&shared.lock);
}

/* ---------- TIME ATTACK (worker) ---------- */

#define BESTS_PATH CONFIG_DIR "/" TRACK_BESTS_FILE

static ta_t ta_w;

/* This car's bests for every run, and the one for run (baro_key is set when polling starts). */
static void ta_load_best(ta_run_t run, bool metric)
{
    static char text[4096];
    read_small_file(BESTS_PATH, text, sizeof text);
    track_best_t best[TA_RUN_COUNT];
    bool have[TA_RUN_COUNT];
    for (int r = 0; r < TA_RUN_COUNT; r++) {
        have[r] = baro_key[0] != '\0' &&
                  track_bests_get(text, baro_key, ta_run_key((ta_run_t)r, metric), &best[r]);
    }
    LightLock_Lock(&shared.lock);
    shared.ta_car_known = baro_key[0] != '\0';
    for (int r = 0; r < TA_RUN_COUNT; r++) {
        shared.ta_have_bests[r] = have[r];
        if (have[r]) {
            shared.ta_bests[r] = best[r];
        }
    }
    shared.ta_have_best = have[run];
    if (have[run]) {
        shared.ta_best = best[run];
    }
    shared.ta_record = false;
    shared.ta_saved = false;
    LightLock_Unlock(&shared.lock);
}

/* A finished run: log it and keep it if it beats the best. */
static void ta_finished(spike_t *s)
{
    static char text[4096], out[4096];
    const ta_result_t *r = &ta_w.result;
    char date[24];
    now_text(date, sizeof date);
    date[10] = '\0';
    bool record = false, saved = false;
    if (baro_key[0] != '\0') {
        read_small_file(BESTS_PATH, text, sizeof text);
        float extra = r->run == TA_RUN_QUARTER ? r->trap_kmh
                    : r->run == TA_RUN_BRAKE ? r->distance_m : 0.0f;
        size_t n = track_bests_offer(text, baro_key, ta_run_key(r->run, r->metric), r->seconds,
                                     extra, date, &record, out, sizeof out);
        if (record && n > 0) {
            FILE *f = fopen(BESTS_PATH, "wb");
            saved = f != NULL && fwrite(out, 1, n, f) == n;
            if (f != NULL && fclose(f) != 0) {
                saved = false;
            }
        }
    }
    achieve_earn(s, ACH_STOPWATCH);
    spike_logf(s, "TRACK: %s %.2f s (estimate, speed read %.1f times a second)%s",
               ta_run_name(r->run, r->metric), (double)r->seconds, (double)r->hz,
               record ? (saved ? ", new best" : ", new best (! not saved)") : "");
    LightLock_Lock(&shared.lock);
    shared.ta_record = record;
    shared.ta_saved = saved;
    if (record) {
        shared.ta_have_best = true;
        shared.ta_best.seconds = r->seconds;
        shared.ta_best.extra = 0.0f;
        snprintf(shared.ta_best.date, sizeof shared.ta_best.date, "%.10s", date);
        shared.ta_have_bests[r->run] = true;
        shared.ta_bests[r->run] = shared.ta_best;
    }
    shared.ta_have_last[r->run] = true;
    shared.ta_last[r->run] = r->seconds;
    LightLock_Unlock(&shared.lock);
}

/* After each step while TIME ATTACK polls: feed the timer the new speed. */
static void ta_feed(spike_t *s, const obd_live_t *lv)
{
    static unsigned gen_seen = ~0u;
    static uint32_t reads_seen;
    LightLock_Lock(&shared.lock);
    bool on = shared.ta_on;
    unsigned gen = shared.ta_gen;
    ta_run_t run = (ta_run_t)shared.ta_want_run;
    bool metric = shared.ta_want_metric;
    bool armed = shared.ta_armed;
    LightLock_Unlock(&shared.lock);
    static char key_seen[sizeof baro_key];
    if (!on) {
        ta_off(&ta_w);
        gen_seen = ~0u;
        return;
    }
    uint64_t now = tp_now_ms();
    if (gen != gen_seen) {
        gen_seen = gen;
        if (armed) {
            ta_arm(&ta_w, run, metric, now);
            spike_logf(s, "TRACK: Time Attack %s armed", ta_run_name(run, metric));
        } else {
            ta_off(&ta_w);   /* polling speed, not timing (a run's Stop) */
            ta_w.run = run;
            ta_w.metric = metric;
        }
        reads_seen = 0;
        ta_load_best(run, metric);
        memcpy(key_seen, baro_key, sizeof key_seen);
    } else if (strcmp(key_seen, baro_key) != 0) {   /* the car became known */
        memcpy(key_seen, baro_key, sizeof key_seen);
        ta_load_best(run, metric);
    }
    if (!armed) {
        const obd_live_pid_t *sp = lv != NULL ? obd_live_get(lv, 0x0D) : NULL;
        if (sp != NULL && sp->have) {   /* for "Start while parked" */
            ta_w.have_last = true;
            ta_w.last_kmh = sp->value;
            ta_w.last_ms = sp->at_ms;
        }
        LightLock_Lock(&shared.lock);
        shared.ta = ta_w;
        shared.ta_version++;
        LightLock_Unlock(&shared.lock);
        return;
    }
    bool finished = false;
    const obd_live_pid_t *p = lv != NULL ? obd_live_get(lv, 0x0D) : NULL;
    if (p != NULL && p->have && p->reads != reads_seen) {
        reads_seen = p->reads;
        finished = ta_speed(&ta_w, p->at_ms, p->value);
    }
    ta_tick(&ta_w, now);
    if (finished) {
        ta_finished(s);
    }
    LightLock_Lock(&shared.lock);
    shared.ta = ta_w;
    shared.ta_version++;
    LightLock_Unlock(&shared.lock);
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
    obd_module_t module = (obd_module_t)shared.live_module;
    bool speed_only = shared.ta_on;
    bool no_volts = shared.ta_on || shared.shift_on || shared.gmeter_on || shared.revsync_on ||
                    shared.eco_on || shared.trip_on;
    memcpy(pids, shared.live_want, sizeof pids);
    memcpy(periods, shared.live_want_period, sizeof periods);
    LightLock_Unlock(&shared.lock);

    if (!on) {
        if (rec_fp != NULL) {
            rec_stop(s, "polling stopped");
        }
        LightLock_Lock(&shared.lock);
        shared.rec_want = false;
        LightLock_Unlock(&shared.lock);
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
        if (gen != gen_seen && rec_fp != NULL) {
            rec_stop(s, "view changed; a new file follows");   /* new columns */
        } else if (!ready && rec_fp != NULL) {
            spike_logf(s, "REC: resumed");
        }
        if (ready) {
            obd_live_restore_header(&lv, s->elm);
            spike_live_summary(s, &lv);   /* the old PID set's rates */
        }
        static obd_link_t module_link;
        static unsigned module_connects = ~0u;
        static obd_module_t module_read = OBD_MODULE_ENGINE;
        const obd_link_t *use = &link;
        if (module != OBD_MODULE_ENGINE) {
            /* Another module: its own supported PIDs, once a connection. */
            if (module_connects != s->transport->connects || module_read != module) {
                obd_link_result_t mr = obd_link_read_module(s->elm, &link, module, &module_link,
                                                            spike_trace_command, s);
                if (mr == OBD_LINK_LOST) {
                    tcp_transport_close(s->transport);
                    ready = false;
                    return;
                }
                module_connects = s->transport->connects;
                module_read = module;
                char list[3 * 0xE0 + 1];
                spike_logf(s, "LIVE: %s module: %s", obd_module_name(module),
                           mr == OBD_LINK_OK ? obd_link_pid_list(&module_link, list, sizeof list)
                                             : "no answer");
            }
            if (module_link.result != OBD_LINK_OK) {
                live_stop(link.protocol >= 6 && link.protocol <= 9
                          ? "No transmission module answered"
                          : "Transmission data needs a CAN car");
                return;
            }
            use = &module_link;
        }
        int count = obd_live_init(&lv, use, pids, n);
        for (int i = 0; i < n; i++) {
            if (periods[i] != LIVE_PERIOD_DEFAULT) {
                obd_live_set_period(&lv, pids[i], periods[i]);
            }
        }
        lv.use_count = true;
        lv.use_multi = true;   /* CAN cars: several PIDs a request */
        lv.use_engine_header = true;   /* CAN cars: one module only (the engine, or the view's) */
        lv.module = module;
        lv.no_volts = no_volts;        /* TIME ATTACK, SHIFT LIGHT: only what they show */
        uint8_t polled[OBD_LIVE_MAX];
        for (int i = 0; i < lv.count; i++) {
            polled[i] = lv.pid[i].pid;
        }
        history_reset(&live_history, polled, lv.count);
        baro_seed(s, &lv);
        odo_saved_km = -1.0f;
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
        if (rec_fp != NULL) {
            drive_log_flush(&rec_log);
            fflush(rec_fp);
            spike_logf(s, "REC: paused until the connection is back");
        }
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
    if (speed_only) {
        ta_feed(s, &lv);
    }
    history_from_live(&live_history, &lv);
    baro_save(s, &lv);
    odo_save(s, &lv);
    rec_step(s, &lv, &link);
}

/* ---------- GARAGE (worker) ---------- */

#define GARAGE_PATH        CONFIG_DIR "/" GARAGE_FILE
#define GARAGE_DRIVES_PATH CONFIG_DIR "/" GARAGE_DRIVES_FILE

static void garage_progress(const char *msg)
{
    LightLock_Lock(&shared.lock);
    snprintf(shared.garage_msg, sizeof shared.garage_msg, "%s", msg);
    shared.garage_version++;
    LightLock_Unlock(&shared.lock);
}

/* The distance in one drive file, read line by line. */
static double garage_drive_km(FILE *f, const char *head, size_t head_len)
{
    static char buf[4096 + 512 + 1];
    drive_dist_t dd;
    if (!drive_dist_begin(&dd, head, head_len)) {
        return 0.0;
    }
    fseek(f, 0, SEEK_SET);
    size_t keep = 0;
    for (;;) {
        size_t n = fread(buf + keep, 1, 4096, f);
        size_t len = keep + n, start = 0;
        for (size_t i = 0; i < len; i++) {
            if (buf[i] == '\n') {
                buf[i] = '\0';
                drive_dist_line(&dd, buf + start);
                start = i + 1;
            }
        }
        keep = len - start;
        if (keep > 512) {
            keep = 0;
        }
        memmove(buf, buf + start, keep);
        if (n == 0) {
            break;
        }
    }
    return dd.km;
}

#define TRIP_PATH    CONFIG_DIR "/" TRIP_FILE
#define MILEAGE_PATH CONFIG_DIR "/" MILEAGE_FILE

/* trip.csv and mileage.csv into shared (worker). */
static void garage_load_logs(void)
{
    static char trip[sizeof shared.trip_text], miles[sizeof shared.miles_text];
    read_small_file(TRIP_PATH, trip, sizeof trip);
    read_small_file(MILEAGE_PATH, miles, sizeof miles);
    LightLock_Lock(&shared.lock);
    memcpy(shared.trip_text, trip, sizeof trip);
    memcpy(shared.miles_text, miles, sizeof miles);
    shared.garage_version++;
    LightLock_Unlock(&shared.lock);
}

static bool write_text(const char *path, const char *text, size_t n)
{
    FILE *f = n > 0 ? fopen(path, "wb") : NULL;
    bool ok = f != NULL && fwrite(text, 1, n, f) == n;
    if (f != NULL && fclose(f) != 0) {
        ok = false;
    }
    return ok;
}

/* TRIP (worker): a new trip starts at shared.trip_km. */
static void trip_save(spike_t *s)
{
    static char text[2048], out[2048];
    char key[32], date[24];
    LightLock_Lock(&shared.lock);
    snprintf(key, sizeof key, "%s", shared.trip_key);
    float km = shared.trip_km;
    LightLock_Unlock(&shared.lock);
    now_text(date, sizeof date);
    date[10] = '\0';
    read_small_file(TRIP_PATH, text, sizeof text);
    size_t n = keyval_store_set(text, TRIP_HEADER, key, km, date, out, sizeof out);
    bool ok = write_text(TRIP_PATH, out, n);
    spike_logf(s, "%sTRIP: new trip at %.1f km%s", ok ? "" : "! ", (double)km,
               ok ? "" : " (not saved to trip.csv)");
    garage_load_logs();
}

/* MILEAGE (worker): add shared.miles_new, replace entry shared.miles_index
 * with it, or delete that entry. */
static void miles_change(spike_t *s, int action)
{
    static char text[8192], out[8192];
    char key[32];
    LightLock_Lock(&shared.lock);
    snprintf(key, sizeof key, "%s", shared.miles_key);
    mileage_entry_t e = shared.miles_new;
    int index = shared.miles_index;
    LightLock_Unlock(&shared.lock);
    read_small_file(MILEAGE_PATH, text, sizeof text);
    bool found = true;
    size_t n = action == BTN_MILES_ADD ? mileage_add(text, key, &e, out, sizeof out)
             : mileage_rewrite(text, key, index, action == BTN_MILES_SET ? &e : NULL, &found, out,
                               sizeof out);
    bool ok = found && n > 0 && write_text(MILEAGE_PATH, out, n);
    if (found && n == 0 && action == BTN_MILES_DROP) {
        static const char header[] = "car,date,start_km,end_km,fuel_l,price_l\n";
        ok = write_text(MILEAGE_PATH, header, sizeof header - 1);   /* it was the only line */
    }
    spike_logf(s, "%sMILEAGE: entry %s%s", ok ? "" : "! ",
               action == BTN_MILES_ADD ? "added" : action == BTN_MILES_SET ? "changed" : "deleted",
               ok ? "" : " (not saved to mileage.csv)");
    garage_load_logs();
}

static void garage_scan(spike_t *s)
{
    static garage_t g;
    static char text[8192], cache_old[16384], cache_new[16384];
    static char names[256][32];
    static char head[DRIVE_HEAD_BYTES];
    static struct { double km; char vin[18]; char car[64]; bool ok; } drv[256];
    LightLock_Lock(&shared.lock);
    shared.garage_scanning = true;
    char current[32];
    baro_store_key(&shared.diag, app_cfg->car_make, app_cfg->car_model, app_cfg->car_year, current,
                   sizeof current);
    snprintf(shared.garage_current, sizeof shared.garage_current, "%s", current);
    LightLock_Unlock(&shared.lock);
    garage_progress("Reading the cars...");

    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    int year = tm != NULL ? tm->tm_year + 1900 : 0;
    memset(&g, 0, sizeof g);
    garage_add(&g, current, year);
    read_small_file(BESTS_PATH, text, sizeof text);
    garage_add_from_csv(&g, text, year);
    LightLock_Lock(&shared.lock);
    snprintf(shared.garage_bests, sizeof shared.garage_bests, "%s", text);
    LightLock_Unlock(&shared.lock);
    read_small_file(BARO_PATH, text, sizeof text);
    garage_add_from_csv(&g, text, year);
    read_small_file(GARAGE_PATH, text, sizeof text);
    garage_add_from_csv(&g, text, year);
    garage_read_nicknames(&g, text);
    garage_load_logs();
    read_small_file(ODO_PATH, text, sizeof text);
    garage_add_from_csv(&g, text, year);
    for (int i = 0; i < g.count; i++) {
        float km;
        g.car[i].have_odo = baro_store_get(text, g.car[i].key, &km, g.car[i].odo_date,
                                           sizeof g.car[i].odo_date);
        g.car[i].odo_km = g.car[i].have_odo ? km : 0.0;
    }

    /* Logged distance: every drive file, from the cache when unchanged. */
    int n = 0;
    DIR *dir = opendir(DRIVES_DIR);
    if (dir != NULL) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL && n < 256) {
            if (drive_list_is_log_name(e->d_name)) {
                snprintf(names[n++], sizeof names[0], "%.31s", e->d_name);
            }
        }
        closedir(dir);
    }
    read_small_file(GARAGE_DRIVES_PATH, cache_old, sizeof cache_old);
    size_t used = 0;
    cache_new[0] = '\0';
    int read_now = 0;
    for (int i = 0; i < n; i++) {
        char path[96];
        snprintf(path, sizeof path, "%s/%.31s", DRIVES_DIR, names[i]);
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            continue;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        uint32_t sz = size > 0 ? (uint32_t)size : 0;
        double km = 0.0;
        char vin[18] = "", car[64] = "";
        if (!garage_cache_get(cache_old, names[i], sz, &km, vin, sizeof vin, car, sizeof car)) {
            char msg[48];
            snprintf(msg, sizeof msg, "Counting drive %d of %d...", i + 1, n);
            garage_progress(msg);
            size_t got = 0;
            read_part(f, 0, head, sizeof head, &got);
            drive_info_t d;
            memset(&d, 0, sizeof d);
            drive_info_parse_head(&d, head, got);
            snprintf(vin, sizeof vin, "%s", d.vin);
            snprintf(car, sizeof car, "%s", d.car);
            km = garage_drive_km(f, head, got);
            read_now++;
        }
        fclose(f);
        garage_cache_add(cache_new, sizeof cache_new, &used, names[i], sz, km, vin, car);
        drv[i].ok = true;
        drv[i].km = km;
        snprintf(drv[i].vin, sizeof drv[i].vin, "%s", vin);
        snprintf(drv[i].car, sizeof drv[i].car, "%s", car);
        if (vin[0] != '\0') {
            garage_add(&g, vin, year);   /* a drive's car with a VIN gets a card too */
        }
    }
    for (int i = 0; i < n; i++) {
        int c = drv[i].ok ? garage_find_drive(&g, drv[i].vin, drv[i].car) : -1;
        if (c >= 0) {
            g.car[c].km += drv[i].km;
            g.car[c].drives++;
        }
        drv[i].ok = false;
    }
    if (read_now > 0 || strcmp(cache_old, cache_new) != 0) {
        FILE *f = fopen(GARAGE_DRIVES_PATH, "wb");
        if (f != NULL) {
            fwrite(cache_new, 1, used, f);
            fclose(f);
        }
    }
    spike_logf(s, "GARAGE: %d car%s, %d drive%s (%d read now)", g.count, g.count == 1 ? "" : "s",
               n, n == 1 ? "" : "s", read_now);
    double km = 0.0;
    for (int i = 0; i < g.count; i++) {
        km += g.car[i].km;
    }
    if (km >= ACHIEVE_MILES_KM) {
        achieve_earn(s, ACH_100_MILES);
    }
    if (g.count >= ACHIEVE_GARAGE_CARS) {
        achieve_earn(s, ACH_FULL_GARAGE);
    }
    LightLock_Lock(&shared.lock);
    shared.garage = g;
    shared.garage_scanning = false;
    shared.garage_msg[0] = '\0';
    shared.garage_version++;
    LightLock_Unlock(&shared.lock);
}

static void garage_save(spike_t *s)
{
    static char text[4096], out[4096];
    char key[32], nick[GARAGE_NICK_MAX + 1];
    LightLock_Lock(&shared.lock);
    snprintf(key, sizeof key, "%s", shared.garage_nick_key);
    snprintf(nick, sizeof nick, "%s", shared.garage_nick);
    LightLock_Unlock(&shared.lock);
    if (key[0] == '\0') {
        return;
    }
    read_small_file(GARAGE_PATH, text, sizeof text);
    size_t n = garage_set_nickname(text, key, nick, out, sizeof out);
    FILE *f = n > 0 ? fopen(GARAGE_PATH, "wb") : NULL;
    bool ok = f != NULL && fwrite(out, 1, n, f) == n;
    if (f != NULL && fclose(f) != 0) {
        ok = false;
    }
    spike_logf(s, "%sGARAGE: nickname %s%s", ok ? "" : "! ", nick[0] ? "saved" : "removed",
               ok ? "" : " (not saved to garage.csv)");
    LightLock_Lock(&shared.lock);
    for (int i = 0; i < shared.garage.count; i++) {
        if (strcmp(shared.garage.car[i].key, key) == 0) {
            snprintf(shared.garage.car[i].nickname, sizeof shared.garage.car[i].nickname, "%s",
                     nick);
        }
    }
    shared.garage_version++;
    LightLock_Unlock(&shared.lock);
}

/* REV SYNC (worker): save a finished game's score if it's this car's
 * best, then read the best. */
static void rs_best_io(spike_t *s)
{
    static char text[4096], out[4096];
    char key[32];
    LightLock_Lock(&shared.lock);
    baro_store_key(&shared.diag, app_cfg->car_make, app_cfg->car_model, app_cfg->car_year, key,
                   sizeof key);
    int score = shared.rs_save_score;
    shared.rs_save_score = 0;
    const char *run = shared.hs_run != NULL ? shared.hs_run : "revsync";
    LightLock_Unlock(&shared.lock);
    bool eco_run = strcmp(run, "eco") == 0;
    read_small_file(BESTS_PATH, text, sizeof text);
    bool record = false, saved = false;
    if (score > 0 && key[0] != '\0') {
        char date[24];
        now_text(date, sizeof date);
        date[10] = '\0';
        size_t n = track_bests_offer_high(text, key, run, (float)score, date, &record, out,
                                          sizeof out);
        if (record && n > 0) {
            FILE *f = fopen(BESTS_PATH, "wb");
            saved = f != NULL && fwrite(out, 1, n, f) == n;
            if (f != NULL && fclose(f) != 0) {
                saved = false;
            }
            memcpy(text, out, n + 1);
        }
    }
    if (score > 0 && strcmp(eco_run ? eco_rank(score) : rs_rank(score), "S") == 0) {
        achieve_earn(s, eco_run ? ACH_HYPERMILER : ACH_REV_S);
    }
    if (score > 0) {
        spike_logf(s, "TRACK: %s %d points, rank %s%s", eco_run ? "Eco Challenge" : "Rev Sync",
                   score, eco_run ? eco_rank(score) : rs_rank(score),
                   record ? (saved ? ", new high score" : ", new high score (! not saved)") : "");
    }
    track_best_t best;
    bool have = key[0] != '\0' && track_bests_get(text, key, run, &best);
    LightLock_Lock(&shared.lock);
    shared.rs_car_known = key[0] != '\0';
    shared.rs_have_best = have;
    if (have) {
        shared.rs_best = best;
    }
    if (score > 0) {
        shared.rs_record = record;
    }
    LightLock_Unlock(&shared.lock);
}

/* SHIFT LIGHT: the shift point to config.ini (worker). */
static void shift_save(spike_t *s)
{
    LightLock_Lock(&shared.lock);
    gb_config_t copy = *app_cfg;
    LightLock_Unlock(&shared.lock);
    bool saved = config_save(&copy, CONFIG_PATH);
    spike_logf(s, "SHIFT LIGHT: shift point %u rpm%s", (unsigned)copy.shift_rpm,
               saved ? ", saved" : " (! not saved to config.ini)");
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
            } else if (action == BTN_GRAPH_LOAD) {
                graph_load(spike);
            } else if (action == BTN_LOG_SCAN) {
                log_scan(spike);
            } else if (action == BTN_LOG_DELETE) {
                log_delete(spike);
            } else if (action == BTN_SET_UNITS) {
                set_units(spike);
            } else if (action == BTN_LIVE_SAVE) {
                live_save(spike);
            } else if (action == BTN_SHIFT_SAVE) {
                shift_save(spike);
            } else if (action == BTN_RS_BEST) {
                rs_best_io(spike);
            } else if (action == BTN_GARAGE_SCAN) {
                garage_scan(spike);
            } else if (action == BTN_ACH_LOAD) {
                achieve_load();
            } else if (action >= BTN_ACH_BASE && action < BTN_ACH_BASE + ACH_COUNT) {
                achieve_earn(spike, (achieve_id_t)(action - BTN_ACH_BASE));
            } else if (action == BTN_GARAGE_SAVE) {
                garage_save(spike);
            } else if (action == BTN_TRIP_SAVE) {
                trip_save(spike);
            } else if (action == BTN_MILES_ADD || action == BTN_MILES_DROP ||
                       action == BTN_MILES_SET) {
                miles_change(spike, action);
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
    int nf = diag_facts(&shared.diag, app_cfg->metric ? OBD_UNITS_METRIC : OBD_UNITS_US, TOP_COLS,
                        top_lines, 10);
    int n = diag_items(&shared.diag, tab, items, DIAG_ITEMS_MAX);
    snprintf(empty, sizeof empty, "%s", diag_empty_text(&shared.diag, tab));
    LightLock_Unlock(&shared.lock);

    spike_format_header(header, sizeof header, connected, busy);
    if (scanning) {
        snprintf(title, sizeof title, ">> Scanning: %s...", obd_diag_step_str(step));
    } else if (home) {
        snprintf(title, sizeof title, "Gauge Boy: OBD-II Deluxe");
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
        static char wrapped[14][DIAG_VIEW_COLS + 1];
        int w = diag_wrap(items[selected].detail, TOP_COLS, 2, wrapped, 14);
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
    bool delete_dialog;        /* LOG: delete question showing */
    int graph_col;             /* GRAPH: the value shown */
    double graph_t0, graph_t1; /* GRAPH: the time window, seconds */
    double graph_len;          /* GRAPH: the drive's length */
    bool ta_dialog;            /* TIME ATTACK: closed-course notice showing */
    bool ta_warned;            /* ... accepted this session */
    int ta_run;                /* TIME ATTACK: the run picked (ta_run_t) */
    screen_t garage_back;      /* where BACK goes from GARAGE (HOME) */
    uint64_t miles_del_armed;  /* MILEAGE: DELETE tapped once, until this time */
    int miles_page;            /* MILEAGE: list page */
    int miles_sel;             /* ... selected entry, newest first; -1 = none */
    int live_scroll;           /* LIVE view list (graphics): scrolled by, px */
    bool live_touching;        /* ... a touch began on the list */
    bool live_dragged;         /* ... and moved: a scroll, not a tap */
    int live_touch_y0, live_scroll0;
    int live_row_held;         /* ... the row under it, -1 = none */
    bool live_grip;            /* ... it began on the scroll bar: the grip follows it */
    int live_grip_grab;        /* ... where on the grip it holds, px from its top */
    int live_held;             /* LIVE view (graphics): the control held (live_hit_t) */
    bool live_held_in;         /* ... and the finger is still on it */
    int diag_scroll;           /* DIAG page (graphics): its list scrolled by, px */
    int diag_held;             /* DIAG (graphics): the button or tab held (diag_hit_t) */
    bool diag_held_in;         /* ... and the finger is still on it */
    int dialog_held;           /* clear question (graphics): 1 Cancel, 2 Clear Codes held */
    bool dialog_held_in;
    bool trim_info;            /* TRIM (graphics): the Info page shows */
    int trim_held;             /* ... the control held (trim_hit_t) */
    bool trim_held_in;
    bool track_info;           /* TRACK screens (graphics): the Info page shows */
    bool ta_open;              /* TIME ATTACK (graphics): a run's screen (else the run list) */
    int track_held;            /* ... the control held (track_hit_t) */
    bool track_held_in;
    int ach_scroll;            /* ACHIEVEMENTS (graphics): the list scrolled by, px */
    bool log_open;             /* LOG (graphics): the selected drive's page (else the list) */
    bool log_info;             /* ... the Info page shows */
    int log_held;              /* ... the control held (log_hit_t) */
    bool log_held_in;
    int log_scroll;            /* ... the list scrolled by, px */
    int setup_held;            /* SETUP (graphics): the control held (setup_hit_t) */
    bool setup_held_in;
    int make_scroll;           /* ... the make picker scrolled by, px */
} ui_state_t;

static bool home_gfx;          /* HOME drawn with citro2d (else the text layout) */
static bool graph_gfx;         /* GRAPH drawn with citro2d (else a text summary) */

/* The action for a HOME card. */
static int home_item_action(int item)
{
    switch (item) {
    case HOME_GARAGE:     return BTN_HOME_GARAGE;
    case HOME_ADAPTER: return BTN_HOME_WIFI;
    case HOME_LIVE:    return BTN_MENU_LIVE;
    case HOME_DIAG:    return BTN_MENU_DIAG;
    case HOME_TRIM:    return BTN_MENU_TRIM;
    case HOME_TRACK:   return BTN_MENU_TRACK;
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
    shared.live_module = OBD_MODULE_ENGINE;   /* a view sets another after this */
    shared.live_on = true;
    shared.live_gen++;
    shared.live_msg[0] = '\0';
    shared.live_ui_version++;
}

/* TRIM: the four trims fast enough to watch, RPM and speed for the hints. */
static void trim_start(void)
{
    /* The trims, then context for recordings (MAP, MAF and load slowly;
     * the car's unsupported ones are skipped). */
    static const uint8_t pids[] = { OBD_PID_STFT_B1, OBD_PID_STFT_B2, OBD_PID_LTFT_B1,
                                    OBD_PID_LTFT_B2, 0x0C, 0x0D, OBD_PID_MAP, OBD_PID_MAP_WIDE,
                                    0x10, 0x04 };
    static const uint32_t periods[] = { 0, 0, 1000, 1000, 1000, 2000, 2000, 2000, 2000, 1000 };
    LightLock_Lock(&shared.lock);
    live_start_locked(pids, periods, (int)sizeof pids);
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

/* LIVE's Dashboard (the view list's top screen): RPM and speed fast, coolant
 * and the odometer slowly. Stops a recording (it belongs to a view). */
static void live_dash_start(void)
{
    uint8_t pids[8];
    uint32_t periods[8];
    int n = live_dash_request(pids, periods, 8);
    LightLock_Lock(&shared.lock);
    shared.rec_want = false;
    live_start_locked(pids, periods, n);
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
    shared.live_module = live_view_module(v);
    if (strcmp(app_cfg->live_view, live_view_key(v)) != 0) {
        snprintf(app_cfg->live_view, sizeof app_cfg->live_view, "%s", live_view_key(v));
        ui->live_dirty = true;
    }
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
    ui->live_view = v;
}

/* TIME ATTACK: poll speed alone and arm the run. restart: (re)start
 * polling too (entering the screen); otherwise only the run changes. */
static void ta_start_ex(int run, bool metric, bool restart, bool armed)
{
    static const uint8_t pids[] = { 0x0D };
    static const uint32_t periods[] = { 0 };
    LightLock_Lock(&shared.lock);
    shared.ta_armed = armed;
    if (restart || !shared.live_on) {
        shared.ta_on = true;
        live_start_locked(pids, periods, 1);
    }
    shared.ta_want_run = run;
    shared.ta_want_metric = metric;
    shared.ta_gen++;
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

/* Polls speed and times the run (the text screens: picking a run arms it). */
static void ta_start(int run, bool metric, bool restart)
{
    ta_start_ex(run, metric, restart, true);
}

static void ta_stop(void)
{
    LightLock_Lock(&shared.lock);
    shared.ta_on = false;
    shared.live_on = false;
    shared.live_ui_version++;
    LightLock_Unlock(&shared.lock);
}

/* SHIFT LIGHT: RPM as fast as the car allows, speed every second (to
 * keep the shift point setting for when parked). */
static void shift_start(void)
{
    static const uint8_t pids[] = { 0x0C, 0x0D };
    static const uint32_t periods[] = { 0, 1000 };
    LightLock_Lock(&shared.lock);
    shared.ta_on = false;
    shared.shift_on = true;
    live_start_locked(pids, periods, 2);
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

static void shift_stop(void)
{
    LightLock_Lock(&shared.lock);
    shared.shift_on = false;
    shared.live_on = false;
    shared.live_ui_version++;
    LightLock_Unlock(&shared.lock);
}

/* UI: ask the worker to unlock an achievement (once a session from here;
 * the worker ignores ones already unlocked). */
static void achieve_ask(achieve_id_t id)
{
    static bool asked[ACH_COUNT];
    if (!asked[id]) {
        asked[id] = true;
        enqueue(BTN_ACH_BASE + (int)id);
    }
}

/* G-METER: speed (for learning forward and the parked check), the
 * accelerometer on. */
static gmeter_t gmeter;
static bool gm_accel_on;

static void gmeter_start(void)
{
    static const uint8_t pids[] = { 0x0D };
    static const uint32_t periods[] = { 0 };
    if (gmeter.forward_sign == 0) {
        gm_init(&gmeter);   /* first time: not calibrated */
    }
    if (!gm_accel_on) {
        gm_accel_on = R_SUCCEEDED(HIDUSER_EnableAccelerometer());
    }
    LightLock_Lock(&shared.lock);
    shared.ta_on = false;
    shared.shift_on = false;
    shared.gmeter_on = true;
    live_start_locked(pids, periods, 1);
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

static void gmeter_stop(void)
{
    if (gm_accel_on) {
        HIDUSER_DisableAccelerometer();
        gm_accel_on = false;
    }
    LightLock_Lock(&shared.lock);
    shared.gmeter_on = false;
    shared.live_on = false;
    shared.live_ui_version++;
    LightLock_Unlock(&shared.lock);
}

/* G-METER, every frame: a sensor reading, and the speed when it's new. */
static void gmeter_frame(spike_t *s, uint64_t now, gm_vec_t *raw_out)
{
    static uint64_t last;
    static uint32_t reads_seen;
    accelVector v;
    hidAccelRead(&v);
    gm_vec_t raw = { (float)v.x, (float)v.y, (float)v.z };
    float dt = last != 0 && now > last ? (float)(now - last) : 16.7f;
    gm_raw_watch(&gmeter, raw, dt);
    *raw_out = gmeter.raw_avg;   /* steady: the sensor jitters a few units at rest */
    if (gmeter.cal == GM_CAL_RUNNING) {
        gm_cal_t r = gm_cal_add(&gmeter, raw);
        if (r == GM_CAL_OK) {
            spike_logf(s, "G-METER: calibrated, 1 g = %.0f raw, shake %.3f g typical, %.3f g most",
                       (double)gmeter.scale, (double)gmeter.cal_rms_g, (double)gmeter.cal_max_g);
        } else if (r == GM_CAL_MOVED) {
            spike_logf(s, "! G-METER: moved while calibrating, shake %.3f g typical (limit %.2f), "
                       "%.3f g most (limit %.2f)", (double)gmeter.cal_rms_g, (double)GM_CAL_RMS,
                       (double)gmeter.cal_max_g, (double)GM_CAL_MAX);
        } else if (r == GM_CAL_NO_SENSOR) {
            spike_logf(s, "! G-METER: no motion sensor readings (raw %d %d %d)", (int)raw.x,
                       (int)raw.y, (int)raw.z);
        }
    } else {
        gm_sample(&gmeter, raw, dt);
        if (gmeter.cal == GM_CAL_OK && gmeter.peak_total >= ACHIEVE_G) {
            achieve_ask(ACH_PULLING_GS);
        }
    }
    last = now;
    LightLock_Lock(&shared.lock);
    const obd_live_pid_t *p = shared.live_have ? obd_live_get(&shared.live, 0x0D) : NULL;
    bool fresh = p != NULL && p->have && p->reads != reads_seen;
    uint64_t at = fresh ? p->at_ms : 0;
    float kmh = fresh ? p->value : 0.0f;
    if (fresh) {
        reads_seen = p->reads;
    }
    LightLock_Unlock(&shared.lock);
    if (fresh) {
        gm_speed(&gmeter, at, kmh);
    }
}

/* REV SYNC: RPM as fast as it comes, speed twice a second (the lock). */
static rev_sync_t revsync;

static void revsync_start(int shift_rpm, uint64_t now)
{
    static const uint8_t pids[] = { 0x0C, 0x0D };
    static const uint32_t periods[] = { 0, 500 };
    rs_init(&revsync, shift_rpm, (uint32_t)now);
    LightLock_Lock(&shared.lock);
    shared.ta_on = shared.shift_on = shared.gmeter_on = false;
    shared.revsync_on = true;
    shared.hs_run = "revsync";
    shared.rs_record = false;
    live_start_locked(pids, periods, 2);
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

static void revsync_stop(void)
{
    LightLock_Lock(&shared.lock);
    shared.revsync_on = false;
    shared.live_on = false;
    shared.live_ui_version++;
    LightLock_Unlock(&shared.lock);
}

/* REV SYNC, every frame: new RPM and speed readings, the clock. Returns
 * true when a game has just finished. */
static bool revsync_frame(uint64_t now)
{
    static uint32_t rpm_seen, speed_seen;
    LightLock_Lock(&shared.lock);
    const obd_live_pid_t *r = shared.live_have ? obd_live_get(&shared.live, 0x0C) : NULL;
    const obd_live_pid_t *v = shared.live_have ? obd_live_get(&shared.live, 0x0D) : NULL;
    bool new_rpm = r != NULL && r->have && r->reads != rpm_seen;
    bool new_speed = v != NULL && v->have && v->reads != speed_seen;
    uint64_t rt = new_rpm ? r->at_ms : 0, vt = new_speed ? v->at_ms : 0;
    float rpm = new_rpm ? r->value : 0.0f, kmh = new_speed ? v->value : 0.0f;
    if (new_rpm) {
        rpm_seen = r->reads;
    }
    if (new_speed) {
        speed_seen = v->reads;
    }
    LightLock_Unlock(&shared.lock);
    rs_state_t before = revsync.state;
    if (new_speed) {
        rs_speed(&revsync, vt, kmh);
    }
    if (new_rpm) {
        rs_rpm(&revsync, rt, rpm);
    }
    rs_tick(&revsync, now);
    if (revsync.state == RS_JUDGE && revsync.grade[revsync.round] == RS_GRADE_PERFECT) {
        achieve_ask(ACH_PERFECT_SYNC);
    }
    return before != RS_DONE && revsync.state == RS_DONE;
}

/* ECO CHALLENGE: speed and RPM fast, the air flow (MAF, or MAP for
 * speed-density) fast, intake air temperature slowly. */
static eco_t eco;

static void eco_start(void)
{
    static const uint8_t pids[] = { 0x0D, 0x0C, 0x10, 0x0B, 0x87, 0x0F };
    static const uint32_t periods[] = { 0, 0, 0, 0, 0, 2000 };
    eco_reset(&eco);
    LightLock_Lock(&shared.lock);
    shared.ta_on = shared.shift_on = shared.gmeter_on = shared.revsync_on = false;
    shared.eco_on = true;
    shared.hs_run = "eco";
    shared.rs_record = false;
    live_start_locked(pids, periods, (int)sizeof pids);
    LightLock_Unlock(&shared.lock);
    LightEvent_Signal(&shared.wake);
}

static void eco_stop(void)
{
    LightLock_Lock(&shared.lock);
    shared.eco_on = false;
    shared.live_on = false;
    shared.live_ui_version++;
    LightLock_Unlock(&shared.lock);
}

/* ECO, every frame: on each new speed reading, a step with the latest
 * other values. */
static void eco_frame(float engine_l)
{
    static uint32_t speed_seen;
    eco_input_t in;
    memset(&in, 0, sizeof in);
    uint64_t at = 0;
    LightLock_Lock(&shared.lock);
    const obd_live_t *lv = &shared.live;
    const obd_live_pid_t *v = shared.live_have ? obd_live_get(lv, 0x0D) : NULL;
    bool fresh = v != NULL && v->have && v->reads != speed_seen;
    if (fresh) {
        speed_seen = v->reads;
        at = v->at_ms;
        in.have_speed = true;
        in.kmh = v->value;
        in.have_rpm = obd_live_value(lv, 0x0C, &in.rpm);
        in.have_maf = obd_live_value(lv, 0x10, &in.maf_gs);
        in.have_map = obd_live_value(lv, 0x0B, &in.map_kpa) ||
                      obd_live_value(lv, 0x87, &in.map_kpa);
        in.have_iat = obd_live_value(lv, 0x0F, &in.iat_c);
    }
    LightLock_Unlock(&shared.lock);
    if (fresh) {
        eco_step(&eco, at, &in, engine_l);
    }
}

/* The car is moving (speed read in the last few seconds above 0). */
static bool car_moving(uint64_t now)
{
    LightLock_Lock(&shared.lock);
    const obd_live_pid_t *p = shared.live_have ? obd_live_get(&shared.live, 0x0D) : NULL;
    bool moving = p != NULL && p->have && p->value >= 1.0f && now - p->at_ms < 5000;
    LightLock_Unlock(&shared.lock);
    return moving;
}

/* Big 3x5 block digits for the countdown and the times. */
static const char *big_glyph(char c)
{
    switch (c) {
    case '0': case 'O': return "#### ## ## ####";
    case '1': return " # ##  #  # ###";
    case '2': return "###  #####  ###";
    case '3': return "###  ####  ####";
    case '4': return "# ## ####  #  #";
    case '5': return "####  ###  ####";
    case '6': return "####  #### ####";
    case '7': return "###  #  #  #  #";
    case '8': return "#### ##### ####";
    case '9': return "#### ####  ####";
    case 'G': return "####  # ## ####";
    case '.': return "             # ";
    case '!': return " #  #  #     # ";
    case '-': return "      ###      ";
    default:  return "               ";
    }
}

/* text in big digits on rows row..row+4 from col (0-based), in color. */
static void big_print(int row, int col, const char *text, const char *color)
{
    for (int r = 0; r < 5; r++) {
        printf("\x1b[%d;%dH", row + r + 1, col + 1);
        for (const char *c = text; *c != '\0'; c++) {
            const char *g = big_glyph(*c);
            for (int x = 0; x < 3; x++) {
                printf("%s ", g[r * 3 + x] == '#' ? color : "\x1b[0m");
            }
            printf("\x1b[0m ");
        }
    }
    printf("\x1b[0m");
}

/* TRACK top screen: what the modes are and the rules. */
static void draw_track_top(int busy, bool connected)
{
    static const char *const text[] = {
        "TRACK  Track modes",
        "",
        "TIME ATTACK  0-60, 60-0 braking, 1/4 mile",
        "",
        "SHIFT LIGHT  LEDs that fill with RPM and flash",
        "             at your shift point",
        "",
        "G-METER      the car's G-forces from the 3DS's",
        "             own motion sensor, with peaks",
        "",
        "REV SYNC     blip the throttle to a target RPM",
        "             (parked only)",
        "ECO          fuel economy and a score for",
        "             smooth driving",
        "",
        "ACHIEVEMENTS stickers for milestones",
        "",
        "Coming in version 0.4: attract mode.",
        "",
        "For closed courses, track days and passengers",
        "only. Nothing here needs a touch while moving:",
        "pick a run parked, then just drive.",
        "No online leaderboards.",
    };
    char header[TOP_COLS + 1];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    int row = 2;
    for (size_t i = 0; i < sizeof text / sizeof text[0]; i++) {
        print_row(row++, TOP_COLS, text[i]);
    }
    while (row < TOP_ROWS) {
        print_row(row++, TOP_COLS, "");
    }
}

/* TIME ATTACK top screen: the run's state in big text, speed, best. */
static void draw_ta_top(int busy, bool connected, bool metric, uint64_t now)
{
    static ta_t ta;
    static obd_live_t lv;
    char msg[sizeof shared.live_msg];
    LightLock_Lock(&shared.lock);
    ta = shared.ta;
    lv = shared.live;
    bool have_live = shared.live_have;
    bool on = shared.live_on;
    bool car_known = shared.ta_car_known;
    bool have_best = shared.ta_have_best;
    track_best_t best = shared.ta_best;
    bool record = shared.ta_record, saved = shared.ta_saved;
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);

    char header[TOP_COLS + 1], line[TOP_COLS + 32];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    const obd_live_pid_t *sp = have_live ? obd_live_get(&lv, 0x0D) : NULL;
    if (sp != NULL && sp->hz > 0.0f) {
        snprintf(line, sizeof line, "TIME ATTACK  %s   speed %.1f/s",
                 ta_run_name((ta_run_t)ta.run, metric), (double)sp->hz);
    } else {
        snprintf(line, sizeof line, "TIME ATTACK  %s", ta_run_name((ta_run_t)ta.run, metric));
    }
    print_row(2, TOP_COLS, line);
    for (int r = 3; r < 15; r++) {
        print_row(r, TOP_COLS, "");
    }

    const char *unit = metric ? "km/h" : "mph";
    float to_unit = metric ? 1.0f : 1.0f / TA_MPH_KMH;
    char big[16] = "";
    const char *color = "\x1b[42m";   /* lime-ish: green blocks */
    const char *say = NULL, *say2 = NULL;
    switch (ta.state) {
    case TA_STOP_FIRST:
        say = "Stop the car: the timer arms when it";
        say2 = "stands still for a moment.";
        break;
    case TA_COUNTDOWN:
        snprintf(big, sizeof big, "%d", ta_countdown(&ta, now));
        color = "\x1b[43m";
        say = "Stay still...";
        break;
    case TA_GO:
        if (now - ta.state_ms < TA_GO_SHOW_MS) {
            snprintf(big, sizeof big, "GO!");
        }
        say = "Ready. Timing starts by itself when";
        say2 = "the car moves.";
        break;
    case TA_BUILD_UP:
        if (now - ta.state_ms < TA_GO_SHOW_MS) {
            snprintf(big, sizeof big, "GO!");
        }
        say = metric ? "Drive above 100 km/h, then brake" : "Drive above 60 mph, then brake";
        say2 = "to a stop. Timing starts by itself.";
        break;
    case TA_BRAKE_READY:
        say = "Ready. Timing starts when you slow";
        say2 = metric ? "under 100 km/h." : "under 60 mph.";
        break;
    case TA_RUNNING: {
        double t = ((double)now - ta.start_ms) / 1000.0;
        snprintf(big, sizeof big, "%.1f", t < 0.0 ? 0.0 : t);
        color = "\x1b[46m";
        say = "Timing...";
        break;
    }
    case TA_DONE:
        break;
    default:
        say = "Starting...";
        break;
    }
    if (ta.state == TA_DONE || (ta.result.have && ta.state != TA_RUNNING &&
                                ta.state != TA_COUNTDOWN && big[0] == '\0')) {
        const ta_result_t *r = &ta.result;
        snprintf(big, sizeof big, "%.2f", (double)r->seconds);
        color = ta.state == TA_DONE && record ? "\x1b[45m" : "\x1b[42m";
        if (ta.state == TA_DONE) {
            say = record ? "NEW RECORD!" : "Result (an estimate)";
        } else if (say2 == NULL) {
            say2 = "Last result above.";
        }
        if (r->run == TA_RUN_QUARTER) {
            snprintf(line, sizeof line, "Trap %.0f %s, %s %.2f s", (double)(r->trap_kmh * to_unit),
                     unit, metric ? "0-100" : "0-60", (double)r->split_s);
            print_row(12, TOP_COLS, line);
        } else if (r->run == TA_RUN_BRAKE) {
            snprintf(line, sizeof line, "Stopping distance %.0f %s",
                     (double)(metric ? r->distance_m : r->distance_m * 3.28084f),
                     metric ? "m" : "ft");
            print_row(12, TOP_COLS, line);
        }
        snprintf(line, sizeof line, "Estimate: speed read %.1f times a second.",
                 (double)r->hz);
        print_row(13, TOP_COLS, line);
    }
    if (big[0] != '\0') {
        int width = (int)strlen(big) * 4;
        big_print(4, (TOP_COLS - width) / 2, big, color);
        if (ta.state == TA_DONE || ta.state == TA_RUNNING) {
            printf("\x1b[%d;%dHs", 8 + 1, (TOP_COLS - width) / 2 + width + 1);
        }
    }
    print_row(10, TOP_COLS, say != NULL ? say : "");
    print_row(11, TOP_COLS, say2 != NULL ? say2 : "");
    if (ta.note != NULL && ta.state != TA_DONE) {
        snprintf(line, sizeof line, "\x1b[33m%s\x1b[0m", ta.note);
        printf("\x1b[%d;1H%s", 3 + 1, line);
    }

    if (!on && msg[0] != '\0') {
        print_row(15, TOP_COLS, msg);
    } else if (have_live && lv.count == 0) {
        print_row(15, TOP_COLS, "This car doesn't report speed (PID 0D).");
    } else if (msg[0] != '\0') {
        print_row(15, TOP_COLS, msg);
    } else if (sp != NULL && sp->have) {
        snprintf(line, sizeof line, "Speed  %.0f %s", (double)(sp->value * to_unit), unit);
        print_row(15, TOP_COLS, line);
    } else {
        print_row(15, TOP_COLS, "Speed  -");
    }
    if (!car_known) {
        print_row(17, TOP_COLS, "Best: not saved (car unknown: SETUP > VEHICLE)");
    } else if (have_best) {
        snprintf(line, sizeof line, "Best  %.2f s  (%s)%s", (double)best.seconds, best.date,
                 record && !saved ? "  ! not saved" : "");
        print_row(17, TOP_COLS, line);
    } else {
        print_row(17, TOP_COLS, "Best  none yet");
    }
    for (int r = 18; r < TOP_ROWS - 2; r++) {
        print_row(r, TOP_COLS, "");
    }
    print_row(TOP_ROWS - 2, TOP_COLS, "Closed course only. Results are estimates.");
    print_row(TOP_ROWS - 1, TOP_COLS, "No touch needed while moving.");
}

/* SHIFT LIGHT top screen: the LED row, RPM in big digits, the shift point. */
static void draw_shift_top(int busy, bool connected, int shift_rpm, uint64_t now)
{
    static shift_light_t sl;
    static uint32_t reads_seen;
    static obd_live_t lv;
    char msg[sizeof shared.live_msg];
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have_live = shared.live_have;
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);

    if (sl.shift_rpm != shift_rpm) {
        shift_init(&sl, shift_rpm);
        reads_seen = 0;
    }
    const obd_live_pid_t *rp = have_live ? obd_live_get(&lv, 0x0C) : NULL;
    if (rp != NULL && rp->have && rp->reads != reads_seen) {
        reads_seen = rp->reads;
        shift_reading(&sl, rp->at_ms, rp->value);
    }
    bool fresh = rp != NULL && rp->have && now - rp->at_ms < 2000;
    float rpm = fresh ? shift_rpm_now(&sl, now) : 0.0f;
    int lit = fresh ? shift_update(&sl, rpm) : 0;
    if (sl.flashing) {
        achieve_ask(ACH_REDLINE);
    }
    bool on = !sl.flashing || shift_flash_on(now);

    char header[TOP_COLS + 1], line[TOP_COLS + 32];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf("\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    if (rp != NULL && rp->hz > 0.0f) {
        snprintf(line, sizeof line, "SHIFT LIGHT  shift at %d rpm   RPM %.1f/s", shift_rpm,
                 (double)rp->hz);
    } else {
        snprintf(line, sizeof line, "SHIFT LIGHT  shift at %d rpm", shift_rpm);
    }
    print_row(2, TOP_COLS, line);
    print_row(3, TOP_COLS, "");

    /* The LEDs: SHIFT_LEDS blocks 4 wide with a 1-column gap, 3 rows tall. */
    static const char *const color[] = { "\x1b[42m", "\x1b[43m", "\x1b[41m" };
    for (int r = 0; r < 3; r++) {
        printf("\x1b[%d;1H", 4 + r + 1);
        for (int i = 0; i < SHIFT_LEDS; i++) {
            bool led = i < lit && on;
            const char *c = sl.flashing ? "\x1b[41m" : color[shift_led_color(i)];
            if (led) {
                printf("%s    \x1b[0m", c);
            } else {
                printf("\x1b[0m%s", r == 1 ? " -- " : "    ");
            }
            if (i < SHIFT_LEDS - 1) {
                printf(" ");
            }
        }
    }
    printf("\x1b[0m");
    print_row(7, TOP_COLS, "");

    for (int r = 8; r < 16; r++) {
        print_row(r, TOP_COLS, "");
    }
    if (fresh) {
        char big[16];
        snprintf(big, sizeof big, "%d", (int)(rpm / 10.0f + 0.5f) * 10);
        int width = (int)strlen(big) * 4;
        big_print(9, (TOP_COLS - width) / 2, big, sl.flashing ? "\x1b[41m" : "\x1b[47m");
        printf("\x1b[%d;%dHrpm", 13 + 1, (TOP_COLS - width) / 2 + width + 1);
        print_row(15, TOP_COLS, sl.flashing ? "                    SHIFT!" : "");
    } else if (msg[0] != '\0') {
        print_row(11, TOP_COLS, msg);
    } else if (have_live && lv.count == 0) {
        print_row(11, TOP_COLS, "This car doesn't report RPM (PID 0C).");
    } else {
        print_row(11, TOP_COLS, "Waiting for RPM...");
    }
    for (int r = 16; r < TOP_ROWS - 2; r++) {
        print_row(r, TOP_COLS, "");
    }
    snprintf(line, sizeof line, "The LEDs light from %d rpm and flash at %d.",
             shift_rpm * SHIFT_START_PCT / 100, shift_rpm);
    print_row(TOP_ROWS - 2, TOP_COLS, line);
    print_row(TOP_ROWS - 1, TOP_COLS, "Display only: no touch needed while moving.");
}

/* G-METER top screen: a friction circle (1 g at the edge, a ring at
 * 0.5 g), the values and the peaks. */
static void draw_gmeter_top(int busy, bool connected, gm_vec_t raw, bool full)
{
    const gmeter_t *gm = &gmeter;
    char header[TOP_COLS + 1], line[TOP_COLS + 32];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf(full ? "\x1b[0m\x1b[2J" : "\x1b[0m");   /* first draw: nothing left from before */
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    print_row(2, TOP_COLS, "G-METER");

    enum { R = 10, CX = 11, CY = 15 };   /* 1-based console cell of the centre */
    bool ok = gm->cal == GM_CAL_OK && gm->have;
    int bx = 0, by = 0;
    if (ok) {
        float x = gm->lat, y = gm->lon;
        float d = sqrtf(x * x + y * y);
        if (d > 1.0f) {   /* past 1 g: pinned to the edge */
            x /= d;
            y /= d;
        }
        bx = (int)lroundf(x * R);
        by = (int)lroundf(-y * R);
    }
    print_row(3, TOP_COLS, "");
    printf("\x1b[%d;%dHACCEL", CY - R - 1, CX - 2);
    for (int dy = -R; dy <= R; dy++) {
        printf("\x1b[%d;1H", CY + dy);
        for (int dx = -R - 1; dx <= R + 1; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            if (ok && dx == bx && dy == by) {
                printf("\x1b[42;30mO\x1b[0m");
            } else if (fabsf(d - R) < 0.5f || fabsf(d - R / 2) < 0.5f) {
                printf("\x1b[32m.\x1b[0m");
            } else if (dx == 0 && dy == 0) {
                printf("+");
            } else if (dx == 0 || dy == 0) {
                printf("\x1b[2m%c\x1b[0m", dx == 0 ? ':' : '-');
            } else {
                printf(" ");
            }
        }
        printf("  ");
    }
    printf("\x1b[%d;%dHBRAKE", CY + R + 1, CX - 2);
    printf("\x1b[%d;1HL\x1b[%d;%dHR", CY + 1, CY + 1, CX + R + 2);

    /* The values, right of the circle. */
    int col = 26;
    for (int r = 3; r < TOP_ROWS; r++) {
        printf("\x1b[%d;%dH%*s", r + 1, col, TOP_COLS - col + 1, "");
    }
    int row = 5;
    if (gm->cal == GM_CAL_RUNNING) {
        printf("\x1b[%d;%dHCalibrating:", row++, col);
        printf("\x1b[%d;%dHhold still... %d%%", row++, col,
               gm->cal_n * 100 / GM_CAL_SAMPLES);
    } else if (gm->cal == GM_CAL_NONE) {
        printf("\x1b[%d;%dHMount the 3DS,", row++, col);
        printf("\x1b[%d;%dHthen CALIBRATE.", row++, col);
    } else if (gm->cal == GM_CAL_MOVED) {
        printf("\x1b[%d;%dH\x1b[33mIt moved: hold it\x1b[0m", row++, col);
        printf("\x1b[%d;%dH\x1b[33mstill, CALIBRATE\x1b[0m", row++, col);
        snprintf(line, sizeof line, "(shook %.2f g)", (double)gm->cal_max_g);
        printf("\x1b[%d;%dH%s", row++, col, line);
    } else if (gm->cal == GM_CAL_NO_SENSOR) {
        printf("\x1b[%d;%dH\x1b[33mNo motion sensor\x1b[0m", row++, col);
        printf("\x1b[%d;%dH\x1b[33mreadings.\x1b[0m", row++, col);
    } else if (ok) {
        snprintf(line, sizeof line, "Side  %.2f g %s", (double)fabsf(gm->lat),
                 gm->lat > 0.02f ? "R" : gm->lat < -0.02f ? "L" : "");
        printf("\x1b[%d;%dH%s", row++, col, line);
        snprintf(line, sizeof line, "Long  %.2f g %s", (double)fabsf(gm->lon),
                 gm->lon > 0.02f ? "acc" : gm->lon < -0.02f ? "brk" : "");
        printf("\x1b[%d;%dH%s", row++, col, line);
        snprintf(line, sizeof line, "Total %.2f g", (double)gm_total(gm));
        printf("\x1b[%d;%dH%s", row++, col, line);
        row++;
        printf("\x1b[%d;%dHPeaks", row++, col);
        snprintf(line, sizeof line, "Left  %.2f Right %.2f", (double)fabsf(gm->peak_left),
                 (double)fabsf(gm->peak_right));
        printf("\x1b[%d;%dH%s", row++, col, line);
        snprintf(line, sizeof line, "Accel %.2f Brake %.2f", (double)fabsf(gm->peak_accel),
                 (double)fabsf(gm->peak_brake));
        printf("\x1b[%d;%dH%s", row++, col, line);
        snprintf(line, sizeof line, "Total %.2f g", (double)gm->peak_total);
        printf("\x1b[%d;%dH%s", row++, col, line);
        row++;
        printf("\x1b[%d;%dH%s", row++, col,
               gm->forward_learned ? "Forward: learned" : "Forward: guessed");
        if (!gm->forward_learned) {
            printf("\x1b[%d;%dH(learned on the", row++, col);
            printf("\x1b[%d;%dH first launch)", row++, col);
        }
    }
    snprintf(line, sizeof line, "sensor %d %d %d", (int)lroundf(raw.x), (int)lroundf(raw.y),
             (int)lroundf(raw.z));
    printf("\x1b[%d;%dH%s", TOP_ROWS - 3, col, line);
    print_row(TOP_ROWS - 1, TOP_COLS, "Display only: no touch needed while moving.");
}

/* REV SYNC top screen: the tach bar with the target, big text for the
 * round, the rounds so far. */
static void draw_revsync_top(int busy, bool connected, bool full)
{
    const rev_sync_t *rs = &revsync;
    LightLock_Lock(&shared.lock);
    bool car_known = shared.rs_car_known, have_best = shared.rs_have_best;
    track_best_t best = shared.rs_best;
    bool record = shared.rs_record;
    char msg[sizeof shared.live_msg];
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);

    char header[TOP_COLS + 1], line[TOP_COLS + 32];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf(full ? "\x1b[0m\x1b[2J" : "\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    bool playing = rs->state >= RS_READY && rs->state <= RS_DONE;
    if (playing) {
        snprintf(line, sizeof line, "REV SYNC   round %d/%d   score %d%s", rs->round + 1, RS_ROUNDS,
                 rs->score, rs->combo > 1 ? "   COMBO!" : "");
    } else {
        snprintf(line, sizeof line, "REV SYNC   parked only");
    }
    print_row(2, TOP_COLS, line);

    /* The tach: 0 to 7000 rpm over 48 columns. */
    enum { W = 48, MAXR = 7000 };
    int target = playing ? rs->target[rs->round] : 0;
    int tc = target * W / MAXR;
    int rc = rs->have_rpm ? (int)(rs->rpm * W / MAXR) : 0;
    int pc = rs->state >= RS_GO && rs->state <= RS_JUDGE && rs->peak > 0.0f
           ? (int)(rs->peak * W / MAXR) : -1;
    rc = rc > W ? W : rc;
    pc = pc > W - 1 ? W - 1 : pc;
    print_row(3, TOP_COLS, "");
    if (target > 0) {
        snprintf(line, sizeof line, "TARGET %d", target);
        int lc = tc - (int)strlen(line) / 2;
        lc = lc < 0 ? 0 : lc > TOP_COLS - (int)strlen(line) ? TOP_COLS - (int)strlen(line) : lc;
        print_row(4, TOP_COLS, "");
        printf("\x1b[%d;%dH\x1b[33m%s\x1b[0m", 4 + 1, lc + 1, line);
        print_row(5, TOP_COLS, "");
        printf("\x1b[%d;%dH\x1b[33mv\x1b[0m", 5 + 1, tc + 1);
    } else {
        print_row(4, TOP_COLS, "");
        print_row(5, TOP_COLS, "");
    }
    for (int r = 0; r < 2; r++) {
        printf("\x1b[%d;1H", 6 + r + 1);
        for (int c = 0; c < W; c++) {
            bool lit = c < rc;
            if (c == tc && target > 0) {
                printf("\x1b[43m \x1b[0m");   /* the target */
            } else if (lit) {
                printf("%s \x1b[0m", c < tc || target == 0 ? "\x1b[42m" : "\x1b[41m");
            } else {
                printf("\x1b[0m%c", r == 1 ? '_' : ' ');
            }
        }
    }
    print_row(8, TOP_COLS, "");
    if (pc >= 0) {
        printf("\x1b[%d;%dH\x1b[36m^ %d\x1b[0m", 8 + 1, pc + 1, (int)lroundf(rs->peak));
    }
    print_row(9, TOP_COLS, "0      1      2      3      4      5      6     7");
    for (int r = 10; r < 24; r++) {
        print_row(r, TOP_COLS, "");
    }

    char big[16] = "";
    const char *color = "\x1b[43m";
    const char *say = "", *say2 = "";
    switch (rs->state) {
    case RS_LOCKED:
        say = rs->note != NULL ? rs->note : "Waiting for the car's speed...";
        say2 = "Rev Sync runs only while the speed reads 0.";
        break;
    case RS_IDLE:
        say = "Parked. Tap START to play 5 rounds.";
        say2 = "Engine warm, in neutral or park.";
        break;
    case RS_READY:
        snprintf(big, sizeof big, "%d", target);
        say = "Get ready: blip to this RPM at GO.";
        break;
    case RS_GO:
        snprintf(big, sizeof big, "GO!");
        color = "\x1b[42m";
        say = "Blip the throttle to the target!";
        break;
    case RS_JUDGE: {
        int r = rs->round;
        snprintf(big, sizeof big, "%d", rs->peak_of[r]);
        color = rs->grade[r] >= RS_GRADE_GREAT ? "\x1b[42m" : rs->grade[r] == RS_GRADE_MISS
              ? "\x1b[41m" : "\x1b[43m";
        snprintf(line, sizeof line, "%s!  +%d%s", rs_grade_name(rs->grade[r]), rs->points,
                 rs->combo > 1 && rs->grade[r] >= RS_GRADE_GREAT ? "  combo" : "");
        say = line;
        if (rs->peak_of[r] == 0) {
            big[0] = '\0';
            say2 = "No blip in time.";
        }
        break;
    }
    case RS_DONE:
        snprintf(big, sizeof big, "%d", rs->score);
        color = record ? "\x1b[45m" : "\x1b[42m";
        snprintf(line, sizeof line, "RANK %s%s", rs_rank(rs->score), record ? "   NEW HIGH SCORE!" : "");
        say = line;
        say2 = "Tap AGAIN to play again.";
        break;
    }
    if (big[0] != '\0') {
        int width = (int)strlen(big) * 4;
        big_print(11, (TOP_COLS - width) / 2, big, color);
    }
    print_row(17, TOP_COLS, say);
    print_row(18, TOP_COLS, say2);
    if (!rs->have_rpm && msg[0] != '\0') {
        print_row(18, TOP_COLS, msg);
    }
    if (playing) {
        for (int i = 0; i < RS_ROUNDS && i <= rs->round; i++) {
            if (i == rs->round && rs->state != RS_JUDGE && rs->state != RS_DONE) {
                break;
            }
            snprintf(line, sizeof line, "%d. target %4d  blip %4d  %s", i + 1, rs->target[i],
                     rs->peak_of[i], rs_grade_name(rs->grade[i]));
            print_row(19 + i, TOP_COLS, line);
        }
    }
    if (!car_known) {
        print_row(25, TOP_COLS, "High score: not saved (car unknown)");
    } else if (have_best) {
        snprintf(line, sizeof line, "High score %d  (%s)", (int)best.seconds, best.date);
        print_row(25, TOP_COLS, line);
    } else {
        print_row(25, TOP_COLS, "High score: none yet");
    }
    for (int r = 26; r < TOP_ROWS - 1; r++) {
        print_row(r, TOP_COLS, "");
    }
    print_row(TOP_ROWS - 1, TOP_COLS, "Parked only: it stops if the car moves.");
}

/* ECO CHALLENGE top screen: the score and rank, economy now and for the
 * trip, the trip's numbers, a tip. */
static void draw_eco_top(int busy, bool connected, const gb_config_t *cfg, uint64_t now,
                         bool full)
{
    static obd_live_t lv;
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have_live = shared.live_have;
    bool car_known = shared.rs_car_known, have_best = shared.rs_have_best;
    track_best_t best = shared.rs_best;
    char msg[sizeof shared.live_msg];
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);

    char header[TOP_COLS + 1], line[TOP_COLS + 32];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf(full ? "\x1b[0m\x1b[2J" : "\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    bool maf = have_live && obd_live_get(&lv, 0x10) != NULL;
    bool map = have_live && (obd_live_get(&lv, 0x0B) != NULL || obd_live_get(&lv, 0x87) != NULL);
    if (!have_live) {
        snprintf(line, sizeof line, "ECO CHALLENGE");
    } else if (maf) {
        snprintf(line, sizeof line, "ECO CHALLENGE   fuel from the MAF");
    } else if (map && cfg->engine_dl != 0) {
        snprintf(line, sizeof line, "ECO CHALLENGE   fuel from MAP, %u.%u L",
                 (unsigned)(cfg->engine_dl / 10), (unsigned)(cfg->engine_dl % 10));
    } else {
        snprintf(line, sizeof line, "ECO CHALLENGE   no fuel estimate");
    }
    print_row(2, TOP_COLS, line);
    print_row(3, TOP_COLS, "");

    int score = eco_score(&eco);
    bool ranked = eco_ranked(&eco);
    const char *rank = eco_rank(score);
    const char *color = !ranked ? "\x1b[47m" : rank[0] == 'S' ? "\x1b[45m"
                      : rank[0] == 'A' ? "\x1b[42m" : rank[0] == 'B' ? "\x1b[43m" : "\x1b[41m";
    for (int r = 4; r < 10; r++) {
        print_row(r, TOP_COLS, "");
    }
    snprintf(line, sizeof line, "%d", score);
    big_print(4, 6, line, color);
    if (ranked) {
        printf("\x1b[%d;30HRANK %s", 5 + 1, rank);
    } else {
        printf("\x1b[%d;30HKeep driving:", 5 + 1);
        printf("\x1b[%d;30Hranked after 1 min", 6 + 1);
        printf("\x1b[%d;30Hand 0.5 km", 7 + 1);
    }

    bool us = !cfg->metric;
    float l100 = 0.0f;
    if (eco_now_l100(&eco, &l100)) {
        if (us) {
            snprintf(line, sizeof line, "Now   %5.1f mpg", (double)(ECO_L100_PER_MPG / l100));
        } else {
            snprintf(line, sizeof line, "Now   %5.1f L/100 km", (double)l100);
        }
    } else if (eco.have_lph) {
        if (us) {
            snprintf(line, sizeof line, "Now   %5.2f gal/h", (double)(eco.lph / ECO_L_PER_GAL));
        } else {
            snprintf(line, sizeof line, "Now   %5.2f L/h", (double)eco.lph);
        }
    } else {
        snprintf(line, sizeof line, "Now   -");
    }
    print_row(10, TOP_COLS, line);
    double dist = us ? eco.dist_km / 1.609344 : eco.dist_km;
    double fuel = us ? eco.fuel_l / ECO_L_PER_GAL : eco.fuel_l;
    if (eco_avg_l100(&eco, &l100)) {
        if (us) {
            snprintf(line, sizeof line, "Trip  %5.1f mpg   %.1f mi   %.2f gal",
                     (double)(ECO_L100_PER_MPG / l100), dist, fuel);
        } else {
            snprintf(line, sizeof line, "Trip  %5.1f L/100 km   %.1f km   %.2f L", (double)l100,
                     dist, fuel);
        }
    } else {
        snprintf(line, sizeof line, "Trip  %.1f %s", dist, us ? "mi" : "km");
    }
    print_row(11, TOP_COLS, line);
    snprintf(line, sizeof line, "Driving %d:%02d   idling %d:%02d", (int)eco.moving_s / 60,
             (int)eco.moving_s % 60, (int)eco.idle_s / 60, (int)eco.idle_s % 60);
    print_row(12, TOP_COLS, line);
    snprintf(line, sizeof line, "Hard starts %d   hard stops %d", eco.harsh_accel,
             eco.harsh_brake);
    print_row(13, TOP_COLS, line);
    print_row(14, TOP_COLS, "");
    if (eco.tip != ECO_TIP_NONE && now - eco.tip_ms < 10000) {
        print_row(15, TOP_COLS, "");
        printf("\x1b[%d;1H\x1b[33m%s\x1b[0m", 15 + 1, eco_tip_text(eco.tip));
    } else {
        print_row(15, TOP_COLS, "");
    }
    if (have_live && !maf && !(map && cfg->engine_dl != 0)) {
        print_row(16, TOP_COLS, map ? "Set the engine size in SETUP > VEHICLE"
                                    : "This car reports neither MAF nor MAP.");
        print_row(17, TOP_COLS, map ? "for a fuel estimate; the score works now."
                                    : "The score still works.");
    } else {
        print_row(16, TOP_COLS, msg);
        print_row(17, TOP_COLS, "");
    }
    for (int r = 18; r < TOP_ROWS - 3; r++) {
        print_row(r, TOP_COLS, "");
    }
    if (!car_known) {
        print_row(TOP_ROWS - 3, TOP_COLS, "Best: not saved (car unknown)");
    } else if (have_best) {
        snprintf(line, sizeof line, "Best trip score %d  (%s)", (int)best.seconds, best.date);
        print_row(TOP_ROWS - 3, TOP_COLS, line);
    } else {
        print_row(TOP_ROWS - 3, TOP_COLS, "Best trip score: none yet");
    }
    print_row(TOP_ROWS - 2, TOP_COLS, "Fuel figures are estimates.");
    print_row(TOP_ROWS - 1, TOP_COLS, "Display only: no touch needed while moving.");
}

/* ACHIEVEMENTS top screen: every sticker, unlocked or how to get it. */
static void draw_achieve_top(int busy, bool connected, bool full)
{
    static char text[sizeof shared.ach_text];
    LightLock_Lock(&shared.lock);
    memcpy(text, shared.ach_text, sizeof text);
    LightLock_Unlock(&shared.lock);
    char header[TOP_COLS + 1], line[TOP_COLS + 64];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf(full ? "\x1b[0m\x1b[2J" : "\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    snprintf(line, sizeof line, "ACHIEVEMENTS   %d of %d unlocked", achieve_count(text), ACH_COUNT);
    print_row(2, TOP_COLS, line);
    print_row(3, TOP_COLS, "");
    int row = 4;
    for (int i = 0; i < ACH_COUNT && row < TOP_ROWS - 1; i++) {
        const achieve_info_t *a = achieve_info((achieve_id_t)i);
        char date[16];
        print_row(row, TOP_COLS, "");
        print_row(row + 1, TOP_COLS, "");
        if (achieve_unlocked(text, (achieve_id_t)i, date, sizeof date)) {
            printf("\x1b[%d;1H\x1b[42;30m*\x1b[0m \x1b[32m%-28.28s\x1b[0m %s", row + 1, a->name,
                   date);
        } else {
            printf("\x1b[%d;1H\x1b[2m-\x1b[0m %-28.28s locked", row + 1, a->name);
        }
        printf("\x1b[%d;3H\x1b[2m%.46s\x1b[0m", row + 2, a->how);
        row += 2;
    }
    while (row < TOP_ROWS) {
        print_row(row++, TOP_COLS, "");
    }
}

/* GARAGE top screen: one car's card. */
static void draw_garage_top(int busy, bool connected, int index, bool metric, bool full)
{
    static garage_t g;
    static char bests[sizeof shared.garage_bests];
    LightLock_Lock(&shared.lock);
    g = shared.garage;
    memcpy(bests, shared.garage_bests, sizeof bests);
    char current[32], msg[48];
    snprintf(current, sizeof current, "%s", shared.garage_current);
    snprintf(msg, sizeof msg, "%s", shared.garage_msg);
    bool scanning = shared.garage_scanning;
    LightLock_Unlock(&shared.lock);

    char header[TOP_COLS + 1], line[TOP_COLS + 48];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf(full ? "\x1b[0m\x1b[2J" : "\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    for (int r = 2; r < TOP_ROWS; r++) {
        print_row(r, TOP_COLS, "");
    }
    if (g.count == 0) {
        print_row(2, TOP_COLS, "GARAGE");
        print_row(4, TOP_COLS, scanning ? msg : "No cars yet. Connect to a car (or set it in");
        if (!scanning) {
            print_row(5, TOP_COLS, "SETUP > VEHICLE), record a drive or set a best.");
        }
        return;
    }
    if (index >= g.count) {
        index = g.count - 1;
    }
    const garage_car_t *c = &g.car[index];
    snprintf(line, sizeof line, "GARAGE   car %d of %d%s", index + 1, g.count,
             strcmp(c->key, current) == 0 ? "   (connected)" : "");
    print_row(2, TOP_COLS, line);

    /* The picture: a frame for now (the photo and mascot come with M5). */
    static const char *const frame[] = {
        "+----------------+", "|                |", "|   photo with   |", "|  the 3DS camera|",
        "|  comes in v0.5 |", "|                |", "+----------------+",
    };
    for (int i = 0; i < 7; i++) {
        printf("\x1b[%d;1H\x1b[36m%s\x1b[0m", 4 + i + 1, frame[i]);
    }
    int col = 21;
    const char *name = c->nickname[0] != '\0' ? c->nickname : c->title;
    printf("\x1b[%d;%dH\x1b[33m%.28s\x1b[0m", 4 + 1, col, name);
    int row = 5;
    if (c->nickname[0] != '\0') {
        printf("\x1b[%d;%dH%.28s", ++row, col, c->title);
    }
    if (c->vin[0] != '\0') {
        printf("\x1b[%d;%dHVIN %s", ++row, col, c->vin);
    } else {
        printf("\x1b[%d;%dH(set in SETUP)", ++row, col);
    }
    double dist = metric ? c->km : c->km / 1.609344;
    snprintf(line, sizeof line, "Logged %.1f %s", dist, metric ? "km" : "mi");
    printf("\x1b[%d;%dH%.28s", row + 2, col, line);
    snprintf(line, sizeof line, "in %d drive%s", c->drives, c->drives == 1 ? "" : "s");
    printf("\x1b[%d;%dH%.28s", row + 3, col, line);
    if (c->have_odo) {
        char odo[24];
        obd_format_grouped((long long)((metric ? c->odo_km : c->odo_km / 1.609344) + 0.5), odo,
                           sizeof odo);
        snprintf(line, sizeof line, "Odometer %s %s", odo, metric ? "km" : "mi");
        printf("\x1b[%d;%dH%.28s", row + 4, col, line);
    }

    print_row(12, TOP_COLS, "Personal bests");
    static const struct { const char *run; const char *name; } runs[] = {
        { "0-60", "0-60 mph" }, { "0-100", "0-100 km/h" }, { "60-0", "60-0 mph" },
        { "100-0", "100-0 km/h" }, { "quarter", "1/4 mile" }, { "revsync", "Rev Sync" },
        { "eco", "Eco trip" },
    };
    row = 13;
    for (size_t i = 0; i < sizeof runs / sizeof runs[0]; i++) {
        track_best_t b;
        if (!track_bests_get(bests, c->key, runs[i].run, &b)) {
            continue;
        }
        char extra[24] = "";
        if (strcmp(runs[i].run, "quarter") == 0 && b.extra > 0.0f) {
            snprintf(extra, sizeof extra, "%.0f %s", (double)(metric ? b.extra : b.extra / 1.609344f),
                     metric ? "km/h" : "mph");
        } else if ((strcmp(runs[i].run, "60-0") == 0 || strcmp(runs[i].run, "100-0") == 0) &&
                   b.extra > 0.0f) {
            snprintf(extra, sizeof extra, "%.0f %s", (double)(metric ? b.extra : b.extra * 3.28084f),
                     metric ? "m" : "ft");
        }
        if (strcmp(runs[i].run, "revsync") == 0) {
            snprintf(line, sizeof line, "  %-11s %6d  rank %-2s       %s", runs[i].name,
                     (int)b.seconds, rs_rank((int)b.seconds), b.date);
        } else if (strcmp(runs[i].run, "eco") == 0) {
            snprintf(line, sizeof line, "  %-11s %6d  rank %-2s       %s", runs[i].name,
                     (int)b.seconds, eco_rank((int)b.seconds), b.date);
        } else {
            snprintf(line, sizeof line, "  %-11s %6.2f s %-12s %s", runs[i].name, (double)b.seconds,
                     extra, b.date);
        }
        print_row(row++, TOP_COLS, line);
    }
    if (row == 13) {
        print_row(row++, TOP_COLS, "  none yet: TIME ATTACK, REV SYNC, ECO");
    }
    if (scanning) {
        print_row(TOP_ROWS - 2, TOP_COLS, msg);
    }
}

/* GARAGE: type a nickname for card index (UI thread; the keyboard covers
 * both screens). True if it changed. */
static bool edit_nickname(int index)
{
    char key[32], text[GARAGE_NICK_MAX + 1];
    LightLock_Lock(&shared.lock);
    bool ok = index >= 0 && index < shared.garage.count;
    if (ok) {
        snprintf(key, sizeof key, "%s", shared.garage.car[index].key);
        snprintf(text, sizeof text, "%s", shared.garage.car[index].nickname);
    }
    LightLock_Unlock(&shared.lock);
    if (!ok) {
        return false;
    }
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, GARAGE_NICK_MAX);
    swkbdSetHintText(&kb, "Nickname (empty: none)");
    swkbdSetInitialText(&kb, text);
    swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);
    char out[GARAGE_NICK_MAX + 1];
    if (swkbdInputText(&kb, out, sizeof out) != SWKBD_BUTTON_CONFIRM) {
        return false;
    }
    config_clean_text(out);
    if (strcmp(out, text) == 0) {
        return false;
    }
    LightLock_Lock(&shared.lock);
    snprintf(shared.garage_nick_key, sizeof shared.garage_nick_key, "%s", key);
    snprintf(shared.garage_nick, sizeof shared.garage_nick, "%s", out);
    LightLock_Unlock(&shared.lock);
    return true;
}

/* GARAGE card index's car (a copy) and its odometer now: live while the
 * TRIP screen polls the connected car, else the last saved reading.
 * False if there's no card or no reading; *live says which. */
static bool card_odometer(int index, garage_car_t *car, double *km, bool *live)
{
    LightLock_Lock(&shared.lock);
    bool ok = index >= 0 && index < shared.garage.count;
    if (ok) {
        *car = shared.garage.car[index];
    }
    float v = 0.0f;
    bool polled = ok && shared.trip_on && shared.live_have &&
                  strcmp(car->key, shared.garage_current) == 0 &&
                  obd_live_value(&shared.live, 0xA6, &v);
    LightLock_Unlock(&shared.lock);
    if (!ok) {
        return false;
    }
    *live = polled;
    if (polled) {
        *km = v;
        return true;
    }
    *km = car->odo_km;
    return car->have_odo;
}

/* A distance in the units, with commas and one decimal under 1,000:
 * "29,958 mi", "123.4 mi". */
static char *dist_text(double km, bool metric, char *out, size_t len)
{
    double v = metric ? km : km / MILEAGE_KM_MI;
    if (v < 1000.0) {
        snprintf(out, len, "%.1f %s", v, metric ? "km" : "mi");
    } else {
        char g[24];
        obd_format_grouped((long long)(v + 0.5), g, sizeof g);
        snprintf(out, len, "%s %s", g, metric ? "km" : "mi");
    }
    return out;
}

/* TRIP METER top screen. */
static void draw_trip_top(int busy, bool connected, int index, bool metric, bool full)
{
    garage_car_t c;
    double now_km = 0.0;
    bool live = false;
    bool have = card_odometer(index, &c, &now_km, &live);
    static char trip[sizeof shared.trip_text];
    LightLock_Lock(&shared.lock);
    memcpy(trip, shared.trip_text, sizeof trip);
    bool polling = shared.trip_on && shared.live_on;
    char msg[sizeof shared.live_msg];
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);

    char header[TOP_COLS + 1], line[TOP_COLS + 48], a[32], b[32];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf(full ? "\x1b[0m\x1b[2J" : "\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    snprintf(line, sizeof line, "TRIP METER   %.34s", c.nickname[0] ? c.nickname : c.title);
    print_row(2, TOP_COLS, line);
    for (int r = 3; r < TOP_ROWS; r++) {
        print_row(r, TOP_COLS, "");
    }
    if (!have) {
        print_row(4, TOP_COLS, polling ? "Reading the odometer..." : "No odometer reading for this car.");
        print_row(6, TOP_COLS, "The trip meter uses the car's odometer (PID A6,");
        print_row(7, TOP_COLS, "most cars from 2019). Without it, use the");
        print_row(8, TOP_COLS, "MILEAGE LOG and type the readings.");
        print_row(10, TOP_COLS, msg);
        return;
    }
    snprintf(line, sizeof line, "Odometer  %s  (%s)", dist_text(now_km, metric, a, sizeof a),
             live ? "live" : c.odo_date[0] ? c.odo_date : "saved");
    print_row(4, TOP_COLS, line);
    float start = 0.0f;
    char date[16] = "";
    if (!baro_store_get(trip, c.key, &start, date, sizeof date)) {
        print_row(6, TOP_COLS, "No trip yet: tap RESET TRIP to start one here.");
        return;
    }
    double t = now_km - start;
    if (t < 0.0) {
        t = 0.0;
    }
    double v = metric ? t : t / MILEAGE_KM_MI;
    char big[16];
    snprintf(big, sizeof big, "%.1f", v);
    int width = (int)strlen(big) * 4;
    big_print(7, (TOP_COLS - width) / 2, big, "\x1b[46m");
    printf("\x1b[%d;%dH%s", 11 + 1, (TOP_COLS - width) / 2 + width + 1, metric ? "km" : "mi");
    snprintf(line, sizeof line, "Trip since %s, from %s", date, dist_text(start, metric, b, sizeof b));
    print_row(13, TOP_COLS, line);
    if (!live) {
        print_row(15, TOP_COLS, "Connect to this car for a live reading.");
    }
}

/* MILEAGE LOG top screen: the newest entries and the average. */
static void draw_miles_top(int busy, bool connected, int index, bool metric, bool full, int sel)
{
    garage_car_t c;
    double now_km = 0.0;
    bool live = false;
    card_odometer(index, &c, &now_km, &live);
    static char text[sizeof shared.miles_text];
    static mileage_list_t list;
    LightLock_Lock(&shared.lock);
    memcpy(text, shared.miles_text, sizeof text);
    LightLock_Unlock(&shared.lock);
    mileage_read(text, c.key, &list);

    char header[TOP_COLS + 1], line[TOP_COLS + 64], a[24], b[24];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf(full ? "\x1b[0m\x1b[2J" : "\x1b[0m");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    snprintf(line, sizeof line, "MILEAGE LOG   %.34s", c.nickname[0] ? c.nickname : c.title);
    print_row(2, TOP_COLS, line);
    for (int r = 3; r < TOP_ROWS; r++) {
        print_row(r, TOP_COLS, "");
    }
    double avg;
    bool ok = metric ? mileage_l100(list.km, list.fuel_l, &avg) : mileage_mpg(list.km, list.fuel_l, &avg);
    if (list.n == 0) {
        print_row(4, TOP_COLS, "No entries yet. NEW ENTRY: the odometer at the");
        print_row(5, TOP_COLS, "start and end, and the fuel used (e.g. from one");
        print_row(6, TOP_COLS, "fill-up to the next).");
        return;
    }
    const char *cur = metric ? "" : "$";
    if (ok) {
        snprintf(line, sizeof line, "Average  %.1f %s over %s", avg, metric ? "L/100 km" : "mpg",
                 dist_text(list.km, metric, a, sizeof a));
        printf("\x1b[%d;1H\x1b[32m%-*.*s\x1b[0m", 4 + 1, TOP_COLS, TOP_COLS, line);
    }
    if (list.cost > 0.0 && list.cost_km > 0.0) {
        double per = list.cost / (metric ? list.cost_km : list.cost_km / MILEAGE_KM_MI);
        snprintf(line, sizeof line, "Spent    %s%.2f, %s%.2f a %s", cur, list.cost, cur, per,
                 metric ? "km" : "mile");
        print_row(5, TOP_COLS, line);
    }
    snprintf(line, sizeof line, "%-5s %-8s %-7s %-6s %-6s %s", "Date", metric ? "Odo km" : "Odo mi",
             metric ? "km" : "miles", metric ? "litre" : "gal", metric ? "L/100" : "mpg", "cost");
    print_row(6, TOP_COLS, line);
    int row = 7;
    for (int i = list.n - 1; i >= 0 && row < TOP_ROWS - 1; i--, row++) {
        const mileage_entry_t *e = &list.e[i];
        double km = e->end_km - e->start_km, econ;
        bool has = metric ? mileage_l100(km, e->fuel_l, &econ) : mileage_mpg(km, e->fuel_l, &econ);
        double f = metric ? e->fuel_l : e->fuel_l / MILEAGE_L_GAL;
        double d = metric ? km : km / MILEAGE_KM_MI;
        obd_format_grouped((long long)((metric ? e->end_km : e->end_km / MILEAGE_KM_MI) + 0.5), b,
                           sizeof b);
        char cost[16] = "-";
        if (e->price_l > 0.0) {
            snprintf(cost, sizeof cost, "%s%.2f", cur, mileage_cost(e));
        }
        snprintf(line, sizeof line, "%-5.5s %-8s %-7.1f %-6.2f %-6.1f %s", e->date + 5, b, d, f,
                 has ? econ : 0.0, cost);
        if (list.n - 1 - i == sel) {   /* the one selected below */
            printf("\x1b[%d;1H\x1b[42;30m%-*.*s\x1b[0m", row + 1, TOP_COLS, TOP_COLS, line);
        } else {
            print_row(row, TOP_COLS, line);
        }
    }
}

/* A number typed on the 3DS number pad (UI thread). initial may be "".
 * optional: an empty answer is accepted as 0. */
static bool edit_number(const char *hint, const char *initial, bool optional, double *out)
{
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 12);
    swkbdSetNumpadKeys(&kb, '.', 0);
    swkbdSetHintText(&kb, hint);
    swkbdSetInitialText(&kb, initial);
    swkbdSetValidation(&kb, optional ? SWKBD_ANYTHING : SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    char text[16];
    if (swkbdInputText(&kb, text, sizeof text) != SWKBD_BUTTON_CONFIRM) {
        return false;
    }
    if (optional && strspn(text, " ") == strlen(text)) {
        *out = 0.0;
        return true;
    }
    return mileage_parse(text, out);
}

/* MILEAGE: NEW ENTRY's questions, or EDIT's for the entry shown newest-
 * first at sel (-1: a new one). Returns a message for the notice line (or
 * NULL if cancelled); on success the change is queued. */
static const char *miles_new_entry(int index, bool metric, int sel)
{
    garage_car_t c;
    double now_km = 0.0;
    bool live = false;
    bool have_odo = card_odometer(index, &c, &now_km, &live);
    if (c.key[0] == '\0') {
        return "No car on this card.";
    }
    static char text[sizeof shared.miles_text];
    static mileage_list_t list;
    LightLock_Lock(&shared.lock);
    memcpy(text, shared.miles_text, sizeof text);
    LightLock_Unlock(&shared.lock);
    mileage_read(text, c.key, &list);
    double k = metric ? 1.0 : MILEAGE_KM_MI;   /* km per typed unit */
    double g = metric ? 1.0 : MILEAGE_L_GAL;   /* litres per typed unit */
    const mileage_entry_t *old = NULL;
    if (sel >= 0) {
        if (sel >= list.n) {
            return "Tap an entry first.";
        }
        old = &list.e[list.n - 1 - sel];
    }
    char init[24] = "";
    if (old != NULL) {
        snprintf(init, sizeof init, "%.1f", old->start_km / k);
    } else if (list.n > 0) {
        snprintf(init, sizeof init, "%.1f", list.e[list.n - 1].end_km / k);
    } else if (have_odo) {
        snprintf(init, sizeof init, "%.1f", now_km / k);
    }
    double start, end, fuel;
    if (!edit_number(metric ? "Odometer at the start (km)" : "Odometer at the start (miles)", init,
                     false, &start)) {
        return NULL;
    }
    init[0] = '\0';
    if (old != NULL) {
        snprintf(init, sizeof init, "%.1f", old->end_km / k);
    } else if (have_odo && now_km / k > start) {
        snprintf(init, sizeof init, "%.1f", now_km / k);
    }
    if (!edit_number(metric ? "Odometer at the end (km)" : "Odometer at the end (miles)", init,
                     false, &end)) {
        return NULL;
    }
    init[0] = '\0';
    if (old != NULL) {
        snprintf(init, sizeof init, "%.2f", old->fuel_l / g);
    }
    if (!edit_number(metric ? "Fuel used (litres)" : "Fuel used (gallons)", init, false, &fuel)) {
        return NULL;
    }
    /* The price: per gallon or litre, as last time (empty: leave it out). */
    double price = 0.0;
    init[0] = '\0';
    const mileage_entry_t *pe = old != NULL ? old : list.n > 0 ? &list.e[list.n - 1] : NULL;
    if (pe != NULL && pe->price_l > 0.0) {
        snprintf(init, sizeof init, "%.3f", pe->price_l * g);
    }
    if (!edit_number(metric ? "Price per litre (empty: skip)" : "Price per gallon (empty: skip)",
                     init, true, &price)) {
        return NULL;
    }
    const char *bad = mileage_check(start * k, end * k, fuel * g);
    if (bad != NULL) {
        return bad;
    }
    mileage_entry_t e;
    memset(&e, 0, sizeof e);
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (old != NULL) {
        snprintf(e.date, sizeof e.date, "%s", old->date);   /* an edit keeps its date */
    } else if (tm != NULL) {
        strftime(e.date, sizeof e.date, "%Y-%m-%d", tm);
    }
    e.start_km = start * k;
    e.end_km = end * k;
    e.fuel_l = fuel * g;
    e.price_l = price / g;
    LightLock_Lock(&shared.lock);
    snprintf(shared.miles_key, sizeof shared.miles_key, "%s", c.key);
    shared.miles_new = e;
    shared.miles_index = old != NULL ? list.first + (list.n - 1 - sel) : -1;
    LightLock_Unlock(&shared.lock);
    enqueue(old != NULL ? BTN_MILES_SET : BTN_MILES_ADD);
    return old != NULL ? "Entry changed." : "Entry saved.";
}

static void draw_ta_dialog(void)
{
    static const char *const text[] = {
        "TIME ATTACK",
        "",
        "For closed courses, track days and",
        "passengers only. Never race on public",
        "roads.",
        "",
        "Pick a run while parked. The timer then",
        "starts and stops by itself: don't touch",
        "or watch the 3DS while driving.",
        "",
        "Times come from the car's speed, read a",
        "few times a second: they're estimates.",
    };
    consoleSelect(&bottom_console);
    printf("\x1b[0m");
    clear_rows(0, 29);
    for (size_t i = 0; i < sizeof text / sizeof text[0]; i++) {
        print_row(2 + (int)i, BOTTOM_COLS, text[i]);
    }
    for (int i = 0; i < TA_WARN_BUTTON_COUNT; i++) {
        draw_button_ex(&ta_warn_buttons[i], false, false, false);
    }
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
static void status_now(kit_status_t *s, bool connected);

/* The SETUP page shown (graphics), or -1 for other screens. */
static int setup_kind_of(const ui_state_t *ui)
{
    if (!home_gfx) {
        return -1;
    }
    switch (ui->screen) {
    case SCREEN_SETUP:         return SET_MENU;
    case SCREEN_SETUP_WIFI:    return SET_WIFI;
    case SCREEN_GUIDE:         return SET_GUIDE;
    case SCREEN_SETUP_VEHICLE: return SET_VEHICLE;
    case SCREEN_MAKE:          return SET_MAKE;
    case SCREEN_SETUP_OTHER:   return SET_OTHER;
    case SCREEN_ABOUT:         return SET_ABOUT;
    case SCREEN_TEST:          return SET_TESTS;
    default:                   return -1;
    }
}

static void bar_value(int action, const gb_config_t *cfg, char *out, size_t len);

static void set_fact(diag_fact_t *f, int *n, const char *tag, const char *value, const char *detail,
                     diag_ink_t ink)
{
    diag_fact_t *x = &f[(*n)++];
    memset(x, 0, sizeof *x);
    snprintf(x->tag, sizeof x->tag, "%.15s", tag);
    snprintf(x->value, sizeof x->value, "%.39s", value);
    snprintf(x->detail, sizeof x->detail, "%.63s", detail);
    x->ink = ink;
}

/* SETUP's top screens (graphics). */
static void draw_setup_gfx_top(const ui_state_t *ui, const gb_config_t *cfg, bool connected,
                               int scroll)
{
    kit_status_t st;
    status_now(&st, connected);
    diag_fact_t f[DIAG_FACTS_MAX];
    int n = 0;
    char a[64], b[64];
    switch (setup_kind_of(ui)) {
    case SET_MENU: {
        snprintf(b, sizeof b, "%s:%u", cfg->adapter_host, (unsigned)cfg->adapter_port);
        set_fact(f, &n, "Adapter", connected ? "Connected" : "Not connected", b,
                 connected ? DIAG_INK_GREEN : DIAG_INK_AMBER);
        bar_value(BTN_WIFI_SLOT, cfg, a, sizeof a);
        set_fact(f, &n, "Wi-Fi slot", a, "", DIAG_INK_WHITE);
        LightLock_Lock(&shared.lock);
        diag_car_text(&shared.diag, a, sizeof a);
        bool vin = shared.diag.have_vin;
        LightLock_Unlock(&shared.lock);
        set_fact(f, &n, "Car", a[0] ? a : "Not known", a[0] && vin ? "from the VIN" : "", DIAG_INK_WHITE);
        bar_value(BTN_ENGINE_BAR, cfg, a, sizeof a);
        set_fact(f, &n, "Engine", a, "for Eco's fuel estimate", DIAG_INK_WHITE);
        set_fact(f, &n, "Units", cfg->metric ? "Metric" : "US",
                 cfg->metric ? "km/h, C, L/100 km" : "mph, F, mpg", DIAG_INK_WHITE);
        set_fact(f, &n, "Version", GB_VERSION_TEXT, "", DIAG_INK_WHITE);
        diag_draw_facts_top(icons_setup_idx, "SETUP", f, n, "", "", &st);
        return;
    }
    case SET_WIFI: {
        static char check[GUIDE_CHECK_LINES][40];
        LightLock_Lock(&shared.lock);
        memcpy(check, shared.guide_check, sizeof check);
        bool checking = shared.guide_checking, ok = shared.guide_ok;
        wifi_step_t step = (wifi_step_t)shared.guide_step;
        LightLock_Unlock(&shared.lock);
        const char *lines[GUIDE_CHECK_LINES];
        int nl = 0;
        for (int i = 0; i < GUIDE_CHECK_LINES && check[i][0] != '\0'; i++) {
            lines[nl++] = check[i];
        }
        setup_draw_wifi_top(lines, nl, ok, checking ? wifi_step_progress(step) : "", &st);
        return;
    }
    case SET_GUIDE: {
        static char lines[16][DIAG_VIEW_COLS + 1];
        int nl = wifi_guide_lines(ui->page, cfg->adapter_host, DIAG_VIEW_COLS, lines, 16);
        const char *ptr[16];
        for (int i = 0; i < nl; i++) {
            ptr[i] = lines[i];
        }
        char heading[80];
        snprintf(heading, sizeof heading, "Step %d of %d: %s", ui->page + 1, WIFI_GUIDE_STEPS,
                 wifi_guide_title(ui->page));
        setup_draw_guide_top(heading, ptr, nl, &st);
        return;
    }
    case SET_VEHICLE: {
        static const struct { const char *tag; int action; } rows[] = {
            { "Make", BTN_MAKE_SCREEN }, { "Model", BTN_MODEL_EDIT }, { "Year", BTN_NONE },
            { "Engine", BTN_ENGINE_BAR },
        };
        for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            bar_value(rows[i].action, cfg, a, sizeof a);
            set_fact(f, &n, rows[i].tag, a, rows[i].action == BTN_ENGINE_BAR ? "for Eco's fuel estimate" : "",
                     DIAG_INK_WHITE);
        }
        LightLock_Lock(&shared.lock);
        snprintf(a, sizeof a, "%s", shared.diag.have_vin ? shared.diag.vin : "Not read");
        LightLock_Unlock(&shared.lock);
        set_fact(f, &n, "VIN", a, "", DIAG_INK_WHITE);
        diag_draw_facts_top(icons_setup_idx, "VEHICLE", f, n,
                            "Values read from the VIN win over the ones set here.", "", &st);
        return;
    }
    case SET_MAKE:
        bar_value(BTN_MAKE_SCREEN, cfg, a, sizeof a);
        set_fact(f, &n, "Now", a, "", DIAG_INK_WHITE);
        diag_draw_facts_top(icons_setup_idx, "CAR MAKE", f, n,
                            "The make picks the manufacturer code descriptions in DIAG, for cars "
                            "that don't report a VIN.", "", &st);
        return;
    case SET_OTHER:
        set_fact(f, &n, "Units", cfg->metric ? "Metric" : "US",
                 cfg->metric ? "km/h, C, L/100 km, km" : "mph, F, mpg, miles", DIAG_INK_WHITE);
        set_fact(f, &n, "Version", GB_VERSION_TEXT, "Gauge Boy: OBD-II Deluxe", DIAG_INK_WHITE);
        diag_draw_facts_top(icons_setup_idx, "OTHER", f, n, "", "", &st);
        return;
    case SET_ABOUT:
        track_draw_text_top(icons_setup_idx, "ABOUT",
                            "Gauge Boy: OBD-II Deluxe, version " GB_VERSION_TEXT "\n\n"
                            "Reads a car's OBD-II data (check engine light, trouble codes, live data, "
                            "fuel trims) through a Wi-Fi ELM327 adapter.\n\n"
                            "Unofficial homebrew. Not made, endorsed or supported by Nintendo.\n\n"
                            "github.com/ly-man-do/gauge-boy", &st);
        return;
    case SET_TESTS: {
        enum { SHOWN = 13 };
        static char visible[SHOWN][TEXTLOG_COLS + 1];
        LightLock_Lock(&shared.lock);
        int count = textlog_count(&shared.log);
        int first = count - SHOWN - scroll;
        if (first < 0) {
            first = 0;
        }
        int shown = count < SHOWN ? count : SHOWN;
        for (int i = 0; i < shown; i++) {
            snprintf(visible[i], sizeof visible[i], "%s", textlog_line(&shared.log, first + i));
        }
        LightLock_Unlock(&shared.lock);
        const char *ptr[SHOWN];
        for (int i = 0; i < shown; i++) {
            ptr[i] = visible[i];
        }
        setup_draw_log_top(ptr, shown, scroll > 0, &st);
        return;
    }
    default:
        return;
    }
}

/* SETUP's bottom screens (graphics). */
static void draw_setup_gfx_bottom(const ui_state_t *ui, const gb_config_t *cfg, uint64_t now,
                                  int busy)
{
    setup_kind_t kind = (setup_kind_t)setup_kind_of(ui);
    if (kind == SET_MAKE) {
        static const char *names[64];
        int n = make_choice_count();
        for (int i = 0; i < n && i < 64; i++) {
            names[i] = i == 0 ? "Auto" : make_choice_name(i);
        }
        LightLock_Lock(&shared.lock);
        int chosen = make_choice_of(cfg->car_make);
        LightLock_Unlock(&shared.lock);
        int held = ui->live_touching && !ui->live_dragged ? ui->live_row_held : -1;
        setup_draw_make(names, n < 64 ? n : 64, diag_list_clamp(ui->make_scroll, n), chosen, held,
                        ui->live_touching && ui->live_grip,
                        ui->setup_held_in && ui->setup_held == SET_HIT_BACK);
        return;
    }
    setup_bottom_t b;
    memset(&b, 0, sizeof b);
    b.busy = -1;
    b.pressed = ui->setup_held_in ? ui->setup_held : SET_HIT_NONE;
    b.notice = ui->notice;
    static char values[8][48];
    switch (kind) {
    case SET_MENU:
        for (int i = 0; i < 4; i++) {
            b.labels[i] = setup_menu_label(i);
        }
        break;
    case SET_WIFI:
        b.labels[0] = "Check & Connect";
        b.labels[1] = "Wi-Fi slot";
        b.labels[2] = "Connection Guide";
        bar_value(BTN_GUIDE_CHECK, cfg, values[0], sizeof values[0]);
        bar_value(BTN_WIFI_SLOT, cfg, values[1], sizeof values[1]);
        values[2][0] = '\0';
        for (int i = 0; i < 3; i++) {
            b.values[i] = values[i];
        }
        b.chevron[2] = true;
        break;
    case SET_VEHICLE: {
        static const struct { const char *label; int action; bool chev; } rows[5] = {
            { "Make", BTN_MAKE_SCREEN, true }, { "Model", BTN_MODEL_EDIT, true },
            { "Year", BTN_NONE, false }, { "Engine", BTN_ENGINE_BAR, false },
            { "VIN", BTN_DIAG_SCAN, true },
        };
        for (int i = 0; i < 5; i++) {
            b.labels[i] = rows[i].label;
            bar_value(rows[i].action, cfg, values[i], sizeof values[i]);
            b.values[i] = values[i];
            b.chevron[i] = rows[i].chev;
        }
        break;
    }
    case SET_OTHER:
        b.labels[0] = "About Gauge Boy";
        b.metric = cfg->metric;
        break;
    case SET_GUIDE: {
        static char step[24], result[48];
        snprintf(step, sizeof step, "Step %d of %d", ui->page + 1, WIFI_GUIDE_STEPS);
        b.step = step;
        b.step_title = wifi_guide_title(ui->page);
        b.labels[0] = "Check & Connect";
        LightLock_Lock(&shared.lock);
        bool checking = shared.guide_checking, ok = shared.guide_ok;
        wifi_step_t ws = (wifi_step_t)shared.guide_step;
        snprintf(result, sizeof result, "%.47s", shared.guide_check[0]);
        LightLock_Unlock(&shared.lock);
        if (checking) {
            snprintf(result, sizeof result, "%.47s", wifi_step_progress(ws));
            b.result_color = KIT_AMBER;
        } else if (result[0] == '\0') {
            snprintf(result, sizeof result, "Tap Check & Connect after step 5.");
            b.result_color = KIT_INK3;
        } else {
            b.result_color = ok ? KIT_GREEN : KIT_AMBER;
        }
        b.result = result;
        if (flashing(BTN_PAGE_PREV, now)) {
            b.pressed = SET_HIT_PREV;
        } else if (flashing(BTN_PAGE_NEXT, now)) {
            b.pressed = SET_HIT_NEXT;
        }
        break;
    }
    case SET_ABOUT:
        b.text = "Trouble code text: Wal33D/dtc-database (MIT).\n\n"
                 "VIN models: NHTSA vPIC (US government, public domain).\n\n"
                 "Fonts and the rest: CREDITS.md.\n\n"
                 "Not for use by the driver while driving.";
        break;
    case SET_TESTS: {
        static const char *const labels[8] = { "ATZ", "ATSP0 + 0100", "ATDPN", "ATRV", "0902 VIN",
                                                "Speed Test", "Live Test", "Save Log" };
        for (int i = 0; i < 8; i++) {
            b.labels[i] = labels[i];
            if (buttons[i].action >= 0 && buttons[i].action == busy) {
                b.busy = i;
            }
        }
        break;
    }
    default:
        break;
    }
    setup_draw_bottom(kind, &b);
}

/* The LOG screen shown (graphics), or -1 for other screens. */
static int log_kind_of(const ui_state_t *ui)
{
    if (!home_gfx) {
        return -1;
    }
    if (ui->screen == SCREEN_GRAPH) {
        return LOG_GRAPH;
    }
    if (ui->screen != SCREEN_LOG) {
        return -1;
    }
    return ui->log_info ? LOG_INFO : ui->log_open ? LOG_DRIVE : LOG_LIST;
}

static void log_fact(diag_fact_t *f, int *n, const char *tag, const char *value, const char *detail)
{
    diag_fact_t *x = &f[(*n)++];
    memset(x, 0, sizeof *x);
    snprintf(x->tag, sizeof x->tag, "%.15s", tag);
    snprintf(x->value, sizeof x->value, "%.39s", value);   /* the drawing cuts what doesn't fit */
    snprintf(x->detail, sizeof x->detail, "%.63s", detail);
}

/* LOG's top screen (graphics): the drives, a drive's details, or the Delete question. */
static void draw_log_gfx_top(const ui_state_t *ui, bool connected)
{
    static drive_info_t d;
    LightLock_Lock(&shared.lock);
    int count = shared.drive_count;
    bool listed = shared.drives_listed;
    int sel = ui->selected;
    bool have = sel >= 0 && sel < count;
    unsigned long total = 0;
    for (int i = 0; i < count; i++) {
        total += shared.drives[i].size;
    }
    if (have) {
        d = shared.drives[sel];
    } else if (count > 0) {
        d = shared.drives[0];   /* the newest */
    }
    LightLock_Unlock(&shared.lock);
    kit_status_t st;
    status_now(&st, connected);
    char when[16], car[48], dur[16], buf[64], size[16];
    if (ui->delete_dialog && have) {
        log_row_parts(&d, when, sizeof when, car, sizeof car, dur, sizeof dur);
        char text[200];
        snprintf(text, sizeof text, "This removes the drive (%s, %s, %s) from the SD card.\n\n"
                 "This can't be undone.", when, car, dur);
        track_draw_text_top(icons_log_idx, "DELETE DRIVE?", text, &st);
        return;
    }
    diag_fact_t f[DIAG_FACTS_MAX];
    int n = 0;
    if (ui->log_open && have) {
        log_row_parts(&d, when, sizeof when, car, sizeof car, dur, sizeof dur);
        if (!d.ok) {
            log_fact(f, &n, "File", d.name, "");
            diag_draw_facts_top(icons_log_idx, when, f, n, "Not a Gauge Boy drive log.", "", &st);
            return;
        }
        log_fact(f, &n, "Car", d.car[0] ? d.car : "Not known", d.car[0] ? "" : "no VIN read");
        if (d.vin[0] != '\0') {
            log_fact(f, &n, "VIN", d.vin, "");
        }
        log_fact(f, &n, "Protocol", d.protocol[0] ? d.protocol : "Not recorded", "");
        if (d.duration_s >= 0 && d.rate_hz > 0) {
            snprintf(buf, sizeof buf, "about %ld rows at %g a second",
                     (long)(d.duration_s * d.rate_hz + 0.5f), (double)d.rate_hz);
        } else {
            buf[0] = '\0';
        }
        log_fact(f, &n, "Length", dur, buf);
        log_size(d.size, size, sizeof size);
        log_fact(f, &n, "Size", size, d.metric ? "metric units" : "US units");
        char cols[16];
        snprintf(cols, sizeof cols, "%d", d.columns);
        log_fact(f, &n, "Values", cols, "recorded");
        diag_draw_facts_top(icons_log_idx, when, f, n, d.values, "", &st);
        return;
    }
    if (!listed) {
        log_fact(f, &n, "Drives", "Reading", "the SD card...");
        diag_draw_facts_top(icons_log_idx, "LOG", f, n, "", "", &st);
        return;
    }
    snprintf(buf, sizeof buf, "%d", count);
    log_fact(f, &n, "Drives", buf, count == 1 ? "recorded" : "recorded");
    if (count > 0) {
        log_row_parts(&d, when, sizeof when, car, sizeof car, dur, sizeof dur);
        char detail[64];
        snprintf(detail, sizeof detail, "%.40s, %.15s", car, dur);
        log_fact(f, &n, "Newest", when, detail);
        log_size(total, size, sizeof size);
        log_fact(f, &n, "On the SD card", size, "sdmc:/3ds/gauge-boy/drives/");
    }
    diag_draw_facts_top(icons_log_idx, "LOG", f, n,
                        count > 0 ? "Tap a drive for its details."
                                  : "No recorded drives yet. Record one with REC on LIVE.",
                        "", &st);
}

/* The TRACK screen shown (graphics), or -1 for other screens. */
static int track_kind_of(const ui_state_t *ui)
{
    if (!home_gfx) {
        return -1;
    }
    if (ui->ta_dialog) {
        return TRK_NOTICE;
    }
    switch (ui->screen) {
    case SCREEN_TRACK:       return TRK_MENU;
    case SCREEN_TIME_ATTACK: return ui->ta_open ? TRK_TA_RUN : TRK_TA_LIST;
    case SCREEN_SHIFT:       return TRK_SHIFT;
    case SCREEN_GMETER:      return TRK_GMETER;
    case SCREEN_REVSYNC:     return TRK_REVSYNC;
    case SCREEN_ECO:         return TRK_ECO;
    case SCREEN_ACHIEVE:     return TRK_ACHIEVE;
    default:                 return -1;
    }
}

static diag_fact_t *trk_fact(diag_fact_t *f, int *n, const char *tag, const char *value,
                             const char *detail, diag_ink_t ink)
{
    diag_fact_t *x = &f[(*n)++];
    memset(x, 0, sizeof *x);
    snprintf(x->tag, sizeof x->tag, "%s", tag);
    snprintf(x->value, sizeof x->value, "%s", value);
    snprintf(x->detail, sizeof x->detail, "%s", detail);
    x->ink = ink;
    return x;
}

/* TRACK's top screen (graphics): what each mode does. */
static void draw_track_menu_gfx_top(bool connected)
{
    static char text[sizeof shared.ach_text];
    LightLock_Lock(&shared.lock);
    memcpy(text, shared.ach_text, sizeof text);
    LightLock_Unlock(&shared.lock);
    diag_fact_t f[DIAG_FACTS_MAX];
    int n = 0;
    char count[24];
    snprintf(count, sizeof count, "%d of %d", achieve_count(text), ACH_COUNT);
    trk_fact(f, &n, "Time Attack", app_cfg->metric ? "0-100, 100-0, 1/4 mile" : "0-60, 60-0, 1/4 mile",
             "starts on its own", DIAG_INK_WHITE);
    trk_fact(f, &n, "Shift Light", "LEDs fill with RPM", "flash at your shift", DIAG_INK_WHITE);
    trk_fact(f, &n, "G-Meter", "G-forces", "from the 3DS's motion sensor", DIAG_INK_WHITE);
    trk_fact(f, &n, "Rev Sync", "Blip to a target RPM", "parked only", DIAG_INK_WHITE);
    trk_fact(f, &n, "Eco", "Fuel economy", "and a smooth-driving score", DIAG_INK_WHITE);
    trk_fact(f, &n, "Achievements", count, "stickers unlocked", DIAG_INK_WHITE);
    kit_status_t st;
    status_now(&st, connected);
    diag_draw_facts_top(icons_track_idx, "TRACK", f, n, "", "", &st);
}

/* TIME ATTACK's top screen (graphics): the run list's bests, or a run. */
static void draw_ta_gfx_top(const ui_state_t *ui, bool connected, bool metric, uint64_t now)
{
    static ta_t ta;
    static obd_live_t lv;
    char msg[sizeof shared.live_msg];
    LightLock_Lock(&shared.lock);
    ta = shared.ta;
    lv = shared.live;
    bool have_live = shared.live_have;
    bool on = shared.live_on;
    bool car_known = shared.ta_car_known;
    bool have_bests[TA_RUN_COUNT], have_last[TA_RUN_COUNT];
    track_best_t bests[TA_RUN_COUNT];
    float last[TA_RUN_COUNT];
    for (int r = 0; r < TA_RUN_COUNT; r++) {
        have_bests[r] = shared.ta_have_bests[r];
        bests[r] = shared.ta_bests[r];
        have_last[r] = shared.ta_have_last[r];
        last[r] = shared.ta_last[r];
    }
    bool record = shared.ta_record;
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);
    kit_status_t st;
    status_now(&st, connected);

    if (ui->ta_dialog) {
        track_draw_text_top(icons_track_idx, "CLOSED COURSE",
                            "Time Attack is for closed courses, track days and passengers only. "
                            "Never time runs on public roads.\n\nNothing here needs a touch while "
                            "moving: pick a run while parked, then just drive. The timer starts "
                            "and stops by itself.", &st);
        return;
    }
    if (!ui->ta_open) {
        diag_fact_t f[DIAG_FACTS_MAX];
        int n = 0;
        for (int r = 0; r < TA_RUN_COUNT; r++) {
            char best[24] = "--", detail[48];
            if (have_bests[r]) {
                snprintf(best, sizeof best, "%.2f s", (double)bests[r].seconds);
            }
            if (have_last[r]) {
                snprintf(detail, sizeof detail, "%s; last %.2f s", have_bests[r] ? "best" : "no best",
                         (double)last[r]);
            } else {
                snprintf(detail, sizeof detail, "%s", have_bests[r] ? "best (this car)" : "no runs yet");
            }
            trk_fact(f, &n, track_ta_label(r, metric), best, detail, DIAG_INK_WHITE);
        }
        diag_draw_facts_top(icons_track_idx, "TIME ATTACK", f, n,
                            car_known ? "Pick a run while parked."
                                      : "Pick a run while parked. Bests aren't saved until the "
                                        "car is known (SETUP > VEHICLE).", "", &st);
        return;
    }

    int run = ui->ta_run;
    char title[24];
    size_t k = 0;
    for (const char *c = track_ta_label(run, metric); *c != '\0' && k + 1 < sizeof title; c++) {
        title[k++] = (char)toupper((unsigned char)*c);
    }
    title[k] = '\0';
    char big[16] = "", say_buf[64] = "", extra[64] = "", speed[12] = "", best[24] = "--",
         lastt[24] = "--";
    const char *say = "", *say2 = "", *flag = "";
    bool lcd = false;
    u32 color = KIT_INK;
    float to_unit = metric ? 1.0f : 1.0f / TA_MPH_KMH;
    switch (ta.state) {
    case TA_STOP_FIRST:
        say = "Stop the car: the timer arms when it";
        say2 = "stands still for a moment.";
        break;
    case TA_COUNTDOWN:
        snprintf(big, sizeof big, "%d", ta_countdown(&ta, now));
        color = KIT_AMBER;
        say = "Stay still...";
        break;
    case TA_GO:
        if (now - ta.state_ms < TA_GO_SHOW_MS) {
            snprintf(big, sizeof big, "GO!");
            color = KIT_GREEN;
        }
        say = "Ready: timing starts when the car moves.";
        break;
    case TA_BUILD_UP:
        if (now - ta.state_ms < TA_GO_SHOW_MS) {
            snprintf(big, sizeof big, "GO!");
            color = KIT_GREEN;
        }
        say = metric ? "Drive above 100 km/h, then brake to a stop."
                     : "Drive above 60 mph, then brake to a stop.";
        say2 = "Timing starts by itself.";
        break;
    case TA_BRAKE_READY:
        say = metric ? "Ready: timing starts under 100 km/h." : "Ready: timing starts under 60 mph.";
        break;
    case TA_RUNNING: {
        double t = ((double)now - ta.start_ms) / 1000.0;
        snprintf(big, sizeof big, "%.1f", t < 0.0 ? 0.0 : t);
        lcd = true;
        color = KIT_RGB(90, 169, 255);
        say = "Timing...";
        break;
    }
    default:
        break;
    }
    if (ta.result.have && (int)ta.result.run == run && ta.state != TA_RUNNING &&
        ta.state != TA_COUNTDOWN && big[0] == '\0') {
        const ta_result_t *r = &ta.result;
        snprintf(big, sizeof big, "%.2f", (double)r->seconds);
        lcd = true;
        color = ta.state == TA_DONE && record ? KIT_RED_HI : KIT_INK;
        if (ta.state == TA_DONE) {
            flag = record ? "NEW RECORD!" : "";
            say = record ? "" : "Result (an estimate)";
        }
        if (r->run == TA_RUN_QUARTER) {
            snprintf(extra, sizeof extra, "Trap %.0f %s, %s %.2f s", (double)(r->trap_kmh * to_unit),
                     metric ? "km/h" : "mph", metric ? "0-100" : "0-60", (double)r->split_s);
        } else if (r->run == TA_RUN_BRAKE) {
            snprintf(extra, sizeof extra, "Stopping distance %.0f %s",
                     (double)(metric ? r->distance_m : r->distance_m * 3.28084f), metric ? "m" : "ft");
        }
    }
    if (ta.note != NULL && ta.state != TA_DONE && ta.state != TA_OFF) {
        snprintf(say_buf, sizeof say_buf, "%s", ta.note);
        say2 = say;
        say = say_buf;
    }
    const obd_live_pid_t *sp = have_live ? obd_live_get(&lv, 0x0D) : NULL;
    const char *m = "";
    if (!on && msg[0] != '\0') {
        m = msg;
    } else if (have_live && lv.count == 0) {
        m = "This car doesn't report speed (PID 0D).";
    } else if (msg[0] != '\0') {
        m = msg;
    } else if (sp != NULL && sp->have) {
        snprintf(speed, sizeof speed, "%.0f", (double)(sp->value * to_unit));
    } else {
        snprintf(speed, sizeof speed, "--");
    }
    if (have_bests[run]) {
        snprintf(best, sizeof best, "%.2f s", (double)bests[run].seconds);
    }
    if (have_last[run]) {
        snprintf(lastt, sizeof lastt, "%.2f s", (double)last[run]);
    }
    track_ta_top_t t = { title, big, lcd, color, flag, say, say2, extra, speed,
                         metric ? "km/h" : "mph", best, lastt, m };
    track_draw_ta_top(&t, &st);
}

/* SHIFT LIGHT's top screen (graphics). */
static void draw_shift_gfx_top(bool connected, int shift_rpm, uint64_t now)
{
    static shift_light_t sl;
    static uint32_t reads_seen;
    static obd_live_t lv;
    char msg[sizeof shared.live_msg];
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have_live = shared.live_have;
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);
    if (sl.shift_rpm != shift_rpm) {
        shift_init(&sl, shift_rpm);
        reads_seen = 0;
    }
    const obd_live_pid_t *rp = have_live ? obd_live_get(&lv, 0x0C) : NULL;
    if (rp != NULL && rp->have && rp->reads != reads_seen) {
        reads_seen = rp->reads;
        shift_reading(&sl, rp->at_ms, rp->value);
    }
    bool fresh = rp != NULL && rp->have && now - rp->at_ms < 2000;
    float rpm = fresh ? shift_rpm_now(&sl, now) : 0.0f;
    track_shift_top_t t;
    memset(&t, 0, sizeof t);
    t.lit = fresh ? shift_update(&sl, rpm) : 0;
    if (sl.flashing) {
        achieve_ask(ACH_REDLINE);
    }
    t.flashing = sl.flashing;
    t.flash_on = !sl.flashing || shift_flash_on(now);
    for (int i = 0; i < SHIFT_LEDS && i < 10; i++) {
        t.color[i] = (int)shift_led_color(i);
    }
    char rpm_text[16] = "", foot[80], from[16], at[16];
    if (fresh) {
        obd_format_grouped((long long)((int)(rpm / 10.0f + 0.5f) * 10), rpm_text, sizeof rpm_text);
    }
    t.rpm = rpm_text;
    t.msg = msg[0] != '\0' ? msg : have_live && lv.count == 0 ? "This car doesn't report RPM (PID 0C)."
                                                              : "Waiting for RPM...";
    obd_format_grouped(shift_rpm * SHIFT_START_PCT / 100, from, sizeof from);
    obd_format_grouped(shift_rpm, at, sizeof at);
    snprintf(foot, sizeof foot, "The LEDs light from %s rpm and flash at %s.", from, at);
    t.foot = foot;
    kit_status_t st;
    status_now(&st, connected);
    track_draw_shift_top(&t, &st);
}

/* G-METER's top screen (graphics). */
static void draw_gmeter_gfx_top(bool connected)
{
    const gmeter_t *gm = &gmeter;
    static char lines[10][48];
    track_gm_top_t t;
    memset(&t, 0, sizeof t);
    int n = 0;
#define GM_LINE(...) do { snprintf(lines[n], sizeof lines[n], __VA_ARGS__); t.rows[n] = lines[n]; n++; } while (0)
    bool ok = gm->cal == GM_CAL_OK && gm->have;
    if (gm->cal == GM_CAL_RUNNING) {
        GM_LINE("!CALIBRATING");
        GM_LINE("Hold still... %d%%", gm->cal_n * 100 / GM_CAL_SAMPLES);
    } else if (gm->cal == GM_CAL_NONE) {
        GM_LINE("Mount the 3DS, then");
        GM_LINE("tap Calibrate (parked).");
    } else if (gm->cal == GM_CAL_MOVED) {
        GM_LINE("!IT MOVED");
        GM_LINE("Hold it still and");
        GM_LINE("tap Calibrate again.");
        GM_LINE("(shook %.2f g)", (double)gm->cal_max_g);
    } else if (gm->cal == GM_CAL_NO_SENSOR) {
        GM_LINE("!NO SENSOR");
        GM_LINE("No motion sensor readings.");
    } else if (ok) {
        GM_LINE("Side|%.2f g|%s", (double)fabsf(gm->lat),
                gm->lat > 0.02f ? "right" : gm->lat < -0.02f ? "left" : "");
        GM_LINE("Long|%.2f g|%s", (double)fabsf(gm->lon),
                gm->lon > 0.02f ? "accel" : gm->lon < -0.02f ? "brake" : "");
        GM_LINE("Total|%.2f g|", (double)gm_total(gm));
        GM_LINE("!PEAKS");
        GM_LINE("Side|%.2f|left %.2f", (double)fabsf(gm->peak_right), (double)fabsf(gm->peak_left));
        GM_LINE("Accel|%.2f|brake %.2f", (double)fabsf(gm->peak_accel), (double)fabsf(gm->peak_brake));
        GM_LINE("Total|%.2f g|", (double)gm->peak_total);
        GM_LINE("Forward: %s", gm->forward_learned ? "learned" : "guessed");
    }
#undef GM_LINE
    t.nrows = n;
    t.ball = ok;
    t.lat = gm->lat;
    t.lon = gm->lon;
    kit_status_t st;
    status_now(&st, connected);
    track_draw_gmeter_top(&t, &st);
}

/* REV SYNC's top screen (graphics). */
static void draw_revsync_gfx_top(bool connected)
{
    const rev_sync_t *rs = &revsync;
    LightLock_Lock(&shared.lock);
    bool car_known = shared.rs_car_known, have_best = shared.rs_have_best;
    track_best_t best = shared.rs_best;
    bool record = shared.rs_record;
    char msg[sizeof shared.live_msg];
    memcpy(msg, shared.live_msg, sizeof msg);
    LightLock_Unlock(&shared.lock);
    track_rs_top_t t;
    memset(&t, 0, sizeof t);
    bool playing = rs->state >= RS_READY && rs->state <= RS_DONE;
    char big[16] = "", say[48] = "", round[32] = "", score[16] = "", target[16] = "", best_text[64];
    t.target = playing && rs->state != RS_DONE ? rs->target[rs->round] : 0;
    t.rpm = rs->have_rpm ? rs->rpm : 0.0f;
    t.peak = rs->state >= RS_GO && rs->state <= RS_JUDGE && rs->peak > 0.0f ? rs->peak : 0.0f;
    t.big_color = KIT_AMBER;
    t.say2 = "";
    switch (rs->state) {
    case RS_LOCKED:
        snprintf(say, sizeof say, "%s", rs->note != NULL ? rs->note : "Waiting for the car's speed...");
        t.say2 = "Rev Sync runs only while the speed reads 0.";
        break;
    case RS_IDLE:
        snprintf(say, sizeof say, "Parked. Tap Start to play 5 rounds.");
        t.say2 = "Engine warm, in neutral or park.";
        break;
    case RS_READY:
        obd_format_grouped(t.target, big, sizeof big);
        snprintf(say, sizeof say, "Get ready: blip to this RPM at GO.");
        break;
    case RS_GO:
        snprintf(big, sizeof big, "GO!");
        t.big_color = KIT_GREEN;
        snprintf(say, sizeof say, "Blip the throttle to the target!");
        break;
    case RS_JUDGE: {
        int r = rs->round;
        if (rs->peak_of[r] > 0) {
            obd_format_grouped(rs->peak_of[r], big, sizeof big);
        }
        t.big_color = rs->grade[r] >= RS_GRADE_GREAT ? KIT_RGB(255, 210, 63)
                    : rs->grade[r] == RS_GRADE_MISS ? KIT_RED_HI : KIT_AMBER;
        snprintf(say, sizeof say, "%s! +%d%s", rs_grade_name(rs->grade[r]), rs->points,
                 rs->combo > 1 && rs->grade[r] >= RS_GRADE_GREAT ? " combo" : "");
        if (rs->peak_of[r] == 0) {
            t.say2 = "No blip in time.";
        }
        break;
    }
    case RS_DONE:
        obd_format_grouped(rs->score, big, sizeof big);
        t.big_color = record ? KIT_RED_HI : KIT_GREEN;
        snprintf(say, sizeof say, "RANK %s%s", rs_rank(rs->score), record ? "  NEW HIGH SCORE!" : "");
        break;
    }
    if (!rs->have_rpm && msg[0] != '\0') {
        t.say2 = msg;
    }
    if (playing) {
        snprintf(round, sizeof round, "Round %d of %d", rs->round + 1, RS_ROUNDS);
        obd_format_grouped(rs->score, score, sizeof score);
        obd_format_grouped(rs->target[rs->round], target, sizeof target);
    }
    if (!car_known) {
        snprintf(best_text, sizeof best_text, "High score: not saved (car unknown)");
    } else if (have_best) {
        char b[16];
        obd_format_grouped((long long)best.seconds, b, sizeof b);
        snprintf(best_text, sizeof best_text, "High score (this car) %s, rank %s", b,
                 rs_rank((int)best.seconds));
    } else {
        snprintf(best_text, sizeof best_text, "High score: none yet");
    }
    t.big = big;
    t.say = say;
    t.round = round;
    t.score = score;
    t.target_text = target;
    t.best = best_text;
    kit_status_t st;
    status_now(&st, connected);
    track_draw_revsync_top(&t, &st);
}

/* ECO CHALLENGE's top screen (graphics). */
static void draw_eco_gfx_top(bool connected, const gb_config_t *cfg, uint64_t now)
{
    static obd_live_t lv;
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have_live = shared.live_have;
    bool car_known = shared.rs_car_known, have_best = shared.rs_have_best;
    track_best_t best = shared.rs_best;
    LightLock_Unlock(&shared.lock);
    track_eco_top_t t;
    memset(&t, 0, sizeof t);
    int score = eco_score(&eco);
    bool ranked = eco_ranked(&eco);
    const char *rank = eco_rank(score);
    char score_text[8], now_text[24], trip[24], best_text[24];
    snprintf(score_text, sizeof score_text, "%2d", score);
    t.score = score_text;
    t.rank = ranked ? rank : "";
    t.rank_color = rank[0] == 'S' ? KIT_RGB(255, 210, 63) : rank[0] == 'A' ? KIT_GREEN
                 : rank[0] == 'B' ? KIT_AMBER : KIT_RED_HI;
    bool us = !cfg->metric;
    float l100 = 0.0f;
    if (eco_now_l100(&eco, &l100)) {
        snprintf(now_text, sizeof now_text, us ? "%.1f mpg" : "%.1f L/100", us ? (double)(ECO_L100_PER_MPG / l100) : (double)l100);
    } else if (eco.have_lph) {
        snprintf(now_text, sizeof now_text, us ? "%.2f gal/h" : "%.2f L/h",
                 us ? (double)(eco.lph / ECO_L_PER_GAL) : (double)eco.lph);
    } else {
        snprintf(now_text, sizeof now_text, "--");
    }
    if (eco_avg_l100(&eco, &l100)) {
        snprintf(trip, sizeof trip, us ? "%.1f mpg" : "%.1f L/100", us ? (double)(ECO_L100_PER_MPG / l100) : (double)l100);
    } else {
        snprintf(trip, sizeof trip, "--");
    }
    if (!car_known) {
        snprintf(best_text, sizeof best_text, "not saved");
    } else if (have_best) {
        snprintf(best_text, sizeof best_text, "%d %s", (int)best.seconds, eco_rank((int)best.seconds));
    } else {
        snprintf(best_text, sizeof best_text, "none yet");
    }
    t.now = now_text;
    t.trip = trip;
    t.best = best_text;
    static char stats[6][32];
    double dist = us ? eco.dist_km / 1.609344 : eco.dist_km;
    double fuel = us ? eco.fuel_l / ECO_L_PER_GAL : eco.fuel_l;
    snprintf(stats[0], sizeof stats[0], "Distance|%.1f %s", dist, us ? "mi" : "km");
    snprintf(stats[1], sizeof stats[1], "Fuel|%.2f %s", fuel, us ? "gal" : "L");
    snprintf(stats[2], sizeof stats[2], "Driving|%d:%02d", (int)eco.moving_s / 60, (int)eco.moving_s % 60);
    snprintf(stats[3], sizeof stats[3], "Idling|%d:%02d", (int)eco.idle_s / 60, (int)eco.idle_s % 60);
    snprintf(stats[4], sizeof stats[4], "Hard starts|%d", eco.harsh_accel);
    snprintf(stats[5], sizeof stats[5], "Hard stops|%d", eco.harsh_brake);
    for (int i = 0; i < 6; i++) {
        t.stats[i] = stats[i];
    }
    static char tip[96];
    tip[0] = '\0';
    if (eco.tip != ECO_TIP_NONE && now - eco.tip_ms < 10000) {
        snprintf(tip, sizeof tip, "%s", eco_tip_text(eco.tip));
    }
    t.tip = tip;
    bool maf = have_live && obd_live_get(&lv, 0x10) != NULL;
    bool map = have_live && (obd_live_get(&lv, 0x0B) != NULL || obd_live_get(&lv, 0x87) != NULL);
    t.msg = have_live && !maf && !(map && cfg->engine_dl != 0)
                ? (map ? "Set the engine size in SETUP > VEHICLE for a fuel estimate."
                       : "This car reports neither MAF nor MAP; the score still works.")
                : "Fuel figures are estimates.";
    kit_status_t st;
    status_now(&st, connected);
    track_draw_eco_top(&t, &st);
}

/* ACHIEVEMENTS' top screen (graphics): the stickers. */
static void draw_achieve_gfx_top(bool connected)
{
    static char text[sizeof shared.ach_text];
    LightLock_Lock(&shared.lock);
    memcpy(text, shared.ach_text, sizeof text);
    LightLock_Unlock(&shared.lock);
    const char *names[ACH_COUNT];
    bool unlocked[ACH_COUNT];
    for (int i = 0; i < ACH_COUNT; i++) {
        char date[16];
        names[i] = achieve_info((achieve_id_t)i)->name;
        unlocked[i] = achieve_unlocked(text, (achieve_id_t)i, date, sizeof date);
    }
    kit_status_t st;
    status_now(&st, connected);
    track_draw_ach_top(names, unlocked, ACH_COUNT, achieve_count(text), &st);
}

/* A TRACK screen's top (graphics). */
static void draw_track_gfx_top(const ui_state_t *ui, bool connected, const gb_config_t *cfg,
                               uint64_t now)
{
    switch (ui->screen) {
    case SCREEN_TIME_ATTACK: draw_ta_gfx_top(ui, connected, cfg->metric, now); break;
    case SCREEN_SHIFT:       draw_shift_gfx_top(connected, cfg->shift_rpm, now); break;
    case SCREEN_GMETER:      draw_gmeter_gfx_top(connected); break;
    case SCREEN_REVSYNC:     draw_revsync_gfx_top(connected); break;
    case SCREEN_ECO:         draw_eco_gfx_top(connected, cfg, now); break;
    case SCREEN_ACHIEVE:     draw_achieve_gfx_top(connected); break;
    default:
        if (ui->ta_dialog) {
            draw_ta_gfx_top(ui, connected, cfg->metric, now);
        } else {
            draw_track_menu_gfx_top(connected);
        }
        break;
    }
}

/* TRIM's top screen (citro2d): the banks' bars and the engine line. */
static void draw_trim_gfx_top(bool connected)
{
    static obd_live_t lv;
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have = shared.live_have;
    LightLock_Unlock(&shared.lock);
    trim_state_t ts;
    float load = 0;
    bool have_load = false;
    if (have) {
        trim_state_from_live(&lv, &ts);
        have_load = obd_live_value(&lv, 0x04, &load);
    } else {
        memset(&ts, 0, sizeof ts);
        ts.banks = 2;   /* until the car says otherwise */
    }
    char engine[64];
    trim_engine_text(&ts, have_load, load, engine, sizeof engine);
    kit_status_t s;
    status_now(&s, connected);
    trim_draw_top(&ts, engine, &s);
}

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
    LightLock_Lock(&shared.lock);
    bool rec = shared.rec_active;
    unsigned rec_secs = rec ? (unsigned)((tp_now_ms() - shared.rec_started_ms) / 1000u) : 0;
    char rec_line[TOP_COLS + 8];
    snprintf(rec_line, sizeof rec_line, "REC %u:%02u  %s", rec_secs / 60, rec_secs % 60,
             shared.rec_file);
    LightLock_Unlock(&shared.lock);
    print_row(TOP_ROWS - 1, TOP_COLS, msg[0] != '\0' ? msg
                                      : rec ? rec_line
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
                            app_cfg->metric ? OBD_UNITS_METRIC : OBD_UNITS_US,
                            (float)app_cfg->engine_dl / 10.0f, TOP_COLS, lines, LIVE_VIEW_MAX);
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
    LightLock_Lock(&shared.lock);
    bool rec = shared.rec_active;
    unsigned rec_secs = rec ? (unsigned)((tp_now_ms() - shared.rec_started_ms) / 1000u) : 0;
    char rec_line[TOP_COLS + 8];
    snprintf(rec_line, sizeof rec_line, "REC %u:%02u  %s", rec_secs / 60, rec_secs % 60,
             shared.rec_file);
    LightLock_Unlock(&shared.lock);
    print_row(TOP_ROWS - 1, TOP_COLS, msg[0] != '\0' ? msg
                                      : rec ? rec_line
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
/* LOG top screen: the selected drive's details, or what LOG is. */
static void draw_log_top(int selected, int busy, bool connected)
{
    static char lines[DRIVE_INFO_LINES][DRIVE_INFO_COLS + 1];
    char header[TOP_COLS + 1];
    char title[TOP_COLS + 1];
    int n = 0;
    LightLock_Lock(&shared.lock);
    int count = shared.drive_count;
    bool listed = shared.drives_listed;
    if (selected >= 0 && selected < count) {
        n = drive_info_lines(&shared.drives[selected], lines, DRIVE_INFO_LINES);
    }
    LightLock_Unlock(&shared.lock);
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf("\x1b[0m\x1b[2J");
    print_row(0, TOP_COLS, header);
    print_row(1, TOP_COLS, "-------------------------------------------------");
    if (n > 0) {
        print_row(2, TOP_COLS, "Recorded drive");
        for (int i = 0; i < n; i++) {
            print_row(4 + i, TOP_COLS, lines[i]);
        }
        print_row(TOP_ROWS - 1, TOP_COLS, "On the SD card: sdmc:/3ds/gauge-boy/drives/");
        return;
    }
    snprintf(title, sizeof title, "LOG: %s", !listed ? "reading the SD card..."
                                  : count == 0 ? "no recorded drives yet" : "recorded drives");
    print_row(2, TOP_COLS, title);
    print_row(4, TOP_COLS, "Record a drive with REC on the LIVE screen: it");
    print_row(5, TOP_COLS, "saves that view's values to a CSV file, 5 rows");
    print_row(6, TOP_COLS, "a second, in sdmc:/3ds/gauge-boy/drives/.");
    print_row(8, TOP_COLS, count > 0 ? "Tap a drive for its details." : "");
}

/* GRAPH: ask the worker for ui's window and value. */
static void graph_request(const ui_state_t *ui, const char *file)
{
    LightLock_Lock(&shared.lock);
    snprintf(shared.graph_req.file, sizeof shared.graph_req.file, "%.31s", file);
    shared.graph_req.col = ui->graph_col;
    shared.graph_req.t0 = ui->graph_t0;
    shared.graph_req.t1 = ui->graph_t1;
    shared.graph_loading = true;
    shared.graph_version++;
    LightLock_Unlock(&shared.lock);
    enqueue(BTN_GRAPH_LOAD);
}

/* HOME: the current car's key, title and garage card (if it has one).
 * Call with shared.lock held. */
static bool home_car(char *key, size_t klen, char *title, size_t tlen, garage_car_t *card,
                     bool *have_card)
{
    baro_store_key(&shared.diag, app_cfg->car_make, app_cfg->car_model, app_cfg->car_year, key,
                   klen);
    *have_card = false;
    title[0] = '\0';
    if (key[0] == '\0') {
        return false;
    }
    for (int i = 0; i < shared.garage.count; i++) {
        if (strcmp(shared.garage.car[i].key, key) == 0) {
            *card = shared.garage.car[i];
            *have_card = true;
            snprintf(title, tlen, "%s", card->title);
        }
    }
    if (title[0] == '\0') {
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        garage_title(key, tm != NULL ? tm->tm_year + 1900 : 0, title, tlen);
    }
    return true;
}

/* HOME's Garage button: the nickname, else the model ("Ridgeline" from
 * "2008 Honda Ridgeline"); NULL if no car. */
static const char *home_garage_line(char *out, size_t len)
{
    char key[32], title[48];
    static garage_car_t card;
    bool have_card;
    LightLock_Lock(&shared.lock);
    bool have = home_car(key, sizeof key, title, sizeof title, &card, &have_card);
    LightLock_Unlock(&shared.lock);
    if (!have) {
        return NULL;
    }
    if (have_card && card.nickname[0] != '\0') {
        snprintf(out, len, "%s", card.nickname);
        return out;
    }
    const char *model = title;
    const char *sp = strchr(title, ' ');
    if (sp != NULL && strchr(sp + 1, ' ') != NULL) {
        model = strchr(sp + 1, ' ') + 1;   /* after the year and the make */
    }
    snprintf(out, len, "%s", model[0] ? model : title);
    return out;
}

/* The top screen's status bar now (citro2d screens). */
static void status_now(kit_status_t *s, bool connected)
{
    u8 level = 0, charging = 0;
    s->battery = R_SUCCEEDED(PTMU_GetBatteryLevel(&level)) ? level : -1;
    s->charging = R_SUCCEEDED(PTMU_GetBatteryChargeState(&charging)) && charging != 0;
    u8 strength = osGetWifiStrength();
    s->bars = strength > 0 ? strength + 1 : 0;
    s->connected = connected;
    s->now = time(NULL);
}

/* LIVE's Dashboard top screen (citro2d). */
static void draw_live_dash_top(bool connected)
{
    static obd_live_t lv;
    static garage_car_t card;
    char key[32], title[48];
    bool have_card = false;
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have = shared.live_have;
    bool cel_known = shared.diag.have_readiness;
    bool cel = cel_known && shared.diag.readiness.mil_on;
    home_car(key, sizeof key, title, sizeof title, &card, &have_card);
    LightLock_Unlock(&shared.lock);
    live_dash_t d;
    live_dash_values(have ? &lv : NULL, app_cfg->metric ? OBD_UNITS_METRIC : OBD_UNITS_US,
                     have_card && card.have_odo, card.odo_km, &d);
    kit_status_t s;
    status_now(&s, connected);
    live_draw_dash(&d, cel_known, cel, &s);
}

/* A LIVE view's top screen (citro2d): its title and values. */
static void draw_live_view_top(int view, bool connected)
{
    static obd_live_t lv;
    static live_row_t rows[LIVE_VIEW_MAX];
    LightLock_Lock(&shared.lock);
    lv = shared.live;
    bool have = shared.live_have;
    LightLock_Unlock(&shared.lock);
    int n = live_view_rows(have ? &lv : NULL, app_cfg->metric ? OBD_UNITS_METRIC : OBD_UNITS_US,
                           (float)app_cfg->engine_dl / 10.0f, rows, LIVE_VIEW_MAX);
    char title[32];
    size_t k = 0;
    for (const char *c = live_view_name(view); *c != '\0' && k + 1 < sizeof title; c++) {
        title[k++] = (char)toupper((unsigned char)*c);
    }
    title[k] = '\0';
    kit_status_t s;
    status_now(&s, connected);
    live_draw_view_top(title, rows, n, connected, &s);
}

/* DIAG (graphics): the shown page's list rows. */
static diag_row_t diag_rows[DIAG_ITEMS_MAX];
static int diag_rows_n;

static void diag_rows_refresh(diag_tab_t tab)
{
    LightLock_Lock(&shared.lock);
    diag_rows_n = diag_page_rows(&shared.diag, tab, diag_rows, DIAG_ITEMS_MAX);
    LightLock_Unlock(&shared.lock);
}

/* DIAG's top screen (citro2d): what each button holds, a page's summary, the
 * code tapped, or what clearing does. */
static void draw_diag_gfx_top(const ui_state_t *ui, bool connected)
{
    static obd_diag_t d;
    static garage_car_t card;
    char key[32], title[48], status[64] = "";
    bool have_card = false;
    LightLock_Lock(&shared.lock);
    d = shared.diag;
    home_car(key, sizeof key, title, sizeof title, &card, &have_card);
    if (shared.scanning) {
        snprintf(status, sizeof status, "Scanning: %s...", obd_diag_step_str(shared.scan_step));
    } else {
        snprintf(status, sizeof status, "%s", shared.diag_msg);
    }
    LightLock_Unlock(&shared.lock);
    kit_status_t s;
    status_now(&s, connected);
    if (ui->clear_dialog) {
        diag_draw_clear_top(&s);
        return;
    }
    obd_units_t units = app_cfg->metric ? OBD_UNITS_METRIC : OBD_UNITS_US;
    int icon = d.have_readiness && d.readiness.mil_on ? icons_diag_cel_idx : icons_diag_idx;
    diag_fact_t f[DIAG_FACTS_MAX];
    char note[160];
    if (ui->screen == SCREEN_DIAG) {
        int n = diag_home_facts(&d, have_card ? card.nickname : "", units, f, DIAG_FACTS_MAX, note,
                                sizeof note);
        diag_draw_facts_top(icon, "DIAG", f, n, note, status, &s);
        return;
    }
    static diag_code_t c;
    if (ui->tab != DIAG_TAB_READINESS && ui->selected >= 0 &&
        diag_code_detail(&d, ui->tab, ui->selected, units, &c)) {
        diag_draw_code_top(&c, &s);
        return;
    }
    int n = diag_page_facts(&d, ui->tab, f, DIAG_FACTS_MAX, note, sizeof note);
    diag_draw_facts_top(icon, diag_page_title(ui->tab), f, n, note, status, &s);
}

/* HOME top screen (citro2d). */
static void draw_home_top(bool connected)
{
    static garage_car_t card;
    char key[32], title[48], adapter[ELM_LINE_MAX];
    bool have_card;

    LightLock_Lock(&shared.lock);
    bool have_car = home_car(key, sizeof key, title, sizeof title, &card, &have_card);
    bool cel_known = shared.diag.have_readiness;
    bool cel = cel_known && shared.diag.readiness.mil_on;
    int codes = shared.diag.have_stored ? shared.diag.stored.count : -1;
    char code_list[40] = "";
    for (int i = 0; codes > 0 && i < shared.diag.stored.count && i < 3; i++) {
        size_t n = strlen(code_list);
        snprintf(code_list + n, sizeof code_list - n, "%s%s", i ? ", " : "", shared.diag.stored.code[i].code);
    }
    bool have_mil_km = shared.diag.have_mil_km;
    int mil_km = shared.diag.mil_km;
    int protocol = shared.diag.link.protocol;
    snprintf(adapter, sizeof adapter, "%s", shared.diag.link.adapter_id);
    LightLock_Unlock(&shared.lock);
    time_t now = time(NULL);

    home_top_t t;
    memset(&t, 0, sizeof t);
    t.connected = connected;
    t.have_car = have_car;
    t.nickname = have_card ? card.nickname : "";
    t.title = title;
    t.cel_known = cel_known;
    t.cel_on = cel;
    t.codes = codes;
    t.code_list = code_list;
    t.have_mil_km = have_mil_km;
    t.mil_km = mil_km;
    t.protocol = protocol;
    t.adapter_id = adapter;
    t.adapter_host = app_cfg->adapter_host;
    t.metric = app_cfg->metric;
    t.have_odo = have_card && card.have_odo;
    t.odo_km = card.odo_km;

    home_status_t s;
    status_now(&s, connected);
    s.now = now;
    home_draw_top(&t, &s);
}

/* GRAPH top screen (citro2d; a text summary without graphics). */
static void draw_graph_top(const ui_state_t *ui, bool connected)
{
    static graph_cols_t g;
    char title[48], sub[64], place[32], msg[48];
    LightLock_Lock(&shared.lock);
    bool loading = shared.graph_loading;
    g = shared.graph;
    int ncols = shared.graph_ncols;
    int col = shared.graph_col;
    if (ncols > 0) {
        const drive_column_t *c = &shared.graph_columns[col];
        snprintf(title, sizeof title, c->unit[0] ? "%s (%s)" : "%s%s", c->name, c->unit);
    } else {
        snprintf(title, sizeof title, "GRAPH");
    }
    snprintf(place, sizeof place, ncols > 0 ? "value %d of %d" : " ", col + 1, ncols);
    sub[0] = '\0';
    if (ui->selected >= 0 && ui->selected < shared.drive_count) {
        char when[16], car[48], dur[16];
        log_row_parts(&shared.drives[ui->selected], when, sizeof when, car, sizeof car, dur,
                      sizeof dur);
        snprintf(sub, sizeof sub, "%.11s  %.32s  %.12s", when, car, dur);
    }
    snprintf(msg, sizeof msg, "%s", shared.graph_msg);
    LightLock_Unlock(&shared.lock);

    if (graph_gfx) {
        kit_status_t st;
        status_now(&st, connected);
        graph_view_t v = { title, sub, place, loading ? NULL : &g,
                           loading ? "Loading..." : msg[0] ? msg : NULL,
                           st.bars, st.connected, st.battery, st.charging, st.now };
        graph_draw(&v);
        return;
    }
    consoleSelect(&top_console);
    printf("\x1b[0m\x1b[2J");
    print_row(0, TOP_COLS, title);
    print_row(1, TOP_COLS, sub);
    if (loading || msg[0]) {
        print_row(3, TOP_COLS, loading ? "Loading..." : msg);
        return;
    }
    char line[TOP_COLS + 8];
    snprintf(line, sizeof line, "min %.2f  max %.2f  avg %.2f  (%u values)", (double)g.vmin,
             (double)g.vmax, (double)graph_cols_avg(&g), (unsigned)g.n);
    print_row(3, TOP_COLS, line);
    print_row(5, TOP_COLS, "(no graphics: the fonts didn't load)");
}

static void draw_log_delete_dialog(const char *name)
{
    consoleSelect(&bottom_console);
    printf("\x1b[0m");
    clear_rows(0, 29);
    print_row(2, BOTTOM_COLS, "Delete this recorded drive?");
    print_row(4, BOTTOM_COLS, name);
    print_row(6, BOTTOM_COLS, "The file is removed from the SD card.");
    print_row(7, BOTTOM_COLS, "This can't be undone.");
    draw_button_ex(&log_delete_buttons[0], false, false, false);
    draw_button_ex(&log_delete_buttons[1], false, true, false);   /* red */
}

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
    case BTN_GUIDE_CHECK:
        snprintf(out, len, "%s", shared.guide_checking ? "checking..."
                                 : shared.connected ? "Connected" : "Not connected");
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
                                                                              : "tap to read");
        break;
    case BTN_ENGINE_BAR:
        if (cfg->engine_dl != 0) {
            snprintf(out, len, "%u.%u L", (unsigned)(cfg->engine_dl / 10),
                     (unsigned)(cfg->engine_dl % 10));
        } else {
            snprintf(out, len, "Unknown");
        }
        break;
    case BTN_UNITS:
        snprintf(out, len, "%s", cfg->metric ? "Metric" : "US");
        break;
    default:
        break;
    }
    LightLock_Unlock(&shared.lock);
}

/* About Gauge Boy (top screen). */
static void draw_about_top(int busy, bool connected)
{
    char header[TOP_COLS + 1];
    spike_format_header(header, sizeof header, connected, busy);
    consoleSelect(&top_console);
    printf("\x1b[0m\x1b[2J");
    print_row(0, TOP_COLS, header);
    static const char *const about[] = {
        "",
        "Gauge Boy: OBD-II Deluxe",
        "Version " GB_VERSION_TEXT,
        "",
        "Reads a car's OBD-II data (check engine light,",
        "trouble codes, live data, fuel trims) through a",
        "Wi-Fi ELM327 adapter.",
        "",
        "Unofficial homebrew. Not made, endorsed or",
        "supported by Nintendo.",
        "",
        "github.com/ly-man-do/gauge-boy",
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

/* A TRACK screen's bottom (graphics). */
static void draw_track_gfx_bottom(const ui_state_t *ui, const gb_config_t *cfg, uint64_t now,
                                  track_kind_t kind)
{
    int pressed = ui->track_held_in ? ui->track_held : TRK_HIT_NONE;
    if (pressed == TRK_HIT_NONE && flashing(BTN_BACK, now)) {
        pressed = TRK_HIT_BACK;
    }
    if (ui->track_info && kind != TRK_NOTICE) {
        track_draw_info(kind, pressed == TRK_HIT_BACK);
        return;
    }
    if (kind == TRK_ACHIEVE) {
        static char text[sizeof shared.ach_text];
        static char dates[ACH_COUNT][16];
        LightLock_Lock(&shared.lock);
        memcpy(text, shared.ach_text, sizeof text);
        LightLock_Unlock(&shared.lock);
        track_ach_row_t rows[ACH_COUNT];
        for (int i = 0; i < ACH_COUNT; i++) {
            const achieve_info_t *a = achieve_info((achieve_id_t)i);
            rows[i].name = a->name;
            rows[i].unlocked = achieve_unlocked(text, (achieve_id_t)i, dates[i], sizeof dates[i]);
            rows[i].right = rows[i].unlocked ? dates[i] : a->how;
        }
        track_draw_ach_bottom(rows, ACH_COUNT, diag_list_clamp(ui->ach_scroll, ACH_COUNT),
                              ui->live_touching && ui->live_grip, pressed, ui->notice);
        return;
    }
    track_bottom_t b;
    memset(&b, 0, sizeof b);
    b.pressed = pressed;
    b.notice = ui->notice;
    b.status = "";
    static char lcd[16];
    switch (kind) {
    case TRK_MENU:
        for (int i = 0; i < TRACK_MENU_ITEMS; i++) {
            b.labels[i] = track_menu_label(i);
        }
        break;
    case TRK_NOTICE:
        b.labels[0] = "I Understand";
        break;
    case TRK_TA_LIST:
        for (int i = 0; i < TA_RUN_COUNT; i++) {
            b.labels[i] = track_ta_label(i, cfg->metric);
        }
        break;
    case TRK_TA_RUN: {
        LightLock_Lock(&shared.lock);
        b.armed = shared.ta_armed;
        LightLock_Unlock(&shared.lock);
        b.status = b.armed ? "Armed: the timer starts when the car moves."
                           : "Stopped. Tap Start while parked.";
        b.status_color = b.armed ? KIT_GREEN : KIT_INK3;
        break;
    }
    case TRK_SHIFT:
        b.labels[0] = "-";
        b.labels[1] = "+";
        obd_format_grouped(cfg->shift_rpm, lcd, sizeof lcd);
        b.lcd = lcd;
        break;
    case TRK_GMETER:
        b.labels[0] = "Calibrate";
        b.labels[1] = "Reset Peaks";
        break;
    case TRK_REVSYNC: {
        const rev_sync_t *rs = &revsync;
        b.labels[0] = rs->state == RS_DONE ? "Again" : "Start";
        bool locked = rs->state == RS_LOCKED;
        b.status = locked ? "Locked: the speed must read 0." : "Parked: speed 0. Ready.";
        b.status_color = locked ? KIT_AMBER : KIT_GREEN;
        break;
    }
    case TRK_ECO:
        b.labels[0] = "Reset Trip";
        break;
    default:
        break;
    }
    track_draw_bottom(kind, &b);
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
    if (ui->clear_dialog && home_gfx) {
        diag_draw_dialog_bottom(ui->dialog_held_in ? ui->dialog_held : 0);
        cleared_after_confirm = false;
        return;
    }
    if (ui->clear_dialog) {
        draw_clear_dialog();
        cleared_after_confirm = false;
        return;
    }
    if (ui->delete_dialog && home_gfx) {
        diag_draw_question("Delete this drive", "from the SD card?", "Delete",
                           ui->dialog_held_in ? ui->dialog_held : 0);
        cleared_after_confirm = false;
        return;
    }
    if (setup_kind_of(ui) >= 0) {
        draw_setup_gfx_bottom(ui, cfg, now, busy);
        return;
    }
    int lk = log_kind_of(ui);
    if (lk >= 0) {
        int pressed = ui->log_held_in ? ui->log_held : LOG_HIT_NONE;
        if (lk == LOG_INFO) {
            track_draw_info_text(log_info_text(), pressed == LOG_HIT_BACK);
        } else if (lk == LOG_DRIVE) {
            log_draw_drive(pressed, ui->notice);
        } else if (lk == LOG_GRAPH) {
            char value[48] = "GRAPH", showing[64];
            LightLock_Lock(&shared.lock);
            int ncols = shared.graph_ncols, col = shared.graph_col;
            bool loading = shared.graph_loading;
            if (ncols > 0) {
                snprintf(value, sizeof value, "%s", shared.graph_columns[col].name);
            }
            LightLock_Unlock(&shared.lock);
            char a[16], b[16], c[16];
            log_duration((float)ui->graph_t0, a, sizeof a);
            log_duration((float)ui->graph_t1, b, sizeof b);
            log_duration((float)ui->graph_len, c, sizeof c);
            if (loading) {
                snprintf(showing, sizeof showing, "Loading...");
            } else {
                snprintf(showing, sizeof showing, "%s to %s of %s%s", a, b, c, "");
            }
            if (ncols > 1) {
                size_t k = strlen(showing);
                snprintf(showing + k, sizeof showing - k, "   value %d of %d", col + 1, ncols);
            }
            if (flashing(BTN_PAGE_PREV, now)) {
                pressed = LOG_HIT_PREV;
            } else if (flashing(BTN_PAGE_NEXT, now)) {
                pressed = LOG_HIT_NEXT;
            }
            log_draw_graph(value, showing, pressed);
        } else {
            static drive_info_t drives[DRIVE_LIST_MAX];
            static char whens[DRIVE_LIST_MAX][16], cars[DRIVE_LIST_MAX][48], durs[DRIVE_LIST_MAX][16];
            static log_row_t rows[DRIVE_LIST_MAX];
            LightLock_Lock(&shared.lock);
            int count = shared.drive_count;
            bool listed = shared.drives_listed;
            memcpy(drives, shared.drives, sizeof drives[0] * (size_t)count);
            LightLock_Unlock(&shared.lock);
            for (int i = 0; i < count; i++) {
                log_row_parts(&drives[i], whens[i], sizeof whens[i], cars[i], sizeof cars[i], durs[i],
                              sizeof durs[i]);
                rows[i] = (log_row_t){ whens[i], cars[i], durs[i] };
            }
            int held = ui->live_touching && !ui->live_dragged ? ui->live_row_held : -1;
            log_draw_list(rows, count, diag_list_clamp(ui->log_scroll, count), held,
                          ui->live_touching && ui->live_grip, pressed,
                          listed ? "No recorded drives yet." : "Reading the SD card...", ui->notice);
        }
        return;
    }
    int tk = track_kind_of(ui);
    if (tk >= 0) {
        draw_track_gfx_bottom(ui, cfg, now, (track_kind_t)tk);
        return;
    }
    if (ui->ta_dialog) {
        draw_ta_dialog();
        cleared_after_confirm = false;
        return;
    }
    if (ui->delete_dialog) {
        char name[32] = "";
        LightLock_Lock(&shared.lock);
        if (ui->selected >= 0 && ui->selected < shared.drive_count) {
            snprintf(name, sizeof name, "%s", shared.drives[ui->selected].name);
        }
        LightLock_Unlock(&shared.lock);
        draw_log_delete_dialog(name);
        cleared_after_confirm = false;
        return;
    }
    if (ui->screen == SCREEN_HOME && home_gfx) {
        home_view_t v;
        LightLock_Lock(&shared.lock);
        v.cel_known = shared.diag.have_readiness;
        v.connected = shared.connected;
        v.codes = shared.diag.have_stored ? shared.diag.stored.count : -1;
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
        char car[GARAGE_NICK_MAX + 48];
        v.car = home_garage_line(car, sizeof car);
        home_draw(&v);
        return;
    }
    if (ui->screen == SCREEN_LIVE_MENU && home_gfx) {
        static const char *names[LIVE_VIEW_COUNT];
        for (int i = 0; i < LIVE_VIEW_COUNT; i++) {
            names[i] = live_view_name(i);
        }
        int held = ui->live_touching && !ui->live_dragged ? ui->live_row_held : -1;
        for (int i = 0; i < LIVE_VIEW_COUNT; i++) {
            if (flashing(BTN_LIVE_VIEW - i, now)) {
                held = i;
            }
        }
        live_draw_list(names, LIVE_VIEW_COUNT, ui->live_scroll, held, ui->live_touching && ui->live_grip,
                       flashing(BTN_BACK, now));
        return;
    }
    if (ui->screen == SCREEN_TRIM && home_gfx) {
        trim_hit_t pressed = ui->trim_held_in ? (trim_hit_t)ui->trim_held : TRIM_HIT_NONE;
        if (ui->trim_info) {
            trim_draw_info(pressed == TRIM_HIT_BACK || flashing(BTN_BACK, now));
            return;
        }
        static obd_live_t lv;
        trim_controls_t c;
        memset(&c, 0, sizeof c);
        LightLock_Lock(&shared.lock);
        lv = shared.live;
        bool have = shared.live_have;
        c.pids = have ? shared.live.count : shared.live_want_count;
        c.rate = have ? shared.live_rate : 0.0f;
        c.connected = shared.connected;
        c.recording = shared.rec_want || shared.rec_active;
        c.rec_ms = shared.rec_active ? now - shared.rec_started_ms : 0;
        LightLock_Unlock(&shared.lock);
        trim_state_t ts;
        const char *hints[TRIM_HINTS_MAX];
        if (have) {
            trim_state_from_live(&lv, &ts);
            c.nhints = trim_hints(&ts, hints, TRIM_HINTS_MAX);
            c.hints_ok = trim_all_normal(&ts);
        }
        c.hints = hints;
        c.pressed = pressed;
        if (flashing(BTN_LIVE_STOP, now)) {
            c.pressed = TRIM_HIT_STOP;
        } else if (flashing(BTN_LIVE_RECORD, now)) {
            c.pressed = TRIM_HIT_RECORD;
        }
        trim_draw_bottom(&c);
        return;
    }
    if (ui->screen == SCREEN_DIAG && home_gfx) {
        diag_hit_t p = ui->diag_held_in ? (diag_hit_t)ui->diag_held : DIAG_HIT_NONE;
        if (flashing(BTN_DIAG_SCAN, now)) {
            p = DIAG_HIT_RIGHT;   /* X */
        }
        diag_draw_home_bottom(p, ui->notice);
        return;
    }
    if (ui->screen == SCREEN_DIAG_PAGE) {
        diag_rows_refresh(ui->tab);
        diag_page_view_t v;
        memset(&v, 0, sizeof v);
        v.rows = diag_rows;
        v.n = diag_rows_n;
        v.scroll = diag_list_clamp(ui->diag_scroll, diag_rows_n);
        v.selected = ui->selected < diag_rows_n ? ui->selected : -1;
        v.pressed_row = ui->live_touching && !ui->live_dragged ? ui->live_row_held : -1;
        v.grip_pressed = ui->live_touching && ui->live_grip;
        v.pressed_tab = ui->diag_held_in ? (diag_hit_t)ui->diag_held : DIAG_HIT_NONE;
        if (flashing(BTN_CLEAR_ASK, now)) {
            v.pressed_tab = DIAG_HIT_RIGHT;   /* X */
        }
        v.right_tab = ui->tab != DIAG_TAB_READINESS;
        LightLock_Lock(&shared.lock);
        v.empty = diag_empty_text(&shared.diag, ui->tab);
        LightLock_Unlock(&shared.lock);
        v.notice = ui->notice;
        diag_draw_page_bottom(&v);
        return;
    }
    if (ui->screen == SCREEN_LIVE && home_gfx) {
        live_controls_t c;
        memset(&c, 0, sizeof c);
        LightLock_Lock(&shared.lock);
        c.pids = shared.live_have ? shared.live.count : shared.live_want_count;
        c.rate = shared.live_have ? shared.live_rate : 0.0f;
        c.recording = shared.rec_want || shared.rec_active;
        c.rec_ms = shared.rec_active ? now - shared.rec_started_ms : 0;
        LightLock_Unlock(&shared.lock);
        c.custom = ui->live_view == LIVE_VIEW_CUSTOM;
        static const struct { int action; live_hit_t hit; } press[] = {
            { BTN_BACK, LIVE_HIT_BACK }, { BTN_PAGE_PREV, LIVE_HIT_PREV },
            { BTN_PAGE_NEXT, LIVE_HIT_NEXT }, { BTN_LIVE_STOP, LIVE_HIT_STOP },
            { BTN_LIVE_RECORD, LIVE_HIT_RECORD }, { BTN_LIVE_EDIT, LIVE_HIT_EDIT },
        };
        c.pressed = LIVE_HIT_NONE;
        for (unsigned i = 0; i < sizeof press / sizeof press[0]; i++) {
            if (flashing(press[i].action, now)) {
                c.pressed = press[i].hit;
            }
        }
        if (ui->live_held != LIVE_HIT_NONE) {
            c.pressed = ui->live_held_in ? (live_hit_t)ui->live_held : LIVE_HIT_NONE;
        }
        live_draw_view_bottom(&c);
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

    if (ui->screen == SCREEN_TRACK) {
        print_row(2, BOTTOM_COLS, "TRACK  Track modes");
        draw_list(track_buttons, TRACK_BUTTON_COUNT, now, busy, 0, false);
        print_row(22, BOTTOM_COLS, "The top screen says what each one does.");
        print_row(23, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        return;
    }
    if (ui->screen == SCREEN_ACHIEVE) {
        print_row(2, BOTTOM_COLS, "ACHIEVEMENTS");
        print_row(4, BOTTOM_COLS, "Stickers for milestones: scans, clean");
        print_row(5, BOTTOM_COLS, "cars, recorded miles and track modes.");
        print_row(7, BOTTOM_COLS, "They unlock by themselves and pop up");
        print_row(8, BOTTOM_COLS, "here at the bottom; the list is on the");
        print_row(9, BOTTOM_COLS, "top screen. Sticker art comes in v0.5.");
        print_row(16, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        draw_list(ach_buttons, ACH_BUTTON_COUNT, now, busy, 0, false);
        return;
    }
    if (ui->screen == SCREEN_TRIP) {
        print_row(2, BOTTOM_COLS, "TRIP METER");
        print_row(4, BOTTOM_COLS, "Distance from the car's own odometer");
        print_row(5, BOTTOM_COLS, "since you last tapped RESET TRIP.");
        print_row(7, BOTTOM_COLS, "Connected to this car, it reads the");
        print_row(8, BOTTOM_COLS, "odometer live; otherwise it uses the");
        print_row(9, BOTTOM_COLS, "last reading Gauge Boy saved.");
        print_row(16, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        draw_list(trip_buttons, TRIP_BUTTON_COUNT, now, busy, 0, false);
        return;
    }
    if (ui->screen == SCREEN_MILEAGE) {
        garage_car_t c;
        double km;
        bool live;
        card_odometer(ui->page, &c, &km, &live);
        static char text[sizeof shared.miles_text];
        static mileage_list_t list;
        LightLock_Lock(&shared.lock);
        memcpy(text, shared.miles_text, sizeof text);
        LightLock_Unlock(&shared.lock);
        mileage_read(text, c.key, &list);
        clear_rows(1, 24);
        snprintf(line, sizeof line, "MILEAGE LOG: %d entr%s", list.n, list.n == 1 ? "y" : "ies");
        print_row(2, 23, line);
        print_row(4, 23, list.n > 0 ? "Tap one to EDIT it." : "NEW ENTRY to start.");
        for (int slot = 0; slot < LIST_PAGE_ROWS; slot++) {
            int k = ui->miles_page * LIST_PAGE_ROWS + slot;   /* newest first */
            if (k >= list.n) {
                break;
            }
            const mileage_entry_t *e = &list.e[list.n - 1 - k];
            double d = e->end_km - e->start_km, econ = 0.0;
            bool has = cfg->metric ? mileage_l100(d, e->fuel_l, &econ)
                                   : mileage_mpg(d, e->fuel_l, &econ);
            char odo[24], row[64];
            obd_format_grouped((long long)((cfg->metric ? e->end_km : e->end_km / MILEAGE_KM_MI) +
                                           0.5), odo, sizeof odo);
            snprintf(row, sizeof row, "%.5s  %.12s %s  %.1f %s", e->date + 5, odo,
                     cfg->metric ? "km" : "mi", has ? econ : 0.0, cfg->metric ? "L/100" : "mpg");
            draw_list_row(slot, row, k == ui->miles_sel);
        }
        print_row(24, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        snprintf(line, sizeof line, "%d/%d", ui->miles_page + 1, page_count(list.n));
        printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
        bool armed = now < ui->miles_del_armed;
        for (int i = 0; i < MILES_BUTTON_COUNT; i++) {
            button_t b = miles_buttons[i];
            if (b.action == BTN_MILES_DEL && armed) {
                b.label = "AGAIN";
            }
            draw_button_ex(&b, flashing(b.action, now), b.action == BTN_MILES_DEL && armed, false);
        }
        return;
    }
    if (ui->screen == SCREEN_GARAGE) {
        print_row(2, BOTTOM_COLS, "GARAGE");
        print_row(4, BOTTOM_COLS, "A card for each car Gauge Boy knows:");
        print_row(5, BOTTOM_COLS, "by its VIN, or the make, model and");
        print_row(6, BOTTOM_COLS, "year set in SETUP > VEHICLE.");
        print_row(8, BOTTOM_COLS, "Logged distance counts recorded");
        print_row(9, BOTTOM_COLS, "drives (LIVE or TRIM, REC).");
        print_row(11, BOTTOM_COLS, "< > for the other cars.");
        print_row(18, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        draw_list(garage_buttons, GARAGE_BUTTON_COUNT, now, busy, 0, false);
        return;
    }
    if (ui->screen == SCREEN_ECO) {
        print_row(2, BOTTOM_COLS, "ECO CHALLENGE");
        print_row(4, BOTTOM_COLS, "Drive smoothly for a high score:");
        print_row(5, BOTTOM_COLS, "gentle starts and stops, RPM under");
        print_row(6, BOTTOM_COLS, "2800, and no long idling (the first");
        print_row(7, BOTTOM_COLS, "minute of a stop is free).");
        print_row(9, BOTTOM_COLS, "Rank: S 90+, A 80+, B 65+, else C.");
        print_row(10, BOTTOM_COLS, "The trip runs while this screen is");
        print_row(11, BOTTOM_COLS, "open; BACK keeps a best score.");
        print_row(16, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        draw_list(eco_buttons, ECO_BUTTON_COUNT, now, busy, 0, false);
        return;
    }
    if (ui->screen == SCREEN_REVSYNC) {
        print_row(2, BOTTOM_COLS, "REV SYNC");
        print_row(4, BOTTOM_COLS, "Parked, engine warm, in neutral or");
        print_row(5, BOTTOM_COLS, "park. Each round shows a target RPM;");
        print_row(6, BOTTOM_COLS, "at GO, blip the throttle so the peak");
        print_row(7, BOTTOM_COLS, "lands on it.");
        print_row(9, BOTTOM_COLS, "PERFECT within 75 rpm, GREAT 200,");
        print_row(10, BOTTOM_COLS, "GOOD 400, OK 700. GREAT or better in");
        print_row(11, BOTTOM_COLS, "a row builds a combo.");
        print_row(13, BOTTOM_COLS, "Locked unless the speed reads 0.");
        print_row(16, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        for (int i = 0; i < RS_BUTTON_COUNT; i++) {
            button_t b = rs_buttons[i];
            if (b.action == BTN_RS_START && revsync.state == RS_DONE) {
                b.label = "AGAIN";
            }
            draw_button_ex(&b, flashing(b.action, now), false, false);
        }
        return;
    }
    if (ui->screen == SCREEN_GMETER) {
        print_row(2, BOTTOM_COLS, "G-METER");
        print_row(4, BOTTOM_COLS, "Mount the 3DS firmly, screen facing");
        print_row(5, BOTTOM_COLS, "you, then tap CALIBRATE parked with");
        print_row(6, BOTTOM_COLS, "the engine idling (the 3DS still).");
        print_row(8, BOTTOM_COLS, "Up on the circle is speeding up, down");
        print_row(9, BOTTOM_COLS, "is braking, sides are cornering.");
        print_row(10, BOTTOM_COLS, "Which way is forward is learned on");
        print_row(11, BOTTOM_COLS, "the first launch (OBD speed).");
        print_row(13, BOTTOM_COLS, "Calibrate again after moving it.");
        print_row(14, BOTTOM_COLS, "The sensor numbers wobble a little at");
        print_row(15, BOTTOM_COLS, "rest: that's normal and averaged out.");
        print_row(16, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        draw_list(gm_buttons, GM_BUTTON_COUNT, now, busy, 0, false);
        return;
    }
    if (ui->screen == SCREEN_SHIFT) {
        print_row(2, BOTTOM_COLS, "SHIFT LIGHT");
        print_row(4, BOTTOM_COLS, "The LEDs on the top screen fill as the");
        print_row(5, BOTTOM_COLS, "RPM rises and flash at the shift point.");
        print_row(7, BOTTOM_COLS, "Set the shift point while parked, a");
        print_row(8, BOTTOM_COLS, "little under the redline (it's saved).");
        print_row(10, BOTTOM_COLS, "Older cars send RPM a few times a");
        print_row(11, BOTTOM_COLS, "second; the light fills in between.");
        print_row(16, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        draw_list(shift_buttons, SHIFT_BUTTON_COUNT, now, busy, 0, false);
        snprintf(line, sizeof line, "%u rpm", (unsigned)cfg->shift_rpm);
        printf("\x1b[%d;%dH%-13.13s", 21 + 1, 14 + 1, line);
        return;
    }
    if (ui->screen == SCREEN_TIME_ATTACK) {
        static const char *const help[TA_RUN_COUNT][2][2] = {
            { { "0-60: from a standstill. Stop, wait", "for 3, 2, 1, GO, then accelerate." },
              { "0-100: from a standstill. Stop, wait", "for 3, 2, 1, GO, then accelerate." } },
            { { "60-0: drive above 60 mph, then brake", "to a stop. Timing starts under 60." },
              { "100-0: drive above 100 km/h, then", "brake to a stop. Starts under 100." } },
            { { "1/4 mile from a standstill, with the", "trap speed and the 0-60 time." },
              { "1/4 mile from a standstill, with the", "trap speed and the 0-100 time." } },
        };
        int run = ui->ta_run >= 0 && ui->ta_run < TA_RUN_COUNT ? ui->ta_run : 0;
        int m = cfg->metric ? 1 : 0;
        print_row(2, BOTTOM_COLS, "TIME ATTACK: pick a run while parked.");
        print_row(4, BOTTOM_COLS, help[run][m][0]);
        print_row(5, BOTTOM_COLS, help[run][m][1]);
        print_row(7, BOTTOM_COLS, "The timer starts and stops by itself,");
        print_row(8, BOTTOM_COLS, "and arms again once you stop: no touch");
        print_row(9, BOTTOM_COLS, "needed while moving.");
        print_row(11, BOTTOM_COLS, "Best times are kept for each car.");
        print_row(16, BOTTOM_COLS, ui->notice != NULL ? ui->notice : "");
        for (int i = 0; i < TA_BUTTON_COUNT; i++) {
            button_t b = ta_buttons[i];
            int k = BTN_TA_RUN - b.action;
            if (b.label == NULL) {
                b.label = k == TA_RUN_QUARTER ? "1/4 MILE" : ta_run_key((ta_run_t)k, cfg->metric);
            }
            draw_button_ex(&b, flashing(b.action, now), false, b.label != NULL && b.action != BTN_BACK &&
                           k == run);
        }
        return;
    }
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
        LightLock_Lock(&shared.lock);
        wifi_step_t step = (wifi_step_t)shared.guide_step;
        LightLock_Unlock(&shared.lock);
        print_row(10, BOTTOM_COLS, checking ? wifi_step_progress(step) : check[0][0] ? "Last CHECK:"
                                                        : "Tap CHECK after step 5.");
        for (int i = 0; i < GUIDE_CHECK_LINES; i++) {
            print_row(12 + i, BOTTOM_COLS, checking ? "" : check[i]);
        }
        draw_list(guide_buttons, GUIDE_BUTTON_COUNT, now, busy, 0, false);
        snprintf(line, sizeof line, "%d/%d", ui->page + 1, WIFI_GUIDE_STEPS);
        printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
        return;
    }
    if (ui->screen == SCREEN_GRAPH) {
        LightLock_Lock(&shared.lock);
        int ncols = shared.graph_ncols;
        bool loading = shared.graph_loading;
        LightLock_Unlock(&shared.lock);
        clear_rows(1, 24);
        print_row(1, BOTTOM_COLS, "GRAPH: one value against time");
        char a[16], b[16], c[16];
        unsigned t0 = (unsigned)(ui->graph_t0 + 0.5), t1 = (unsigned)(ui->graph_t1 + 0.5);
        unsigned len = (unsigned)(ui->graph_len + 0.5);
        snprintf(a, sizeof a, "%u:%02u", t0 / 60, t0 % 60);
        snprintf(b, sizeof b, "%u:%02u", t1 / 60, t1 % 60);
        snprintf(c, sizeof c, "%u:%02u", len / 60, len % 60);
        char showing[64];
        snprintf(showing, sizeof showing, "Showing %s to %s of %s", a, b, c);
        print_row(11, BOTTOM_COLS, loading ? "Loading..." : showing);
        print_row(13, BOTTOM_COLS, "<< >>: earlier / later");
        print_row(14, BOTTOM_COLS, "ZOOM - / +: more / less of the drive");
        print_row(15, BOTTOM_COLS, "< >: other values; ALL: whole drive");
        print_row(17, BOTTOM_COLS, "The line is smoothed between readings;");
        print_row(18, BOTTOM_COLS, "the faint band shows short spikes.");
        draw_list(graph_buttons, GRAPH_BUTTON_COUNT, now, busy, 0, false);
        snprintf(line, sizeof line, "%d/%d", ui->graph_col + 1, ncols);
        printf("\x1b[%d;15H%-3.3s", 27 + 1, line);
        return;
    }
    if (ui->screen == SCREEN_LOG) {
        LightLock_Lock(&shared.lock);
        int count = shared.drive_count;
        bool listed = shared.drives_listed;
        clear_rows(1, 24);
        snprintf(line, sizeof line, "LOG: %d recorded drive%s", count, count == 1 ? "" : "s");
        print_row(2, BOTTOM_COLS, listed ? line : "LOG: reading the SD card...");
        print_row(4, BOTTOM_COLS, count > 0 ? "Tap one for its details on top." : "");
        for (int slot = 0; slot < LIST_PAGE_ROWS; slot++) {
            int k = ui->page * LIST_PAGE_ROWS + slot;
            if (k >= count) {
                break;
            }
            char row[BOTTOM_COLS + 1];
            drive_info_row(&shared.drives[k], row, sizeof row, 37);
            draw_list_row(slot, row, k == ui->selected);
        }
        LightLock_Unlock(&shared.lock);
        draw_list(log_buttons, LOG_BUTTON_COUNT, now, busy, 0, false);
        snprintf(line, sizeof line, "%d/%d", ui->page + 1, page_count(count));
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
            if (b->w < 20) {   /* < > BACK: buttons; the rest are bars with values */
                draw_button(b, flashing(b->action, now), b->action >= 0 && b->action == busy);
                continue;
            }
            char value[40];
            bar_value(b->action, cfg, value, sizeof value);
            draw_bar(b, value, active);
        }
        if (ui->screen == SCREEN_SETUP_WIFI) {
            /* CHECK & CONNECT: the step in progress, or what it found. */
            char check[GUIDE_CHECK_LINES][40];
            LightLock_Lock(&shared.lock);
            memcpy(check, shared.guide_check, sizeof check);
            bool checking = shared.guide_checking, ok = shared.guide_ok;
            wifi_step_t step = (wifi_step_t)shared.guide_step;
            LightLock_Unlock(&shared.lock);
            if (checking) {
                print_row(19, BOTTOM_COLS, wifi_step_progress(step));
                for (int i = 1; i < GUIDE_CHECK_LINES; i++) {
                    print_row(19 + i, BOTTOM_COLS, "");
                }
            } else if (check[0][0] == '\0') {
                print_row(19, BOTTOM_COLS, "CHECK & CONNECT tries each step and");
                print_row(20, BOTTOM_COLS, "says where it stops. New to this?");
                print_row(21, BOTTOM_COLS, "Start with the CONNECTION GUIDE.");
            } else {
                for (int i = 0; i < GUIDE_CHECK_LINES; i++) {
                    print_row(19 + i, BOTTOM_COLS, "");
                    if (i == 0) {
                        printf("\x1b[%d;1H%s%-39.39s\x1b[0m", 19 + 1, ok ? "\x1b[32m" : "\x1b[31m",
                               check[0]);
                    } else {
                        print_row(19 + i, BOTTOM_COLS, check[i]);
                    }
                }
            }
            printf("\x1b[28;11H%-28.28s", "L/R: scroll the log above");
        } else if (ui->screen == SCREEN_SETUP_VEHICLE) {
            print_row(22, BOTTOM_COLS, "Values from the VIN win; yours are for");
            print_row(23, BOTTOM_COLS, "cars with no VIN. Engine: for TRACK's");
            print_row(24, BOTTOM_COLS, "fuel estimate on cars without a MAF.");
        } else {
            printf("\x1b[28;11H%-28.28s", "L/R: scroll the log above");
        }
        return;
    }
    if (ui->screen == SCREEN_ABOUT) {
        clear_rows(1, 29);
        print_row(1, BOTTOM_COLS, "SETUP > OTHER > About");
        print_row(3, BOTTOM_COLS, "About Gauge Boy is on the top screen.");
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
        LightLock_Lock(&shared.lock);
        bool rec = shared.rec_active;
        bool rec_want = shared.rec_want;
        unsigned rec_secs = rec ? (unsigned)((now - shared.rec_started_ms) / 1000u) : 0;
        char rec_file[sizeof shared.rec_file];
        memcpy(rec_file, shared.rec_file, sizeof rec_file);
        LightLock_Unlock(&shared.lock);
        if (rec) {
            print_row(19, BOTTOM_COLS, "Recording the trims to drives/");
            print_row(20, BOTTOM_COLS, rec_file);
            print_row(22, BOTTOM_COLS, "Safe to leave running; REC to stop.");
        } else {
            print_row(19, BOTTOM_COLS, "REC saves the trims to the SD card");
            print_row(20, BOTTOM_COLS, "(LOG lists it, GRAPH replays it).");
            print_row(22, BOTTOM_COLS, "");
        }
        char rec_label[16];
        snprintf(rec_label, sizeof rec_label, "REC %u:%02u", rec_secs / 60, rec_secs % 60);
        for (int i = 0; i < TRIM_BUTTON_COUNT; i++) {
            button_t b = trim_buttons[i];
            if (b.action == BTN_LIVE_TOGGLE) {
                b.label = on ? "STOP" : "START";
            }
            if (b.action == BTN_LIVE_REC) {
                b.label = rec ? rec_label : rec_want ? "REC ..." : "REC";
            }
            draw_button_ex(&b, flashing(b.action, now), b.action == BTN_LIVE_REC && rec,
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
        LightLock_Lock(&shared.lock);
        bool rec = shared.rec_active;
        bool rec_want = shared.rec_want;
        unsigned rec_secs = rec ? (unsigned)((now - shared.rec_started_ms) / 1000u) : 0;
        char rec_file[sizeof shared.rec_file];
        memcpy(rec_file, shared.rec_file, sizeof rec_file);
        LightLock_Unlock(&shared.lock);
        if (msg[0] != '\0') {
            print_row(11, BOTTOM_COLS, msg);
        }
        if (rec) {
            print_row(19, 25, "Recording to drives/");
            print_row(20, 25, rec_file);
            print_row(22, 25, "Safe to leave running;");
            print_row(23, 25, "tap REC again to stop.");
        } else {
            print_row(19, 25, "REC: save this view to");
            print_row(20, 25, "the SD card (CSV).");
            print_row(22, 25, "");
            print_row(23, 25, "");
        }
        char rec_label[16];
        snprintf(rec_label, sizeof rec_label, "REC %u:%02u", rec_secs / 60, rec_secs % 60);
        for (int i = 0; i < LIVE_BUTTON_COUNT; i++) {
            button_t b = live_buttons[i];
            if (b.action == BTN_LIVE_TOGGLE) {
                b.label = on ? "STOP" : "START";
            }
            if (b.action == BTN_LIVE_REC) {
                b.label = rec ? rec_label : rec_want ? "REC ..." : "REC";
            }
            draw_button_ex(&b, flashing(b.action, now), b.action == BTN_LIVE_REC && rec,
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
        fprintf(log_file, "\n=== Gauge Boy %s session, %s ===\n", GB_VERSION_TEXT, when);
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

/* The SD card folder was sdmc:/3ds/gaugeboy/ before the app was named
 * "Gauge Boy": move its files (settings, logs, drives, code history) to
 * CONFIG_DIR once. A .3dsx stays: it may be the copy that is running. */
#define OLD_DIR "sdmc:/3ds/gaugeboy"
static char migrate_note[200];

static void migrate_old_folder(void)
{
    struct stat st;
    if (stat(OLD_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }
    static char names[64][64];
    int n = 0;
    DIR *dir = opendir(OLD_DIR);
    if (dir == NULL) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL && n < 64) {
        if (e->d_name[0] != '.') {
            snprintf(names[n++], sizeof names[0], "%.63s", e->d_name);
        }
    }
    closedir(dir);
    mkdir(CONFIG_DIR, 0777);
    int moved = 0, kept = 0, failed = 0;
    for (int i = 0; i < n; i++) {
        size_t len = strlen(names[i]);
        bool program = len > 5 && (strcmp(names[i] + len - 5, ".3dsx") == 0 ||
                                   strcmp(names[i] + len - 5, ".smdh") == 0);
        char from[96], to[96];
        snprintf(from, sizeof from, "%s/%.63s", OLD_DIR, names[i]);
        snprintf(to, sizeof to, "%s/%.63s", CONFIG_DIR, names[i]);
        if (program || stat(to, &st) == 0) {
            kept++;   /* the program, or the new folder already has one */
        } else if (rename(from, to) == 0) {
            moved++;
        } else {
            failed++;
        }
    }
    rmdir(OLD_DIR);   /* only if nothing is left */
    if (moved + failed > 0) {
        snprintf(migrate_note, sizeof migrate_note,
                 "%sMoved %d item%s from %s/ to %s/ (the new name)%s%s", failed ? "! " : "",
                 moved, moved == 1 ? "" : "s", OLD_DIR, CONFIG_DIR,
                 failed ? "; some could not be moved" : "",
                 kept ? "; the old folder still has the .3dsx" : "");
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

    migrate_old_folder();
    open_log_file();
    spike_init(&spike, &transport, &elm, log_sink, NULL);
    spike_logf(&spike, "Gauge Boy %s", GB_VERSION_TEXT);
    if (migrate_note[0] != '\0') {
        spike_logf(&spike, "%s", migrate_note);
    }
    home_gfx = home_draw_init();
    bool ptmu_ok = R_SUCCEEDED(ptmuInit());   /* HOME's battery level */
    graph_gfx = home_gfx && graph_draw_init();
    if (!home_gfx) {
        spike_logf(&spike, "! HOME graphics could not start (fonts in romfs); using text HOME");
    }
    if (log_file == NULL) {
        spike_logf(&spike, "! Could not open %s; log is on screen only", LOG_PATH);
    }

    load_config(&spike, &cfg);
    spike.units = cfg.metric ? OBD_UNITS_METRIC : OBD_UNITS_US;
    diag_view_set_units(spike.units);
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
    } else {
        enqueue(BTN_GARAGE_SCAN);   /* HOME's car card: odometer, last drive */
    }

    ui_state_t ui = { SCREEN_HOME, DIAG_TAB_STORED, -1, 0, NULL, false, false, -1, SCREEN_SETUP,
                      false, LIVE_VIEW_ENGINE, SCREEN_HOME, false, 0, 0.0, 0.0, 0.0,
                      false, false, TA_RUN_LAUNCH, SCREEN_HOME, 0, 0, -1, 0, false, false, 0, 0,
                      -1, false, 0, LIVE_HIT_NONE, false, 0, DIAG_HIT_NONE, false, 0, false,
                      false, TRIM_HIT_NONE, false, false, false, TRK_HIT_NONE, false, 0,
                      false, false, LOG_HIT_NONE, false, 0, SET_HIT_NONE, false, 0 };
    gm_vec_t gm_raw = { 0, 0, 0 };   /* G-METER: the latest sensor reading */
    live_menu_count();
    uint64_t notice_until = 0;
    int scroll = 0;                /* log (SETUP, TEST) */
    unsigned drawn_version = ~0u;
    unsigned drawn_diag_version = ~0u;
    unsigned drawn_live_version = ~0u;
    unsigned drawn_live_ui_version = ~0u;
    unsigned drawn_drive_version = ~0u;
    unsigned drawn_graph_version = ~0u;
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
        if (ui.screen == SCREEN_GMETER) {
            gmeter_frame(&spike, now, &gm_raw);
        }
        if (ui.screen == SCREEN_ECO) {
            eco_frame((float)cfg.engine_dl / 10.0f);
        }
        if (ui.screen == SCREEN_ECO || ui.screen == SCREEN_REVSYNC) {
            /* The car may be identified after the screen opened: read its
             * best score again once it is. */
            static bool asked;
            LightLock_Lock(&shared.lock);
            bool unknown = !shared.rs_car_known, have_car = shared.diag.have_vin ||
                           cfg.car_make[0] != '\0' || cfg.car_model[0] != '\0';
            LightLock_Unlock(&shared.lock);
            if (unknown && have_car && !asked && worker != NULL) {
                asked = true;
                enqueue(BTN_RS_BEST);
            }
            if (!unknown) {
                asked = false;
            }
        }
        if (ui.screen == SCREEN_REVSYNC) {
            static rs_state_t rs_drawn = RS_LOCKED;
            if (revsync_frame(now) && worker != NULL) {
                LightLock_Lock(&shared.lock);
                shared.rs_save_score = revsync.score;
                LightLock_Unlock(&shared.lock);
                enqueue(BTN_RS_BEST);
            }
            if ((revsync.state == RS_DONE || revsync.state == RS_IDLE) !=
                (rs_drawn == RS_DONE || rs_drawn == RS_IDLE)) {
                bottom_dirty = true;   /* START / AGAIN */
            }
            rs_drawn = revsync.state;
        }

        if (down & KEY_START) {
            break;
        }
        LightLock_Lock(&shared.lock);
        bool confirming = shared.confirm;
        bool perm_hidden = shared.diag.permanent_unsupported;
        LightLock_Unlock(&shared.lock);
        if (perm_hidden != ui.permanent_hidden) {
            ui.permanent_hidden = perm_hidden;
            if (perm_hidden && ui.tab == DIAG_TAB_PERMANENT && ui.screen != SCREEN_DIAG_PAGE) {
                ui.tab = DIAG_TAB_STORED;
            }
            bottom_dirty = bottom_full = top_dirty = true;
        }

        static screen_t info_screen;
        if (ui.screen != info_screen) {   /* a new screen opens on its controls, not Info */
            info_screen = ui.screen;
            ui.track_info = false;
            if (ui.screen != SCREEN_TIME_ATTACK) {
                ui.ta_open = false;
            }
        }
        int action = 0;
        bool tapped = false;
        if ((down & KEY_TOUCH) && ui.delete_dialog && home_gfx) {
            touchPosition tp;
            hidTouchRead(&tp);
            ui.dialog_held = diag_dialog_hit(tp.px, tp.py);
            ui.dialog_held_in = ui.dialog_held != 0;
            bottom_dirty = true;
        } else if ((down & KEY_TOUCH) && ui.delete_dialog) {
            touchPosition tp;
            hidTouchRead(&tp);
            int i = button_at(log_delete_buttons, 2, tp.px, tp.py);
            if (i >= 0) {
                if (log_delete_buttons[i].action == BTN_LOG_YES && worker != NULL) {
                    LightLock_Lock(&shared.lock);
                    if (ui.selected >= 0 && ui.selected < shared.drive_count) {
                        snprintf(shared.drive_delete, sizeof shared.drive_delete, "%s",
                                 shared.drives[ui.selected].name);
                    }
                    LightLock_Unlock(&shared.lock);
                    enqueue(BTN_LOG_DELETE);
                    ui.selected = -1;
                    top_dirty = true;
                }
                ui.delete_dialog = false;
                bottom_full = true;
            }
        } else if ((down & KEY_TOUCH) && ui.ta_dialog && home_gfx) {
            touchPosition tp;
            hidTouchRead(&tp);
            ui.track_held = track_hit(TRK_NOTICE, tp.px, tp.py);
            ui.track_held_in = ui.track_held != TRK_HIT_NONE;
            bottom_dirty = true;
        } else if ((down & KEY_TOUCH) && ui.ta_dialog) {
            touchPosition tp;
            hidTouchRead(&tp);
            int i = button_at(ta_warn_buttons, TA_WARN_BUTTON_COUNT, tp.px, tp.py);
            if (i >= 0) {
                ui.ta_dialog = false;
                if (ta_warn_buttons[i].action == BTN_TA_WARN_OK) {
                    ui.ta_warned = true;
                    ui.screen = SCREEN_TIME_ATTACK;
                    ta_start(ui.ta_run, cfg.metric, true);
                    top_dirty = true;
                }
                bottom_full = true;
            }
        } else if ((down & KEY_TOUCH) && ui.clear_dialog && home_gfx) {
            /* Pressed while held; acts when the finger lifts on it. */
            touchPosition tp;
            hidTouchRead(&tp);
            ui.dialog_held = diag_dialog_hit(tp.px, tp.py);
            ui.dialog_held_in = ui.dialog_held != 0;
            bottom_dirty = true;
        } else if ((down & KEY_TOUCH) && ui.clear_dialog) {
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
            case SCREEN_LOG:   list = log_buttons;   count = LOG_BUTTON_COUNT;   break;
            case SCREEN_GRAPH: list = graph_buttons; count = GRAPH_BUTTON_COUNT; break;
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
            case SCREEN_TRACK:     list = track_buttons;     count = TRACK_BUTTON_COUNT;     break;
            case SCREEN_TIME_ATTACK: list = ta_buttons;      count = TA_BUTTON_COUNT;        break;
            case SCREEN_SHIFT:     list = shift_buttons;     count = SHIFT_BUTTON_COUNT;     break;
            case SCREEN_GMETER:    list = gm_buttons;        count = GM_BUTTON_COUNT;        break;
            case SCREEN_REVSYNC:   list = rs_buttons;        count = RS_BUTTON_COUNT;        break;
            case SCREEN_ECO:       list = eco_buttons;       count = ECO_BUTTON_COUNT;       break;
            case SCREEN_GARAGE:    list = garage_buttons;    count = GARAGE_BUTTON_COUNT;    break;
            case SCREEN_ACHIEVE:   list = ach_buttons;       count = ACH_BUTTON_COUNT;       break;
            case SCREEN_TRIP:      list = trip_buttons;      count = TRIP_BUTTON_COUNT;      break;
            case SCREEN_MILEAGE:   list = miles_buttons;     count = MILES_BUTTON_COUNT;     break;
            case SCREEN_TEST:  break;
            case SCREEN_DIAG_PAGE: break;
            }
            bool gfx_touch = home_gfx && (ui.screen == SCREEN_HOME || ui.screen == SCREEN_LIVE_MENU ||
                                          ui.screen == SCREEN_LIVE || ui.screen == SCREEN_DIAG ||
                                          ui.screen == SCREEN_DIAG_PAGE || ui.screen == SCREEN_TRIM ||
                                          track_kind_of(&ui) >= 0 || log_kind_of(&ui) >= 0 ||
                                          setup_kind_of(&ui) >= 0);
            int i = gfx_touch ? -1 : button_at(list, count, tp.px, tp.py);
            int item = ui.screen == SCREEN_HOME && home_gfx ? home_hit(tp.px, tp.py) : -1;
            diag_hit_t dhit = !gfx_touch                     ? DIAG_HIT_NONE
                            : ui.screen == SCREEN_DIAG      ? diag_home_hit(tp.px, tp.py)
                            : ui.screen == SCREEN_DIAG_PAGE ? diag_page_tab_hit(tp.px, tp.py,
                                                                  ui.tab != DIAG_TAB_READINESS)
                                                            : DIAG_HIT_NONE;
            int tkind = gfx_touch ? track_kind_of(&ui) : -1;
            int lkind = gfx_touch ? log_kind_of(&ui) : -1;
            int skind = gfx_touch ? setup_kind_of(&ui) : -1;
            if (skind >= 0) {
                /* SETUP: controls pressed while held, act on lifting; the make picker
                 * scrolls and a tap on a make saves it. */
                int h = setup_hit((setup_kind_t)skind, tp.px, tp.py);
                if (h == SET_HIT_NONE && skind == SET_MAKE) {
                    int n = make_choice_count();
                    ui.live_touching = true;
                    if (diag_list_bar_hit(tp.px, tp.py) && diag_list_max_scroll(n) > 0) {
                        ui.live_grip = ui.live_dragged = true;
                        ui.live_row_held = -1;
                        ui.live_grip_grab = diag_list_grip_grab(tp.py, ui.make_scroll, n);
                        ui.make_scroll = diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, n);
                    } else {
                        ui.live_grip = ui.live_dragged = false;
                        ui.live_touch_y0 = tp.py;
                        ui.live_scroll0 = ui.make_scroll;
                        ui.live_row_held = diag_list_hit(tp.px, tp.py, ui.make_scroll, n);
                    }
                } else {
                    ui.setup_held = h;
                    ui.setup_held_in = h != SET_HIT_NONE;
                }
                bottom_dirty = true;
            } else if (lkind >= 0) {
                /* LOG: controls pressed while held, act on lifting; the list scrolls and a
                 * tap on a drive opens its page. */
                int h = log_hit((log_kind_t)lkind, tp.px, tp.py);
                if (h == LOG_HIT_NONE && lkind == LOG_LIST) {
                    LightLock_Lock(&shared.lock);
                    int n = shared.drive_count;
                    LightLock_Unlock(&shared.lock);
                    ui.live_touching = true;
                    if (diag_list_bar_hit(tp.px, tp.py) && diag_list_max_scroll(n) > 0) {
                        ui.live_grip = ui.live_dragged = true;
                        ui.live_row_held = -1;
                        ui.live_grip_grab = diag_list_grip_grab(tp.py, ui.log_scroll, n);
                        ui.log_scroll = diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, n);
                    } else {
                        ui.live_grip = ui.live_dragged = false;
                        ui.live_touch_y0 = tp.py;
                        ui.live_scroll0 = ui.log_scroll;
                        ui.live_row_held = diag_list_hit(tp.px, tp.py, ui.log_scroll, n);
                    }
                } else {
                    ui.log_held = h;
                    ui.log_held_in = h != LOG_HIT_NONE;
                }
                bottom_dirty = true;
            } else if (tkind >= 0) {
                /* TRACK's controls: pressed while held, act on lifting; Achievements' list
                 * scrolls like a DIAG page. */
                track_kind_t k = ui.track_info ? TRK_INFO : (track_kind_t)tkind;
                int h = track_hit(k, tp.px, tp.py);
                if (h == TRK_HIT_NONE && k == TRK_ACHIEVE) {
                    ui.live_touching = true;
                    ui.live_row_held = -1;
                    if (diag_list_bar_hit(tp.px, tp.py) && diag_list_max_scroll(ACH_COUNT) > 0) {
                        ui.live_grip = ui.live_dragged = true;
                        ui.live_grip_grab = diag_list_grip_grab(tp.py, ui.ach_scroll, ACH_COUNT);
                        ui.ach_scroll = diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, ACH_COUNT);
                    } else {
                        ui.live_grip = ui.live_dragged = false;
                        ui.live_touch_y0 = tp.py;
                        ui.live_scroll0 = ui.ach_scroll;
                    }
                } else {
                    ui.track_held = h;
                    ui.track_held_in = h != TRK_HIT_NONE;
                }
                bottom_dirty = true;
            } else if (gfx_touch && ui.screen == SCREEN_TRIM) {
                /* TRIM's toggle and tabs: pressed while held, act on lifting. */
                ui.trim_held = trim_hit(tp.px, tp.py, ui.trim_info);
                ui.trim_held_in = ui.trim_held != TRIM_HIT_NONE;
                bottom_dirty = true;
            } else if (dhit != DIAG_HIT_NONE || (gfx_touch && ui.screen == SCREEN_DIAG)) {
                /* DIAG's buttons and tabs: pressed while held, act on lifting. */
                ui.diag_held = dhit;
                ui.diag_held_in = dhit != DIAG_HIT_NONE;
                bottom_dirty = true;
            } else if (gfx_touch && ui.screen == SCREEN_DIAG_PAGE) {
                int n = diag_rows_n;
                if (diag_list_bar_hit(tp.px, tp.py) && diag_list_max_scroll(n) > 0) {
                    ui.live_touching = ui.live_grip = ui.live_dragged = true;
                    ui.live_row_held = -1;
                    ui.live_grip_grab = diag_list_grip_grab(tp.py, ui.diag_scroll, n);
                    ui.diag_scroll = diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, n);
                } else {
                    ui.live_touching = true;
                    ui.live_grip = ui.live_dragged = false;
                    ui.live_touch_y0 = tp.py;
                    ui.live_scroll0 = ui.diag_scroll;
                    /* Codes can be tapped; the readiness rows only scroll. */
                    ui.live_row_held = ui.tab == DIAG_TAB_READINESS
                                           ? -1 : diag_list_hit(tp.px, tp.py, ui.diag_scroll, n);
                }
                bottom_dirty = true;
            } else if (gfx_touch && ui.screen == SCREEN_LIVE_MENU) {
                if (live_back_hit(tp.px, tp.py)) {
                    action = BTN_BACK;
                    tapped = true;
                } else if (live_list_bar_hit(tp.px, tp.py) &&
                           live_list_max_scroll(LIVE_VIEW_COUNT) > 0) {
                    /* The scroll bar: the grip jumps under the finger and follows it. */
                    ui.live_touching = ui.live_grip = ui.live_dragged = true;
                    ui.live_row_held = -1;
                    ui.live_grip_grab = live_list_grip_grab(tp.py, ui.live_scroll, LIVE_VIEW_COUNT);
                    ui.live_scroll = live_list_scroll_for_grip(tp.py - ui.live_grip_grab, LIVE_VIEW_COUNT);
                    bottom_dirty = true;
                } else {
                    /* A tap picks a row when the finger lifts; a drag scrolls. */
                    ui.live_touching = true;
                    ui.live_grip = false;
                    ui.live_dragged = false;
                    ui.live_touch_y0 = tp.py;
                    ui.live_scroll0 = ui.live_scroll;
                    ui.live_row_held = live_list_hit(tp.px, tp.py, ui.live_scroll, LIVE_VIEW_COUNT);
                    bottom_dirty = true;
                }
            } else if (gfx_touch && ui.screen == SCREEN_LIVE) {
                /* As on the 3DS: held = pressed; the control acts when the finger
                 * lifts while still on it. */
                ui.live_held = live_view_hit(tp.px, tp.py, ui.live_view == LIVE_VIEW_CUSTOM);
                ui.live_held_in = ui.live_held != LIVE_HIT_NONE;
                bottom_dirty = true;
            } else if (item >= 0) {
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
            } else if (ui.screen == SCREEN_MILEAGE) {
                int row = tp.py / 8;
                for (int slot = 0; slot < LIST_PAGE_ROWS; slot++) {
                    int top = LIST_FIRST_ROW + slot * LIST_ROW_PITCH;
                    if (row >= top && row < top + LIST_ROW_H) {
                        action = BTN_MILES_ROW - slot;
                        tapped = true;
                    }
                }
            } else if (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_LOG) {
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

        /* A LIVE view's control (graphics): pressed while held, acts on lifting. */
        bool lifted = false;   /* the action came from a lift: it was shown pressed already */
        if (ui.live_held != LIVE_HIT_NONE) {
            if (ui.screen != SCREEN_LIVE || confirming) {
                ui.live_held = LIVE_HIT_NONE;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                bool in = live_view_hit(tp.px, tp.py, ui.live_view == LIVE_VIEW_CUSTOM) == ui.live_held;
                if (in != ui.live_held_in) {
                    ui.live_held_in = in;
                    bottom_dirty = true;
                }
            } else {
                if (ui.live_held_in && !tapped) {
                    switch (ui.live_held) {
                    case LIVE_HIT_BACK:   action = BTN_BACK;        tapped = true; break;
                    case LIVE_HIT_PREV:   action = BTN_PAGE_PREV;   tapped = true; break;
                    case LIVE_HIT_NEXT:   action = BTN_PAGE_NEXT;   tapped = true; break;
                    case LIVE_HIT_STOP:   action = BTN_LIVE_STOP;   tapped = true; break;
                    case LIVE_HIT_RECORD: action = BTN_LIVE_RECORD; tapped = true; break;
                    case LIVE_HIT_EDIT:   action = BTN_LIVE_EDIT;   tapped = true; break;
                    default: break;
                    }
                    lifted = tapped;
                }
                ui.live_held = LIVE_HIT_NONE;
                ui.live_held_in = false;
                bottom_dirty = true;
            }
        }

        /* SETUP's controls (graphics): the same. */
        if (ui.setup_held != SET_HIT_NONE) {
            int skind = setup_kind_of(&ui);
            if (skind < 0 || confirming) {
                ui.setup_held = SET_HIT_NONE;
                ui.setup_held_in = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                bool in = setup_hit((setup_kind_t)skind, tp.px, tp.py) == ui.setup_held;
                if (in != ui.setup_held_in) {
                    ui.setup_held_in = in;
                    bottom_dirty = true;
                }
            } else {
                int h = ui.setup_held, b = h - SET_HIT_BUTTON;
                if (ui.setup_held_in && !tapped) {
                    if (h == SET_HIT_BACK) {
                        action = BTN_BACK;
                    } else if (h == SET_HIT_PREV || h == SET_HIT_NEXT) {
                        action = h == SET_HIT_PREV ? BTN_PAGE_PREV : BTN_PAGE_NEXT;
                    } else if (h == SET_HIT_US || h == SET_HIT_METRIC) {
                        if (cfg.metric != (h == SET_HIT_METRIC)) {
                            action = BTN_UNITS;
                        }
                    } else if (b >= 0) {
                        static const int menu[4] = { BTN_SETUP_WIFI, BTN_SETUP_VEHICLE,
                                                     BTN_SETUP_OTHER, BTN_SETUP_TESTS };
                        static const int wifi[3] = { BTN_GUIDE_CHECK, BTN_WIFI_SLOT, BTN_GUIDE };
                        static const int veh[SET_VEH_COUNT] = {
                            BTN_MAKE_SCREEN, BTN_MODEL_EDIT, BTN_YEAR_PREV, BTN_YEAR_NEXT,
                            BTN_ENGINE_PREV, BTN_ENGINE_NEXT, BTN_DIAG_SCAN,
                        };
                        switch (skind) {
                        case SET_MENU:    action = menu[b]; break;
                        case SET_WIFI:    action = wifi[b]; break;
                        case SET_GUIDE:   action = BTN_GUIDE_CHECK; break;
                        case SET_VEHICLE: action = veh[b]; break;
                        case SET_OTHER:   action = BTN_ABOUT; break;
                        case SET_TESTS:   action = buttons[b].action; break;
                        default: break;
                        }
                    }
                    tapped = lifted = action != 0;
                    top_dirty = true;   /* a setting may have changed */
                }
                ui.setup_held = SET_HIT_NONE;
                ui.setup_held_in = false;
                bottom_dirty = true;
            }
        }

        /* The make picker (graphics): drag or the grip scrolls it; a tap saves the make. */
        if (ui.live_touching && ui.screen == SCREEN_MAKE) {
            int n = make_choice_count();
            if (confirming) {
                ui.live_touching = ui.live_grip = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                int dy = tp.py - ui.live_touch_y0;
                if (!ui.live_grip && !ui.live_dragged && (dy > 6 || dy < -6)) {
                    ui.live_dragged = true;
                    bottom_dirty = true;
                }
                int sc = ui.live_grip ? diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, n)
                       : ui.live_dragged ? diag_list_clamp(ui.live_scroll0 - dy, n) : ui.make_scroll;
                if (sc != ui.make_scroll) {
                    ui.make_scroll = sc;
                    bottom_dirty = true;
                }
            } else {
                ui.live_touching = ui.live_grip = false;
                int k = ui.live_row_held;
                if (!ui.live_dragged && k >= 0 && k < n) {
                    LightLock_Lock(&shared.lock);
                    snprintf(cfg.car_make, sizeof cfg.car_make, "%s", k == 0 ? "" : make_choice_name(k));
                    LightLock_Unlock(&shared.lock);
                    if (worker != NULL) {
                        enqueue(BTN_SET_MAKE);
                    }
                    ui.screen = SCREEN_SETUP_VEHICLE;
                    bottom_full = top_dirty = true;
                }
                ui.live_row_held = -1;
                bottom_dirty = true;
            }
        }

        /* LOG's controls (graphics): the same. */
        if (ui.log_held != LOG_HIT_NONE) {
            int lkind = log_kind_of(&ui);
            if (lkind < 0 || confirming || ui.delete_dialog) {
                ui.log_held = LOG_HIT_NONE;
                ui.log_held_in = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                bool in = log_hit((log_kind_t)lkind, tp.px, tp.py) == ui.log_held;
                if (in != ui.log_held_in) {
                    ui.log_held_in = in;
                    bottom_dirty = true;
                }
            } else {
                int h = ui.log_held, b = h - LOG_HIT_BUTTON;
                if (ui.log_held_in && !tapped) {
                    if (lkind == LOG_INFO) {
                        ui.log_info = false;
                        bottom_full = true;
                    } else if (h == LOG_HIT_INFO) {
                        ui.log_info = true;
                        bottom_full = true;
                    } else if (h == LOG_HIT_BACK && lkind == LOG_DRIVE) {
                        ui.log_open = false;   /* back to the list */
                        bottom_full = top_dirty = true;
                    } else if (h == LOG_HIT_BACK) {
                        action = BTN_BACK;
                    } else if (h == LOG_HIT_PREV || h == LOG_HIT_NEXT) {
                        action = h == LOG_HIT_PREV ? BTN_PAGE_PREV : BTN_PAGE_NEXT;
                    } else if (lkind == LOG_DRIVE && b >= 0) {
                        action = b == 0 ? BTN_LOG_GRAPH : BTN_LOG_ASK;
                    } else if (lkind == LOG_GRAPH && b >= 0) {
                        static const int g[5] = { BTN_GRAPH_EARLY, BTN_GRAPH_OUT, BTN_GRAPH_IN,
                                                  BTN_GRAPH_LATE, BTN_GRAPH_ALL };
                        action = g[b < 5 ? b : 4];
                    }
                    tapped = lifted = action != 0;
                }
                ui.log_held = LOG_HIT_NONE;
                ui.log_held_in = false;
                bottom_dirty = true;
            }
        }

        /* LOG's list (graphics): drag or the grip scrolls it; a tap opens a drive. */
        if (ui.live_touching && ui.screen == SCREEN_LOG) {
            LightLock_Lock(&shared.lock);
            int n = shared.drive_count;
            LightLock_Unlock(&shared.lock);
            if (confirming || ui.delete_dialog) {
                ui.live_touching = ui.live_grip = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                int dy = tp.py - ui.live_touch_y0;
                if (!ui.live_grip && !ui.live_dragged && (dy > 6 || dy < -6)) {
                    ui.live_dragged = true;
                    bottom_dirty = true;
                }
                int sc = ui.live_grip ? diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, n)
                       : ui.live_dragged ? diag_list_clamp(ui.live_scroll0 - dy, n) : ui.log_scroll;
                if (sc != ui.log_scroll) {
                    ui.log_scroll = sc;
                    bottom_dirty = true;
                }
            } else {
                ui.live_touching = ui.live_grip = false;
                if (!ui.live_dragged && ui.live_row_held >= 0 && ui.live_row_held < n) {
                    ui.selected = ui.live_row_held;
                    ui.log_open = true;
                    bottom_full = top_dirty = true;
                }
                ui.live_row_held = -1;
                bottom_dirty = true;
            }
        }

        /* TRACK's controls (graphics): the same. */
        if (ui.track_held != TRK_HIT_NONE) {
            int tkind = track_kind_of(&ui);
            if (tkind < 0 || confirming) {
                ui.track_held = TRK_HIT_NONE;
                ui.track_held_in = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                track_kind_t k = ui.track_info && tkind != TRK_NOTICE ? TRK_INFO : (track_kind_t)tkind;
                bool in = track_hit(k, tp.px, tp.py) == ui.track_held;
                if (in != ui.track_held_in) {
                    ui.track_held_in = in;
                    bottom_dirty = true;
                }
            } else {
                int h = ui.track_held;
                if (ui.track_held_in && !tapped) {
                    int b = h - TRK_HIT_BUTTON;
                    if (ui.track_info && tkind != TRK_NOTICE) {
                        if (h == TRK_HIT_BACK) {
                            ui.track_info = false;   /* back to the screen's controls */
                            bottom_full = true;
                        }
                    } else if (h == TRK_HIT_INFO) {
                        ui.track_info = true;
                        bottom_full = true;
                    } else if (tkind == TRK_NOTICE) {
                        action = h == TRK_HIT_BACK ? BTN_TA_WARN_NO : BTN_TA_WARN_OK;
                        tapped = lifted = true;
                    } else if (h == TRK_HIT_BACK) {
                        action = BTN_BACK;
                        tapped = lifted = true;
                    } else if (h == TRK_HIT_STOP || h == TRK_HIT_START) {
                        action = h == TRK_HIT_START ? BTN_TA_ARM : BTN_TA_DISARM;
                        tapped = lifted = true;
                    } else if (b >= 0) {
                        static const int menu[TRACK_MENU_ITEMS] = {
                            BTN_TRACK_TA, BTN_TRACK_SHIFT, BTN_TRACK_GMETER, BTN_TRACK_REVSYNC,
                            BTN_TRACK_ECO, BTN_TRACK_ACH,
                        };
                        switch (tkind) {
                        case TRK_MENU:    action = menu[b]; break;
                        case TRK_TA_LIST: action = BTN_TA_OPEN - b; break;
                        case TRK_SHIFT:   action = b == 0 ? BTN_SHIFT_DOWN : BTN_SHIFT_UP; break;
                        case TRK_GMETER:  action = b == 0 ? BTN_GM_CAL : BTN_GM_RESET; break;
                        case TRK_REVSYNC: action = BTN_RS_START; break;
                        case TRK_ECO:     action = BTN_ECO_RESET; break;
                        default: break;
                        }
                        tapped = lifted = action != 0;
                    }
                }
                ui.track_held = TRK_HIT_NONE;
                ui.track_held_in = false;
                bottom_dirty = true;
            }
        }

        /* Achievements' list (graphics): drag or the grip scrolls it. */
        if (ui.live_touching && ui.screen == SCREEN_ACHIEVE) {
            if (confirming) {
                ui.live_touching = ui.live_grip = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                int sc = ui.live_grip ? diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, ACH_COUNT)
                                      : diag_list_clamp(ui.live_scroll0 - (tp.py - ui.live_touch_y0), ACH_COUNT);
                if (sc != ui.ach_scroll) {
                    ui.ach_scroll = sc;
                    bottom_dirty = true;
                }
            } else {
                ui.live_touching = ui.live_grip = false;
                bottom_dirty = true;
            }
        }

        /* TRIM's controls (graphics): the same. */
        if (ui.trim_held != TRIM_HIT_NONE) {
            if (ui.screen != SCREEN_TRIM || confirming) {
                ui.trim_held = TRIM_HIT_NONE;
                ui.trim_held_in = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                bool in = (int)trim_hit(tp.px, tp.py, ui.trim_info) == ui.trim_held;
                if (in != ui.trim_held_in) {
                    ui.trim_held_in = in;
                    bottom_dirty = true;
                }
            } else {
                if (ui.trim_held_in && !tapped) {
                    switch (ui.trim_held) {
                    case TRIM_HIT_BACK:
                        if (ui.trim_info) {
                            ui.trim_info = false;   /* back to the controls */
                            bottom_full = true;
                        } else {
                            action = BTN_BACK;
                            tapped = lifted = true;
                        }
                        break;
                    case TRIM_HIT_INFO:
                        ui.trim_info = true;
                        bottom_full = true;
                        break;
                    case TRIM_HIT_STOP:   action = BTN_LIVE_STOP;   tapped = lifted = true; break;
                    case TRIM_HIT_RECORD: action = BTN_LIVE_RECORD; tapped = lifted = true; break;
                    default: break;
                    }
                }
                ui.trim_held = TRIM_HIT_NONE;
                ui.trim_held_in = false;
                bottom_dirty = true;
            }
        }

        /* DIAG's buttons and tabs (graphics): the same. */
        if (ui.diag_held != DIAG_HIT_NONE) {
            bool page = ui.screen == SCREEN_DIAG_PAGE;
            if ((ui.screen != SCREEN_DIAG && !page) || confirming || ui.clear_dialog) {
                ui.diag_held = DIAG_HIT_NONE;
                ui.diag_held_in = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                diag_hit_t h = page ? diag_page_tab_hit(tp.px, tp.py, ui.tab != DIAG_TAB_READINESS)
                                    : diag_home_hit(tp.px, tp.py);
                bool in = (int)h == ui.diag_held;
                if (in != ui.diag_held_in) {
                    ui.diag_held_in = in;
                    bottom_dirty = true;
                }
            } else {
                if (ui.diag_held_in && !tapped) {
                    if (ui.diag_held == DIAG_HIT_BACK) {
                        action = BTN_BACK;
                    } else if (ui.diag_held == DIAG_HIT_RIGHT) {
                        action = page ? BTN_CLEAR_ASK : BTN_DIAG_SCAN;
                    } else {
                        action = BTN_DIAG_PAGE - (ui.diag_held - DIAG_HIT_STORED);
                    }
                    tapped = lifted = true;
                }
                ui.diag_held = DIAG_HIT_NONE;
                ui.diag_held_in = false;
                bottom_dirty = true;
            }
        }

        /* The clear question (graphics): Cancel or Clear Codes when the finger lifts. */
        if (ui.dialog_held != 0) {
            if (!ui.clear_dialog && !ui.delete_dialog) {
                ui.dialog_held = 0;
                ui.dialog_held_in = false;
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                bool in = diag_dialog_hit(tp.px, tp.py) == ui.dialog_held;
                if (in != ui.dialog_held_in) {
                    ui.dialog_held_in = in;
                    bottom_dirty = true;
                }
            } else {
                if (ui.dialog_held_in && ui.delete_dialog) {
                    if (ui.dialog_held == 2 && worker != NULL) {
                        LightLock_Lock(&shared.lock);
                        if (ui.selected >= 0 && ui.selected < shared.drive_count) {
                            snprintf(shared.drive_delete, sizeof shared.drive_delete, "%s",
                                     shared.drives[ui.selected].name);
                        }
                        LightLock_Unlock(&shared.lock);
                        enqueue(BTN_LOG_DELETE);
                        ui.selected = -1;
                        ui.log_open = false;   /* the drive is gone: back to the list */
                    }
                    ui.delete_dialog = false;
                    bottom_full = top_dirty = true;
                } else if (ui.dialog_held_in) {
                    if (ui.dialog_held == 2 && worker != NULL) {
                        enqueue(BTN_CLEAR_CODES);
                    }
                    ui.clear_dialog = false;
                    bottom_full = top_dirty = true;
                }
                ui.dialog_held = 0;
                ui.dialog_held_in = false;
                bottom_dirty = true;
            }
        }

        /* A DIAG page's list (graphics): drag to scroll, lift on a code to show it. */
        if (ui.live_touching && ui.screen == SCREEN_DIAG_PAGE) {
            int n = diag_rows_n;
            if (confirming || ui.clear_dialog) {
                ui.live_touching = ui.live_grip = false;
            } else if ((hidKeysHeld() & KEY_TOUCH) && ui.live_grip) {
                touchPosition tp;
                hidTouchRead(&tp);
                int sc = diag_list_scroll_for_grip(tp.py - ui.live_grip_grab, n);
                if (sc != ui.diag_scroll) {
                    ui.diag_scroll = sc;
                    bottom_dirty = true;
                }
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                int dy = tp.py - ui.live_touch_y0;
                if (!ui.live_dragged && (dy > 6 || dy < -6)) {
                    ui.live_dragged = true;
                    bottom_dirty = true;
                }
                if (ui.live_dragged) {
                    int sc = diag_list_clamp(ui.live_scroll0 - dy, n);
                    if (sc != ui.diag_scroll) {
                        ui.diag_scroll = sc;
                        bottom_dirty = true;
                    }
                }
            } else {
                ui.live_touching = ui.live_grip = false;
                if (!ui.live_dragged && ui.live_row_held >= 0 && ui.live_row_held < n) {
                    ui.selected = ui.live_row_held;
                    top_dirty = true;
                }
                ui.live_row_held = -1;
                bottom_dirty = true;
            }
        }

        /* LIVE's view list (graphics): drag to scroll, lift to pick. */
        if (ui.live_touching && ui.screen != SCREEN_DIAG_PAGE && ui.screen != SCREEN_ACHIEVE &&
            ui.screen != SCREEN_LOG && ui.screen != SCREEN_MAKE) {
            if (ui.screen != SCREEN_LIVE_MENU || confirming) {
                ui.live_touching = ui.live_grip = false;
            } else if ((hidKeysHeld() & KEY_TOUCH) && ui.live_grip) {
                touchPosition tp;
                hidTouchRead(&tp);
                int sc = live_list_scroll_for_grip(tp.py - ui.live_grip_grab, LIVE_VIEW_COUNT);
                if (sc != ui.live_scroll) {
                    ui.live_scroll = sc;
                    bottom_dirty = true;
                }
            } else if (hidKeysHeld() & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                int dy = tp.py - ui.live_touch_y0;
                if (!ui.live_dragged && (dy > 6 || dy < -6)) {
                    ui.live_dragged = true;
                    bottom_dirty = true;
                }
                if (ui.live_dragged) {
                    int sc = live_list_clamp(ui.live_scroll0 - dy, LIVE_VIEW_COUNT);
                    if (sc != ui.live_scroll) {
                        ui.live_scroll = sc;
                        bottom_dirty = true;
                    }
                }
            } else {
                ui.live_touching = ui.live_grip = false;
                if (!ui.live_dragged && ui.live_row_held >= 0 && !tapped) {
                    action = BTN_LIVE_VIEW - ui.live_row_held;
                    tapped = lifted = true;
                }
                ui.live_row_held = -1;
                bottom_dirty = true;
            }
        }

        /* Optional physical shortcuts: B back, X scan, D-pad / L R. */
        if (ui.ta_dialog && (down & KEY_B)) {
            ui.ta_dialog = false;
            bottom_full = top_dirty = true;
        } else if (ui.clear_dialog && (down & KEY_B)) {
            ui.clear_dialog = false;   /* B cancels; there is no button shortcut to clear */
            bottom_full = top_dirty = true;
        } else if (ui.track_info && track_kind_of(&ui) >= 0 && !ui.ta_dialog && (down & KEY_B)) {
            ui.track_info = false;   /* B closes Info */
            bottom_full = true;
        } else if (!ui.track_info && track_kind_of(&ui) >= 0 && !ui.ta_dialog && (down & KEY_Y) &&
                   !confirming) {
            ui.track_info = true;
            bottom_full = true;
        } else if (ui.trim_info && ui.screen == SCREEN_TRIM && (down & KEY_B)) {
            ui.trim_info = false;   /* B closes Info, back to TRIM's controls */
            bottom_full = true;
        } else if (home_gfx && ui.screen == SCREEN_TRIM && !ui.trim_info && (down & KEY_Y) &&
                   !confirming) {
            ui.trim_info = true;
            bottom_full = true;
        } else if (ui.log_info && ui.screen == SCREEN_LOG && (down & KEY_B)) {
            ui.log_info = false;   /* B closes Info */
            bottom_full = true;
        } else if (home_gfx && ui.screen == SCREEN_LOG && !ui.log_open && !ui.log_info &&
                   !ui.delete_dialog && (down & KEY_Y) && !confirming) {
            ui.log_info = true;
            bottom_full = true;
        } else if (ui.delete_dialog && (down & KEY_B)) {
            top_dirty = true;
            ui.delete_dialog = false;
            bottom_full = true;
        } else if (!confirming && !tapped && !ui.clear_dialog && !ui.delete_dialog &&
                   !ui.ta_dialog) {
            if ((down & KEY_B) && ui.screen != SCREEN_HOME) { action = BTN_BACK; tapped = true; }
            else if ((down & KEY_X) && ui.screen == SCREEN_DIAG) { action = BTN_DIAG_SCAN; tapped = true; }
            else if ((down & KEY_X) && ui.screen == SCREEN_DIAG_PAGE && ui.tab != DIAG_TAB_READINESS) { action = BTN_CLEAR_ASK; tapped = true; }
            else if ((down & KEY_L) && (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_MAKE || ui.screen == SCREEN_LIVE || ui.screen == SCREEN_LIVE_EDIT || ui.screen == SCREEN_LOG || ui.screen == SCREEN_GRAPH)) { action = BTN_PAGE_PREV; tapped = true; }
            else if ((down & KEY_R) && (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_GUIDE || ui.screen == SCREEN_MAKE || ui.screen == SCREEN_LIVE || ui.screen == SCREEN_LIVE_EDIT || ui.screen == SCREEN_LOG || ui.screen == SCREEN_GRAPH)) { action = BTN_PAGE_NEXT; tapped = true; }
        }

        if (tapped) {
            if (!lifted) {
                flash(action, now);
            }
            bottom_dirty = true;
            int n = ui.screen == SCREEN_DIAG ? diag_item_count(ui.tab) : 0;
            if (ui.screen == SCREEN_LOG) {
                LightLock_Lock(&shared.lock);
                n = shared.drive_count;
                LightLock_Unlock(&shared.lock);
            }
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
                ui.live_scroll = 0;
                if (home_gfx) {
                    live_dash_start();   /* the Dashboard on top */
                }
                bottom_full = top_dirty = true;
                break;
            }
            case BTN_LIVE_STOP:
            case BTN_LIVE_RECORD:
                LightLock_Lock(&shared.lock);
                shared.rec_want = action == BTN_LIVE_RECORD;
                if (shared.rec_want && !shared.live_on) {   /* recording polls too */
                    shared.live_on = true;
                    shared.live_gen++;
                    shared.live_msg[0] = '\0';
                }
                shared.live_ui_version++;
                LightLock_Unlock(&shared.lock);
                LightEvent_Signal(&shared.wake);
                top_dirty = true;
                break;
            case BTN_LIVE_EDIT:
                ui.screen = SCREEN_LIVE_EDIT;
                ui.page = 0;
                bottom_full = top_dirty = true;
                break;
            case BTN_LIVE_REC:
                LightLock_Lock(&shared.lock);
                shared.rec_want = !shared.rec_want;
                if (shared.rec_want && !shared.live_on) {   /* REC polls too */
                    shared.live_on = true;
                    shared.live_gen++;
                    shared.live_msg[0] = '\0';
                }
                shared.live_ui_version++;
                LightLock_Unlock(&shared.lock);
                LightEvent_Signal(&shared.wake);
                top_dirty = true;
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
                ui.trim_info = false;
                trim_start();
                bottom_full = top_dirty = true;
                break;
            case BTN_MENU_LOG:
                ui.screen = SCREEN_LOG;
                ui.page = 0;
                ui.selected = -1;
                ui.log_open = ui.log_info = false;
                ui.log_scroll = 0;
                if (worker != NULL) {
                    enqueue(BTN_LOG_SCAN);
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_LOG_GRAPH: {
                char file[32] = "";
                double len = -1.0;
                LightLock_Lock(&shared.lock);
                if (ui.selected >= 0 && ui.selected < shared.drive_count) {
                    memcpy(file, shared.drives[ui.selected].name, sizeof file);
                    file[sizeof file - 1] = '\0';
                    len = shared.drives[ui.selected].duration_s;
                }
                LightLock_Unlock(&shared.lock);
                if (file[0] == '\0') {
                    ui.notice = "GRAPH: tap a drive first";
                    notice_until = now + 3000;
                    break;
                }
                ui.graph_len = len > 1.0 ? len : 1.0;
                ui.graph_t0 = 0.0;
                ui.graph_t1 = ui.graph_len;
                ui.graph_col = 0;
                ui.screen = SCREEN_GRAPH;
                if (worker != NULL) {
                    graph_request(&ui, file);
                }
                bottom_full = top_dirty = true;
                break;
            }
            case BTN_GRAPH_EARLY:
            case BTN_GRAPH_LATE:
            case BTN_GRAPH_IN:
            case BTN_GRAPH_OUT:
            case BTN_GRAPH_ALL: {
                double w = ui.graph_t1 - ui.graph_t0;
                double mid = (ui.graph_t0 + ui.graph_t1) / 2;
                if (action == BTN_GRAPH_IN) {
                    w = w / 2 < 5.0 ? 5.0 : w / 2;
                } else if (action == BTN_GRAPH_OUT) {
                    w *= 2;
                } else if (action == BTN_GRAPH_ALL) {
                    w = ui.graph_len;
                    mid = w / 2;
                } else {
                    mid += action == BTN_GRAPH_LATE ? w / 2 : -w / 2;
                }
                if (w > ui.graph_len) {
                    w = ui.graph_len;
                }
                double t0 = mid - w / 2;
                t0 = t0 < 0.0 ? 0.0 : t0 + w > ui.graph_len ? ui.graph_len - w : t0;
                if (t0 != ui.graph_t0 || t0 + w != ui.graph_t1) {
                    ui.graph_t0 = t0;
                    ui.graph_t1 = t0 + w;
                    LightLock_Lock(&shared.lock);
                    char file[32];
                    memcpy(file, shared.graph_req.file, sizeof file);
                    LightLock_Unlock(&shared.lock);
                    if (worker != NULL) {
                        graph_request(&ui, file);
                    }
                    top_dirty = true;
                }
                break;
            }
            case BTN_LOG_ASK:
                if (ui.selected >= 0) {
                    ui.delete_dialog = true;
                    bottom_full = top_dirty = true;
                } else {
                    ui.notice = "DELETE: tap a drive first";
                    notice_until = now + 3000;
                }
                break;
            case BTN_MENU_TRACK:
                ui.screen = SCREEN_TRACK;
                if (worker != NULL) {
                    enqueue(BTN_ACH_LOAD);   /* the menu shows how many are unlocked */
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_TRACK_TA:
                if (!ui.ta_warned) {
                    ui.ta_dialog = true;   /* the closed-course notice first */
                } else if (home_gfx) {
                    ui.screen = SCREEN_TIME_ATTACK;   /* the run list; polls speed, not timing */
                    ui.ta_open = false;
                    ta_start_ex(ui.ta_run, cfg.metric, true, false);
                } else {
                    ui.screen = SCREEN_TIME_ATTACK;
                    ta_start(ui.ta_run, cfg.metric, true);
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_TA_WARN_OK:
            case BTN_TA_WARN_NO:
                ui.ta_dialog = false;
                if (action == BTN_TA_WARN_OK) {
                    ui.ta_warned = true;
                    ui.screen = SCREEN_TIME_ATTACK;
                    ui.ta_open = false;
                    ta_start_ex(ui.ta_run, cfg.metric, true, false);
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_TA_ARM:
            case BTN_TA_DISARM: {
                LightLock_Lock(&shared.lock);
                bool moving = shared.ta.have_last && shared.ta.last_kmh >= 1.0f &&
                              now - shared.ta.last_ms < TA_GAP_MS;
                LightLock_Unlock(&shared.lock);
                if (action == BTN_TA_ARM && moving) {
                    ui.notice = "Start a run while parked.";
                    notice_until = now + 3000;
                    break;
                }
                ta_start_ex(ui.ta_run, cfg.metric, false, action == BTN_TA_ARM);
                top_dirty = true;
                break;
            }
            case BTN_TRACK_SHIFT:
                ui.screen = SCREEN_SHIFT;
                shift_start();
                bottom_full = top_dirty = true;
                break;
            case BTN_TRACK_ACH:
                ui.screen = SCREEN_ACHIEVE;
                if (worker != NULL) {
                    enqueue(BTN_ACH_LOAD);
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_HOME_GARAGE:
                ui.garage_back = SCREEN_HOME;
                ui.screen = SCREEN_GARAGE;
                ui.page = 0;   /* the connected car's card comes first */
                if (worker != NULL) {
                    enqueue(BTN_GARAGE_SCAN);
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_GARAGE_PREV:
            case BTN_GARAGE_NEXT: {
                LightLock_Lock(&shared.lock);
                int cars = shared.garage.count;
                LightLock_Unlock(&shared.lock);
                if (cars > 0) {
                    ui.page = (ui.page + (action == BTN_GARAGE_NEXT ? 1 : cars - 1)) % cars;
                }
                top_dirty = true;
                break;
            }
            case BTN_GARAGE_TRIP: {
                garage_car_t c;
                double km;
                bool live;
                card_odometer(ui.page, &c, &km, &live);
                LightLock_Lock(&shared.lock);
                bool connected_car = c.key[0] != '\0' && strcmp(c.key, shared.garage_current) == 0;
                LightLock_Unlock(&shared.lock);
                if (connected_car) {   /* read the odometer live */
                    static const uint8_t pids[] = { 0xA6, 0x0D };
                    static const uint32_t periods[] = { 2000, 1000 };
                    LightLock_Lock(&shared.lock);
                    shared.ta_on = shared.shift_on = shared.gmeter_on = false;
                    shared.revsync_on = shared.eco_on = false;
                    shared.trip_on = true;
                    live_start_locked(pids, periods, 2);
                    LightLock_Unlock(&shared.lock);
                    LightEvent_Signal(&shared.wake);
                }
                ui.screen = SCREEN_TRIP;
                bottom_full = top_dirty = true;
                break;
            }
            case BTN_GARAGE_MILES:
                ui.screen = SCREEN_MILEAGE;
                ui.miles_del_armed = 0;
                ui.miles_page = 0;
                ui.miles_sel = -1;
                bottom_full = top_dirty = true;
                break;
            case BTN_TRIP_RESET: {
                if (car_moving(now)) {
                    ui.notice = "Reset the trip while parked.";
                    notice_until = now + 3000;
                    break;
                }
                garage_car_t c;
                double km;
                bool live;
                if (!card_odometer(ui.page, &c, &km, &live)) {
                    ui.notice = "No odometer reading yet.";
                    notice_until = now + 3000;
                    break;
                }
                LightLock_Lock(&shared.lock);
                snprintf(shared.trip_key, sizeof shared.trip_key, "%s", c.key);
                shared.trip_km = (float)km;
                LightLock_Unlock(&shared.lock);
                if (worker != NULL) {
                    enqueue(BTN_TRIP_SAVE);
                }
                ui.notice = "New trip started.";
                notice_until = now + 3000;
                break;
            }
            case BTN_MILES_NEW:
                if (car_moving(now)) {
                    ui.notice = "Add entries while parked.";
                    notice_until = now + 3000;
                    break;
                }
                ui.notice = miles_new_entry(ui.page, cfg.metric, -1);
                notice_until = now + 4000;
                ui.miles_sel = -1;
                ui.miles_page = 0;
                bottom_full = top_dirty = true;   /* the keyboard covered both screens */
                break;
            case BTN_MILES_EDIT:
                if (ui.miles_sel < 0) {
                    ui.notice = "Tap an entry first.";
                    notice_until = now + 3000;
                    break;
                }
                ui.notice = miles_new_entry(ui.page, cfg.metric, ui.miles_sel);
                notice_until = now + 4000;
                bottom_full = top_dirty = true;
                break;
            case BTN_MILES_DEL: {
                if (ui.miles_sel < 0) {
                    ui.notice = "Tap an entry first.";
                    notice_until = now + 3000;
                    break;
                }
                if (now >= ui.miles_del_armed) {
                    ui.miles_del_armed = now + 4000;   /* tap again within 4 s */
                    ui.notice = "Tap AGAIN to delete the selected entry.";
                    notice_until = now + 4000;
                    break;
                }
                ui.miles_del_armed = 0;
                garage_car_t c;
                double km;
                bool live;
                card_odometer(ui.page, &c, &km, &live);
                static char mtext[sizeof shared.miles_text];
                static mileage_list_t ml;
                LightLock_Lock(&shared.lock);
                memcpy(mtext, shared.miles_text, sizeof mtext);
                LightLock_Unlock(&shared.lock);
                mileage_read(mtext, c.key, &ml);
                if (ui.miles_sel >= ml.n) {
                    break;
                }
                LightLock_Lock(&shared.lock);
                snprintf(shared.miles_key, sizeof shared.miles_key, "%s", c.key);
                shared.miles_index = ml.first + (ml.n - 1 - ui.miles_sel);
                LightLock_Unlock(&shared.lock);
                if (worker != NULL) {
                    enqueue(BTN_MILES_DROP);
                }
                ui.miles_sel = -1;
                ui.notice = "Entry deleted.";
                notice_until = now + 3000;
                top_dirty = true;
                break;
            }
            case BTN_MILES_PREV:
            case BTN_MILES_NEXT:
            case BTN_MILES_ROW:
            case BTN_MILES_ROW - 1:
            case BTN_MILES_ROW - 2: {
                garage_car_t c;
                double km;
                bool live;
                card_odometer(ui.page, &c, &km, &live);
                static char rtext[sizeof shared.miles_text];
                static mileage_list_t rl;
                LightLock_Lock(&shared.lock);
                memcpy(rtext, shared.miles_text, sizeof rtext);
                LightLock_Unlock(&shared.lock);
                int count = mileage_read(rtext, c.key, &rl);
                if (action == BTN_MILES_PREV || action == BTN_MILES_NEXT) {
                    int np = ui.miles_page + (action == BTN_MILES_NEXT ? 1 : -1);
                    if (np >= 0 && np < page_count(count)) {
                        ui.miles_page = np;
                        ui.miles_sel = -1;
                    }
                } else {
                    int k = ui.miles_page * LIST_PAGE_ROWS + (BTN_MILES_ROW - action);
                    if (k < count) {
                        ui.miles_sel = k;
                        ui.miles_del_armed = 0;
                    }
                }
                top_dirty = true;
                break;
            }
            case BTN_GARAGE_NICK:
                if (edit_nickname(ui.page) && worker != NULL) {
                    enqueue(BTN_GARAGE_SAVE);
                }
                bottom_full = top_dirty = true;   /* the keyboard covered both screens */
                break;
            case BTN_TRACK_ECO:
                ui.screen = SCREEN_ECO;
                eco_start();
                if (worker != NULL) {
                    enqueue(BTN_RS_BEST);   /* this car's best trip score */
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_ECO_RESET:
                if (car_moving(now)) {
                    ui.notice = "Reset the trip while parked.";
                    notice_until = now + 3000;
                    break;
                }
                eco_reset(&eco);
                top_dirty = true;
                break;
            case BTN_TRACK_REVSYNC:
                ui.screen = SCREEN_REVSYNC;
                revsync_start(cfg.shift_rpm, now);
                if (worker != NULL) {
                    enqueue(BTN_RS_BEST);   /* this car's high score */
                }
                bottom_full = top_dirty = true;
                break;
            case BTN_RS_START:
                if (worker != NULL) {
                    enqueue(BTN_RS_BEST);   /* the car may be known by now */
                }
                if (!rs_start(&revsync, now)) {
                    ui.notice = revsync.state == RS_LOCKED ? "Locked: the speed must read 0."
                                                           : "A game is running.";
                    notice_until = now + 3000;
                    break;
                }
                LightLock_Lock(&shared.lock);
                shared.rs_record = false;
                LightLock_Unlock(&shared.lock);
                bottom_full = top_dirty = true;
                break;
            case BTN_TRACK_GMETER:
                ui.screen = SCREEN_GMETER;
                gmeter_start();
                bottom_full = top_dirty = true;
                break;
            case BTN_GM_CAL:
            case BTN_GM_RESET:
                if (car_moving(now)) {
                    ui.notice = action == BTN_GM_CAL ? "Calibrate while parked."
                                                     : "Reset the peaks while parked.";
                    notice_until = now + 3000;
                    break;
                }
                if (action == BTN_GM_CAL) {
                    gm_cal_start(&gmeter);
                } else {
                    gm_reset_peaks(&gmeter);
                }
                top_dirty = true;
                break;
            case BTN_SHIFT_DOWN:
            case BTN_SHIFT_UP:
                if (car_moving(now)) {
                    ui.notice = "Set the shift point while parked.";
                    notice_until = now + 3000;
                    break;
                }
                LightLock_Lock(&shared.lock);
                cfg.shift_rpm = (uint16_t)shift_step(cfg.shift_rpm,
                                                     action == BTN_SHIFT_UP ? 100 : -100);
                LightLock_Unlock(&shared.lock);
                if (worker != NULL) {
                    enqueue(BTN_SHIFT_SAVE);
                }
                top_dirty = true;
                break;
            case BTN_TA_RUN - TA_RUN_LAUNCH:
            case BTN_TA_RUN - TA_RUN_BRAKE:
            case BTN_TA_RUN - TA_RUN_QUARTER: {
                LightLock_Lock(&shared.lock);
                bool moving = shared.ta.have_last && shared.ta.last_kmh >= 1.0f &&
                              now - shared.ta.last_ms < TA_GAP_MS;
                LightLock_Unlock(&shared.lock);
                if (moving) {
                    ui.notice = "Pick a run while parked.";
                    notice_until = now + 3000;
                    break;
                }
                ui.ta_run = BTN_TA_RUN - action;
                ta_start(ui.ta_run, cfg.metric, false);
                top_dirty = true;
                break;
            }
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
            case BTN_PAGE_NEXT:
                if (ui.screen == SCREEN_GRAPH) {   /* another value of the drive */
                    LightLock_Lock(&shared.lock);
                    int ncols = shared.graph_ncols;
                    char file[32];
                    memcpy(file, shared.graph_req.file, sizeof file);
                    LightLock_Unlock(&shared.lock);
                    if (ncols > 1) {
                        ui.graph_col = (ui.graph_col + (action == BTN_PAGE_NEXT ? 1 : ncols - 1)) %
                                       ncols;
                        if (worker != NULL) {
                            graph_request(&ui, file);
                        }
                        top_dirty = true;
                    }
                    break;
                }
                if (action == BTN_PAGE_NEXT) {
                    goto page_next;
                }
                if (ui.screen == SCREEN_LIVE) {   /* the previous view */
                    live_select_view(&ui, (ui.live_view + LIVE_VIEW_COUNT - 1) % LIVE_VIEW_COUNT);
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.page > 0) { ui.page--; ui.selected = -1; top_dirty = true; }
                bottom_full |= ui.screen == SCREEN_MAKE;   /* the last page is shorter */
                break;
            page_next: {
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
            case BTN_ENGINE_PREV:
            case BTN_ENGINE_NEXT:
                LightLock_Lock(&shared.lock);
                cfg.engine_dl = (uint16_t)config_engine_step(cfg.engine_dl,
                                                             action == BTN_ENGINE_NEXT ? 1 : -1);
                LightLock_Unlock(&shared.lock);
                if (worker != NULL) {
                    enqueue(BTN_SET_VEHICLE);
                }
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
                ui.make_scroll = diag_list_clamp(diag_list_row(make_choice_of(cfg.car_make), 0).y - 80,
                                                 make_choice_count());
                LightLock_Unlock(&shared.lock);
                ui.screen = SCREEN_MAKE;
                bottom_full = top_dirty = true;
                break;
            case BTN_BACK:
                if (ui.screen == SCREEN_LOG && home_gfx && ui.log_open) {
                    ui.log_open = false;   /* the drive's page -> the list */
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_DIAG_PAGE) {
                    ui.screen = SCREEN_DIAG;
                    ui.selected = -1;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_ACHIEVE) {
                    ui.screen = SCREEN_TRACK;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_TRIP || ui.screen == SCREEN_MILEAGE) {
                    if (ui.screen == SCREEN_TRIP) {
                        LightLock_Lock(&shared.lock);
                        shared.trip_on = false;
                        shared.live_on = false;
                        shared.live_ui_version++;
                        LightLock_Unlock(&shared.lock);
                        if (worker != NULL) {
                            enqueue(BTN_GARAGE_SCAN);   /* the card's odometer may be newer */
                        }
                    }
                    ui.screen = SCREEN_GARAGE;   /* keep the card */
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_GARAGE) {
                    ui.page = 0;
                    ui.screen = ui.garage_back;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_ECO) {
                    eco_stop();
                    if (eco_ranked(&eco) && worker != NULL) {
                        LightLock_Lock(&shared.lock);
                        shared.hs_run = "eco";
                        shared.rs_save_score = eco_score(&eco);
                        LightLock_Unlock(&shared.lock);
                        enqueue(BTN_RS_BEST);
                    }
                    ui.screen = SCREEN_TRACK;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_REVSYNC) {
                    revsync_stop();
                    ui.screen = SCREEN_TRACK;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_GMETER) {
                    gmeter_stop();
                    ui.screen = SCREEN_TRACK;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_SHIFT) {
                    shift_stop();
                    ui.screen = SCREEN_TRACK;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_TIME_ATTACK && home_gfx && ui.ta_open) {
                    ui.ta_open = false;   /* back to the run list; stops timing */
                    ta_start_ex(ui.ta_run, cfg.metric, false, false);
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_TIME_ATTACK) {
                    ta_stop();
                    ui.screen = SCREEN_TRACK;
                    bottom_full = top_dirty = true;
                    break;
                }
                if (ui.screen == SCREEN_TRIM) {
                    LightLock_Lock(&shared.lock);
                    shared.live_on = false;
                    shared.live_ui_version++;
                    LightLock_Unlock(&shared.lock);
                }
                if (ui.screen == SCREEN_LIVE_MENU) {
                    LightLock_Lock(&shared.lock);
                    shared.live_on = false;   /* the Dashboard's polling */
                    shared.live_ui_version++;
                    LightLock_Unlock(&shared.lock);
                }
                if (ui.screen == SCREEN_LIVE_EDIT || ui.screen == SCREEN_LIVE) {
                    if (ui.screen == SCREEN_LIVE && home_gfx) {
                        live_dash_start();   /* back to the Dashboard */
                    } else if (ui.screen == SCREEN_LIVE) {
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
                    ui.screen == SCREEN_MAKE || ui.screen == SCREEN_LOG) {
                    ui.page = 0;
                    ui.selected = ui.screen == SCREEN_LOG ? -1 : ui.selected;
                }
                if (ui.screen == SCREEN_GRAPH) {
                    ui.screen = SCREEN_LOG;   /* keep the page and the selection */
                    bottom_full = top_dirty = true;
                    break;
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
                    bottom_full = top_dirty = true;
                } else {
                    ui.notice = home_gfx ? "Tap Scan on DIAG first." : "CLEAR: tap SCAN first";
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
                } else if (action <= BTN_TA_OPEN && action > BTN_TA_OPEN - TA_RUN_COUNT) {
                    ui.ta_run = BTN_TA_OPEN - action;
                    ui.ta_open = true;
                    ta_start_ex(ui.ta_run, cfg.metric, false, false);
                    bottom_full = top_dirty = true;
                } else if (action <= BTN_DIAG_PAGE && action > BTN_DIAG_PAGE - DIAG_TAB_COUNT) {
                    ui.tab = (diag_tab_t)(BTN_DIAG_PAGE - action);
                    ui.screen = SCREEN_DIAG_PAGE;
                    ui.selected = -1;
                    ui.diag_scroll = 0;
                    diag_rows_refresh(ui.tab);
                    bottom_full = top_dirty = true;
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
        static bool was_connected;
        if (connected && !was_connected && worker != NULL) {
            enqueue(BTN_GARAGE_SCAN);   /* HOME's car card: the car may be new */
        }
        was_connected = connected;
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
        {
            /* A new achievement: a pop-up notice on any screen, and the
             * list redrawn if it shows. */
            static unsigned ach_seen;
            static char ach_msg[64];
            LightLock_Lock(&shared.lock);
            unsigned av = shared.ach_version;
            int id = shared.ach_new;
            LightLock_Unlock(&shared.lock);
            if (av != ach_seen) {
                /* Several at once (a clean first scan): say how many. */
                static int shown;
                bool still = ui.notice == ach_msg && now < notice_until;
                shown = still ? shown + (int)(av - ach_seen) : (int)(av - ach_seen);
                ach_seen = av;
                const achieve_info_t *a = achieve_info((achieve_id_t)id);
                if (shown > 1) {
                    snprintf(ach_msg, sizeof ach_msg, "Unlocked %d achievements! (TRACK)", shown);
                } else {
                    snprintf(ach_msg, sizeof ach_msg, "Unlocked: %s!", a != NULL ? a->name : "?");
                }
                ui.notice = ach_msg;
                notice_until = now + 5000;
                bottom_dirty = true;
                if (ui.screen == SCREEN_ACHIEVE) {
                    top_dirty = true;
                }
            }
            static bool ach_loaded;   /* the list arrives after BTN_ACH_LOAD */
            if ((ui.screen == SCREEN_ACHIEVE || ui.screen == SCREEN_TRACK) && !ach_loaded) {
                LightLock_Lock(&shared.lock);
                ach_loaded = shared.ach_text[0] != '\0';
                LightLock_Unlock(&shared.lock);
                top_dirty |= ach_loaded;
                bottom_dirty |= ach_loaded;
            }
        }
        if (ui.screen == SCREEN_GARAGE || ui.screen == SCREEN_TRIP ||
            ui.screen == SCREEN_MILEAGE || ui.screen == SCREEN_HOME) {
            static unsigned garage_drawn = ~0u;
            LightLock_Lock(&shared.lock);
            unsigned gv = shared.garage_version;
            LightLock_Unlock(&shared.lock);
            if (gv != garage_drawn) {
                garage_drawn = gv;
                top_dirty = true;
            }
        }
        /* TIME ATTACK: the countdown and the running time move on their own. */
        bool ta_due = (ui.screen == SCREEN_TIME_ATTACK && now - live_drawn_at >= 100) ||
                      ((ui.screen == SCREEN_SHIFT || ui.screen == SCREEN_GMETER ||
                        ui.screen == SCREEN_REVSYNC || ui.screen == SCREEN_ECO) &&
                       now - live_drawn_at >= 30) ||
                      (ui.screen == SCREEN_TRIP && now - live_drawn_at >= 500) ||
                      (ui.screen == SCREEN_HOME && home_gfx && now - live_drawn_at >= 1000);

        LightLock_Lock(&shared.lock);
        unsigned drive_version = shared.drive_version;
        int drive_count = shared.drive_count;
        LightLock_Unlock(&shared.lock);
        LightLock_Lock(&shared.lock);
        unsigned graph_version = shared.graph_version;
        LightLock_Unlock(&shared.lock);
        if (graph_version != drawn_graph_version) {
            drawn_graph_version = graph_version;
            if (ui.screen == SCREEN_GRAPH) {
                top_dirty = bottom_dirty = true;
            }
        }
        if (drive_version != drawn_drive_version) {
            drawn_drive_version = drive_version;
            if (ui.screen == SCREEN_LOG) {
                if (ui.page >= page_count(drive_count)) {   /* the list got shorter */
                    ui.page = page_count(drive_count) - 1;
                }
                if (ui.selected >= drive_count) {
                    ui.selected = -1;
                    ui.log_open = false;
                }
                top_dirty = bottom_full = true;
            }
        }
        bool shows_log = ui.screen == SCREEN_SETUP || ui.screen == SCREEN_TEST ||
                         ui.screen == SCREEN_MAKE || ui.screen == SCREEN_SETUP_WIFI ||
                         ui.screen == SCREEN_SETUP_VEHICLE || ui.screen == SCREEN_SETUP_OTHER;
        if (top_dirty || busy != drawn_busy || connected != drawn_connected || live_due || ta_due ||
            (shows_log ? (version != drawn_version || scroll != drawn_scroll)
                       : !live_screen && diag_version != drawn_diag_version)) {
            /* HOME's and GRAPH's top graphics reach the last column, which
             * the text screens don't print: clear it all once on leaving. */
            static bool gfx_top_shown;
            bool live_gfx = home_gfx && (ui.screen == SCREEN_LIVE_MENU || ui.screen == SCREEN_LIVE);
            bool diag_gfx = home_gfx && (ui.screen == SCREEN_DIAG || ui.screen == SCREEN_DIAG_PAGE);
            bool trim_gfx = home_gfx && ui.screen == SCREEN_TRIM;
            bool gfx_top_now = (ui.screen == SCREEN_HOME && home_gfx) || live_gfx || diag_gfx || trim_gfx ||
                               track_kind_of(&ui) >= 0 || (home_gfx && ui.screen == SCREEN_LOG) ||
                               setup_kind_of(&ui) >= 0;
            if (gfx_top_shown && !gfx_top_now && ui.screen != SCREEN_GRAPH) {
                consoleSelect(&top_console);
                printf("\x1b[0m\x1b[2J");
            }
            gfx_top_shown = gfx_top_now || (graph_gfx && ui.screen == SCREEN_GRAPH);
            bool track_gfx = track_kind_of(&ui) >= 0;
            if (setup_kind_of(&ui) >= 0) {
                draw_setup_gfx_top(&ui, &cfg, connected, scroll);
                bottom_dirty = true;   /* values on the rows follow the settings */
            } else if (track_gfx) {
                draw_track_gfx_top(&ui, connected, &cfg, now);
                live_drawn_at = now;
                static uint64_t track_bottom_at;
                if (ui.screen == SCREEN_ACHIEVE || ui.screen == SCREEN_TRACK) {
                    bottom_dirty = true;   /* drawn only when something changed */
                } else if (now - track_bottom_at >= 250) {   /* Rev Sync's lock, a run's state */
                    track_bottom_at = now;
                    bottom_dirty = true;
                }
            } else if (ui.screen == SCREEN_TIME_ATTACK) {
                draw_ta_top(busy, connected, cfg.metric, now);
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_TRIP) {
                draw_trip_top(busy, connected, ui.page, cfg.metric, top_dirty);
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_MILEAGE) {
                draw_miles_top(busy, connected, ui.page, cfg.metric, top_dirty, ui.miles_sel);
            } else if (ui.screen == SCREEN_ACHIEVE) {
                draw_achieve_top(busy, connected, top_dirty);
            } else if (ui.screen == SCREEN_GARAGE) {
                draw_garage_top(busy, connected, ui.page, cfg.metric, top_dirty);
            } else if (ui.screen == SCREEN_ECO) {
                draw_eco_top(busy, connected, &cfg, now, top_dirty);
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_REVSYNC) {
                draw_revsync_top(busy, connected, top_dirty);
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_GMETER) {
                draw_gmeter_top(busy, connected, gm_raw, top_dirty);
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_SHIFT) {
                draw_shift_top(busy, connected, cfg.shift_rpm, now);
                live_drawn_at = now;
            } else if (ui.screen == SCREEN_TRACK) {
                draw_track_top(busy, connected);
            } else if (trim_gfx) {
                draw_trim_gfx_top(connected);
                drawn_live_version = live_version;
                live_drawn_at = now;
                bottom_dirty = true;   /* the hints follow the values */
            } else if (ui.screen == SCREEN_TRIM) {
                draw_trim_top(busy, connected);
                drawn_live_version = live_version;
                live_drawn_at = now;
            } else if (diag_gfx) {
                draw_diag_gfx_top(&ui, connected);
            } else if (live_gfx && ui.screen == SCREEN_LIVE_MENU) {
                draw_live_dash_top(connected);
                drawn_live_version = live_version;
                live_drawn_at = now;
            } else if (live_gfx) {
                draw_live_view_top(ui.live_view, connected);
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
            } else if (ui.screen == SCREEN_LOG && home_gfx) {
                draw_log_gfx_top(&ui, connected);
            } else if (ui.screen == SCREEN_LOG) {
                draw_log_top(ui.selected, busy, connected);
            } else if (ui.screen == SCREEN_GRAPH) {
                draw_graph_top(&ui, connected);
            } else if (shows_log) {
                draw_top(scroll, busy, connected);
            } else if (ui.screen == SCREEN_HOME && home_gfx) {
                draw_home_top(connected);
                live_drawn_at = now;
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
        /* LIVE (graphics): the REC dot pulses while recording. */
        static uint64_t rec_drawn_at;
        if ((ui.screen == SCREEN_LIVE || (ui.screen == SCREEN_TRIM && !ui.trim_info)) && home_gfx &&
            now - rec_drawn_at >= 50) {
            LightLock_Lock(&shared.lock);
            bool rec = shared.rec_active;
            LightLock_Unlock(&shared.lock);
            if (rec) {
                bottom_dirty = true;
                rec_drawn_at = now;
            }
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
    graph_draw_exit();
    home_draw_exit();
    if (ptmu_ok) {
        ptmuExit();
    }
    spike_logf(&spike, "Session end");
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}
