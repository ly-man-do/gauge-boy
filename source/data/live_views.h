#ifndef GAUGEBOY_LIVE_VIEWS_H
#define GAUGEBOY_LIVE_VIEWS_H

/*
 * LIVE views (gauge-boy-3n6.12): a few PIDs each, picked for one job, plus
 * All (every PID the car supports) and Custom (the user's own pick). A
 * view can give a PID its own refresh time (Warm-up reads coolant every
 * second instead of every 15 s). Platform-free.
 */

#include <stdbool.h>
#include <stdint.h>

#include "obd/obd_live.h"

/* In the order the LIVE list shows them (owner, 2026-11-18). */
typedef enum {
    LIVE_VIEW_ENGINE = 0,
    LIVE_VIEW_WARMUP,
    LIVE_VIEW_DRIVE,
    LIVE_VIEW_AIR_FUEL,
    LIVE_VIEW_IDLE,
    LIVE_VIEW_O2,          /* the oxygen sensors */
    LIVE_VIEW_TRANS,       /* the transmission module's own PIDs (CAN cars) */
    LIVE_VIEW_CUSTOM,
    LIVE_VIEW_ALL,
    LIVE_VIEW_COUNT
} live_view_id_t;

#define LIVE_PERIOD_DEFAULT UINT32_MAX   /* use the PID's own refresh time */

const char *live_view_name(int view);      /* "Idle/Vacuum" */
const char *live_view_key(int view);       /* "idle", for config.ini */
const char *live_view_purpose(int view);   /* "Vacuum leaks, rough idle" */

/* The module a view polls (the transmission view: the transmission
 * module alone; the rest: the engine). */
obd_module_t live_view_module(int view);

/* The view for a config key, or -1. */
int live_view_find(const char *key);

/* The Dashboard (LIVE's top screen while the view list is open): RPM and
 * speed as fast as possible, coolant every 5 s, the odometer every minute
 * (the adapter's battery voltage comes with every poll). Returns the count. */
int live_dash_request(uint8_t *pids, uint32_t *periods, int max);

/* All and Custom can hold many PIDs: they show a refresh warning. */
bool live_view_warns(int view);

/*
 * The PIDs a view asks for, in order, with their refresh times
 * (LIVE_PERIOD_DEFAULT = the PID's own). Before the car's support filter
 * (obd_live_init drops what the car lacks). Custom returns custom[].
 * Returns the count (at most max).
 */
int live_view_request(int view, const uint8_t *custom, int ncustom, uint8_t *pids,
                      uint32_t *periods, int max);

/*
 * About how many seconds between reads of the fast PIDs (refresh time 0)
 * at rate requests per second, after the timed PIDs and ATRV take their
 * share. 0 if there are no fast PIDs or the rate isn't known yet.
 */
float live_fast_interval(const obd_live_t *lv, float rate);

#endif
