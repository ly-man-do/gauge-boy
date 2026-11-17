#ifndef GAUGEBOY_HISTORY_H
#define GAUGEBOY_HISTORY_H

/*
 * History: the last HISTORY_LEN values of each polled PID, with when they
 * were read, in fixed rings (no allocation). Filled from the LIVE poller;
 * read for graphs (graph.h) and window stats. Platform-free.
 */

#include <stdbool.h>
#include <stdint.h>

#include "data/graph.h"
#include "obd/obd_live.h"

#define HISTORY_LEN 512      /* per PID: about 100 s at 5 reads a second */

typedef struct {
    uint8_t pid;
    int head;                /* next slot to write */
    int count;
    uint64_t last_ms;        /* newest sample's time (0 = none) */
    uint64_t t_ms[HISTORY_LEN];
    float v[HISTORY_LEN];
} history_series_t;

typedef struct {
    history_series_t s[OBD_LIVE_MAX];
    int count;
} history_t;

typedef struct {
    int n;
    float min, max, avg;
} history_stats_t;

/* Empty rings for these PIDs (as the poller polls them). */
void history_reset(history_t *h, const uint8_t *pids, int n);

/* Adds a sample if it is newer than the PID's last one. False for a PID
 * not in the history. */
bool history_add(history_t *h, uint8_t pid, uint64_t t_ms, float v);

/* Adds every polled PID's latest value that was read since the last call
 * (by its read time). Returns how many were added. */
int history_from_live(history_t *h, const obd_live_t *lv);

/* Sample i of a PID, 0 = oldest. False out of range. */
bool history_get(const history_t *h, uint8_t pid, int i, uint64_t *t_ms, float *v);

/* Samples of a PID; 0 if it isn't in the history. */
int history_count(const history_t *h, uint8_t pid);

/* min / max / average of a PID's samples read in from_ms..to_ms. */
history_stats_t history_window(const history_t *h, uint8_t pid, uint64_t from_ms,
                               uint64_t to_ms);

/* A PID's samples in from_ms..to_ms into graph columns (times in seconds
 * from from_ms). */
void history_columns(const history_t *h, uint8_t pid, uint64_t from_ms, uint64_t to_ms,
                     int ncols, graph_cols_t *g);

#endif
