#ifndef GAUGEBOY_LIVE_VIEW_H
#define GAUGEBOY_LIVE_VIEW_H

/*
 * LIVE screen text (M2 text dashboard; the toy-like gauges come in M5) and
 * the PID choice behind its toggles. Platform-free.
 */

#include <stdbool.h>
#include <stdint.h>

#include "obd/obd_link.h"
#include "obd/obd_live.h"
#include "obd/obd_pid.h"

#define LIVE_VIEW_COLS 49   /* top-screen text width */
#define LIVE_VIEW_MAX  28

/*
 * Top-screen lines: a title with the protocol and requests per second,
 * then one line per polled PID (label, value, refresh rate), the adapter's
 * battery voltage and, when MAP is polled, manifold vacuum. lv may be NULL
 * (not polling yet). Returns the line count.
 */
int live_view_lines(const obd_live_t *lv, int protocol, float rate, obd_units_t units, int cols,
                    char lines[][LIVE_VIEW_COLS + 1], int max);

/* PIDs the car supports that LIVE can show, in obd_pid table order (for
 * the toggles). Returns the count. */
int live_choices(const obd_link_t *link, uint8_t *out, int max);

bool live_pid_chosen(const uint8_t *pids, int count, uint8_t pid);

/* Adds pid at the end, or removes it. Returns the new count (unchanged if
 * the list is full). */
int live_toggle(uint8_t *pids, int count, int max, uint8_t pid);

#endif
