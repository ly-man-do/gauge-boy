/*
 * M0 spike app, 3DS side: consoles, touch buttons and the worker thread.
 * The UI thread never does network or file I/O after startup; the worker
 * thread runs the button actions and writes the log file.
 */
#include "spike_ui.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "data/config.h"
#include "data/textlog.h"
#include "spike_actions.h"
#include "transport/net.h"
#include "transport/tcp_transport.h"
#include "version.h"

#define LOG_PATH      CONFIG_DIR "/spike-log.txt"
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

/* Bottom-screen buttons in 8x8 console cells: 19x6 cells = 152x48 px. */
typedef struct {
    const char *label;
    int action;          /* spike_action_t, or BTN_SCROLL_* */
    int col, row, w, h;
} button_t;

static const button_t buttons[] = {
    { NULL,       SPIKE_CONNECT,    0,  2, 19, 6 },
    { NULL,       SPIKE_ATZ,        20, 2, 19, 6 },
    { NULL,       SPIKE_SEARCH,     0,  9, 19, 6 },
    { NULL,       SPIKE_PROTOCOL,   20, 9, 19, 6 },
    { NULL,       SPIKE_VOLTAGE,    0, 16, 19, 6 },
    { NULL,       SPIKE_VIN,        20, 16, 19, 6 },
    { "OLDER ^",  BTN_SCROLL_OLDER, 0, 23, 19, 6 },
    { "NEWER v",  BTN_SCROLL_NEWER, 20, 23, 19, 6 },
};
#define BUTTON_COUNT ((int)(sizeof buttons / sizeof buttons[0]))

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
    volatile bool quit;
} shared;

static FILE *log_file;  /* main thread at startup / shutdown, worker thread otherwise */

static PrintConsole top_console;
static PrintConsole bottom_console;

static void log_sink(void *ctx, const char *line)
{
    (void)ctx;
    LightLock_Lock(&shared.lock);
    textlog_add(&shared.log, line);
    LightLock_Unlock(&shared.lock);

    if (log_file != NULL) {
        fputs(line, log_file);
        fputc('\n', log_file);
        fflush(log_file);
    }
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

static void worker_main(void *arg)
{
    spike_t *spike = arg;

    while (!shared.quit) {
        LightEvent_WaitTimeout(&shared.wake, 200LL * 1000 * 1000);
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

            spike_run(spike, (spike_action_t)action);

            LightLock_Lock(&shared.lock);
            shared.busy = -1;
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

static void draw_button(const button_t *b, bool flash, bool busy)
{
    const char *label = b->label ? b->label : spike_action_label((spike_action_t)b->action);
    const char *colors = busy ? "\x1b[41;37m" : flash ? "\x1b[43;30m" : "\x1b[44;37m";
    int label_len = (int)strlen(label);
    int pad = (b->w - label_len) / 2;

    printf("%s", colors);
    for (int r = 0; r < b->h; r++) {
        printf("\x1b[%d;%dH", b->row + r + 1, b->col + 1);
        if (r == b->h / 2) {
            printf("%*s%s%*s", pad, "", label, b->w - pad - label_len, "");
        } else {
            printf("%*s", b->w, "");
        }
    }
    printf("\x1b[0m");
}

static void draw_bottom(const gb_config_t *cfg, const uint64_t *flash_until,
                        uint64_t now, int busy)
{
    char line[BOTTOM_COLS + 1];

    consoleSelect(&bottom_console);
    printf("\x1b[0m");
    snprintf(line, sizeof line, "Adapter %s:%u", cfg->adapter_host,
             (unsigned)cfg->adapter_port);
    print_row(0, BOTTOM_COLS, line);
    for (int i = 0; i < BUTTON_COUNT; i++) {
        draw_button(&buttons[i], now < flash_until[i],
                    buttons[i].action >= 0 && buttons[i].action == busy);
    }
    print_row(29, BOTTOM_COLS, "START: exit  Up/Down L/R: scroll");
}

static int button_at(int px, int py)
{
    int col = px / 8;
    int row = py / 8;
    for (int i = 0; i < BUTTON_COUNT; i++) {
        const button_t *b = &buttons[i];
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
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        char when[32] = "unknown time";
        if (tm != NULL) {
            strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tm);
        }
        fprintf(log_file, "\n=== GaugeBoy %s spike session, %s ===\n", GB_VERSION, when);
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
    spike_logf(&spike, "GaugeBoy %s M0 spike", GB_VERSION);
    if (log_file == NULL) {
        spike_logf(&spike, "! Could not open %s; log is on screen only", LOG_PATH);
    }

    load_config(&spike, &cfg);
    tcp_transport_init(&transport, cfg.adapter_host, cfg.adapter_port);

    bool net_ok = net_init();
    if (!net_ok) {
        spike_logf(&spike, "! Network service did not start (is Wi-Fi on?)");
    }
    spike.net_up = net_bring_up;
    spike.net_info = net_describe;
    {
        char info[160];
        net_describe(info, sizeof info);
        spike_logf(&spike, "Network: %s", info);
    }
    spike_logf(&spike, "Adapter %s:%u. Join its Wi-Fi in System Settings,",
               cfg.adapter_host, (unsigned)cfg.adapter_port);
    spike_logf(&spike, "then tap CONNECT and ATZ.");

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    int worker_prio = prio + 1 > 0x3F ? 0x3F : (int)prio + 1;
    Thread worker = threadCreate(worker_main, &spike, WORKER_STACK, worker_prio, -2, false);
    if (worker == NULL) {
        spike_logf(&spike, "! Could not start the worker thread; buttons will not work");
    }

    uint64_t flash_until[BUTTON_COUNT] = { 0 };
    int scroll = 0;
    unsigned drawn_version = ~0u;
    int drawn_scroll = -1;
    int drawn_busy = -2;
    bool drawn_connected = false;
    bool bottom_dirty = true;

    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        uint64_t now = tp_now_ms();

        if (down & KEY_START) {
            break;
        }
        if (down & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            int i = button_at(tp.px, tp.py);
            if (i >= 0) {
                flash_until[i] = now + FLASH_MS;
                bottom_dirty = true;
                if (buttons[i].action == BTN_SCROLL_OLDER) {
                    scroll = clamp_scroll(scroll + LOG_ROWS / 2);
                } else if (buttons[i].action == BTN_SCROLL_NEWER) {
                    scroll = clamp_scroll(scroll - LOG_ROWS / 2);
                } else if (worker != NULL) {
                    enqueue(buttons[i].action);
                }
            }
        }
        /* Optional physical shortcuts. */
        if (down & KEY_DUP)   scroll = clamp_scroll(scroll + 1);
        if (down & KEY_DDOWN) scroll = clamp_scroll(scroll - 1);
        if (down & KEY_L)     scroll = clamp_scroll(scroll + LOG_ROWS);
        if (down & KEY_R)     scroll = clamp_scroll(scroll - LOG_ROWS);

        LightLock_Lock(&shared.lock);
        unsigned version = shared.log.version;
        int busy = shared.busy;
        bool connected = shared.connected;
        LightLock_Unlock(&shared.lock);

        if (version != drawn_version || scroll != drawn_scroll || busy != drawn_busy ||
            connected != drawn_connected) {
            draw_top(scroll, busy, connected);
            drawn_version = version;
            drawn_scroll = scroll;
            bottom_dirty |= busy != drawn_busy;
            drawn_busy = busy;
            drawn_connected = connected;
        }
        for (int i = 0; i < BUTTON_COUNT; i++) {
            if (flash_until[i] != 0 && now >= flash_until[i]) {
                flash_until[i] = 0;
                bottom_dirty = true;
            }
        }
        if (bottom_dirty) {
            draw_bottom(&cfg, flash_until, now, busy);
            bottom_dirty = false;
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
    if (net_ok) {
        net_exit();
    }
    spike_logf(&spike, "Session end");
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}
