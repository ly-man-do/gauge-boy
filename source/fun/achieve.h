#ifndef GAUGEBOY_ACHIEVE_H
#define GAUGEBOY_ACHIEVE_H

/*
 * Achievements (brief section 5e): stickers unlocked by milestones, saved
 * to the SD card (achievements.csv: "id,date", one line per unlock):
 *
 *   id,date
 *   first_scan,2026-11-18
 *
 * The list, the rules that need more than "it happened" (a clean scan, a
 * distance, a count) and the file's text are here; the app calls
 * achieve_unlock() where each one happens. Platform-free.
 */

#include <stdbool.h>
#include <stddef.h>

#include "obd/obd_diag.h"

#define ACHIEVE_FILE "achievements.csv"

typedef enum {
    ACH_FIRST_SCAN = 0,   /* a DIAG scan read the car */
    ACH_CLEAN_BILL,       /* CEL off, no stored or pending codes, every monitor ready */
    ACH_CODE_BREAKER,     /* codes cleared */
    ACH_ON_THE_RECORD,    /* a drive recorded with REC */
    ACH_100_MILES,        /* 100 miles logged in recorded drives */
    ACH_FULL_GARAGE,      /* 3 cars in the garage */
    ACH_STOPWATCH,        /* a Time Attack result */
    ACH_PERFECT_SYNC,     /* PERFECT in Rev Sync */
    ACH_REV_S,            /* a Rev Sync game ranked S */
    ACH_HYPERMILER,       /* an Eco trip ranked S */
    ACH_REDLINE,          /* the shift light flashed */
    ACH_PULLING_GS,       /* 0.5 g on the G-meter */
    ACH_COUNT
} achieve_id_t;

#define ACHIEVE_MILES_KM   160.934   /* 100 miles */
#define ACHIEVE_GARAGE_CARS 3
#define ACHIEVE_G           0.5f

typedef struct {
    const char *key;      /* in the file: "first_scan" */
    const char *name;     /* "First Scan" */
    const char *how;      /* how to get it: "Scan a car in DIAG" */
} achieve_info_t;

const achieve_info_t *achieve_info(achieve_id_t id);

/* Whether a finished DIAG scan earns ACH_FIRST_SCAN / ACH_CLEAN_BILL. */
bool achieve_scan_ok(const obd_diag_t *d);
bool achieve_scan_clean(const obd_diag_t *d);

/* Unlocked in the file's text; date (may be NULL) gets when. */
bool achieve_unlocked(const char *text, achieve_id_t id, char *date, size_t date_len);

/* Unlocks id: if it's new, writes the text with it into out (returns the
 * length, 0 if out is too small) and sets *is_new. Otherwise *is_new is
 * false and nothing is written (returns 0). */
size_t achieve_unlock(const char *text, achieve_id_t id, const char *date, bool *is_new, char *out,
                      size_t len);

/* How many are unlocked. */
int achieve_count(const char *text);

#endif
