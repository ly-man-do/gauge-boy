#ifndef GAUGEBOY_WIFI_GUIDE_H
#define GAUGEBOY_WIFI_GUIDE_H

/*
 * The in-app Wi-Fi setup guide (SETUP > Wi-Fi setup guide): the steps for
 * saving the adapter's network in System Settings with the manual IP and
 * DNS Gauge Boy needs (so it can answer the 3DS connection test), with the
 * values worked out from the adapter's address in config.ini and written
 * the way the 3DS shows them (three digits per part). Platform-free.
 */

#include "data/diag_view.h"   /* DIAG_VIEW_COLS */

#define WIFI_GUIDE_STEPS 5

/* Short title of step 0..WIFI_GUIDE_STEPS-1. */
const char *wifi_guide_title(int step);

/* The step's text for the top screen, wrapped to cols, values filled in
 * from adapter_host ("192.168.0.10"). Returns the number of lines. */
int wifi_guide_lines(int step, const char *adapter_host, int cols,
                     char lines[][DIAG_VIEW_COLS + 1], int max);

/* "192.168.0.11" -> "192.168.000.011" (the 3DS's input style). Returns
 * out; copies the text unchanged if it isn't an IPv4 address. */
char *wifi_guide_3digit(const char *ip, char *out, size_t len);

/* ---------- CHECK & CONNECT ---------- */

typedef enum {
    WIFI_STEP_WIFI = 0,    /* the 3DS on Wi-Fi (joining a saved slot if not) */
    WIFI_STEP_ADAPTER,     /* a TCP connection to the adapter */
    WIFI_STEP_ANSWER,      /* the adapter answering a command (ATI) */
    WIFI_STEP_CAR,         /* the car answering (not a failure: connected anyway) */
    WIFI_STEP_DONE,
} wifi_step_t;

typedef enum {
    WIFI_TCP_OK = 0, WIFI_TCP_REFUSED, WIFI_TCP_TIMEOUT, WIFI_TCP_OTHER,
} wifi_tcp_t;

typedef struct {
    wifi_step_t failed;    /* the step that failed, or WIFI_STEP_DONE */
    wifi_tcp_t tcp;        /* how the adapter step failed */
    bool car_ok;           /* the car answered */
    int slot;              /* the slot checked (1-3), 0 if none */
    bool slot_read;        /* its settings could be read */
    bool slot_matches;     /* its IP and DNS are as in the guide (steps 3-4) */
    char host[16];         /* the adapter's address */
    unsigned port;
    char adapter[24];      /* "ELM327 v2.3" */
    char car[40];          /* "2023 Subaru Ascent", or the protocol */
    char plan_ip[16];      /* what the guide says to use: the 3DS's IP (= DNS) */
    bool slot_any;         /* Wi-Fi slot is Any: the 3DS may be on another network */
    char network[33];      /* the network it's on, if the 3DS says ("" if not) */
} wifi_check_t;

#define WIFI_CHECK_COLS 39

/* What CHECK says: on success just that it's connected (and to what); on
 * failure the step and what to do, with the slot's settings named when
 * they differ from the guide. Returns the line count. */
int wifi_check_lines(const wifi_check_t *c, char lines[][WIFI_CHECK_COLS + 1], int max);

/* "Checking Wi-Fi..." for a step in progress. */
const char *wifi_step_progress(wifi_step_t step);

#endif
