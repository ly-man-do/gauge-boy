#ifndef GAUGEBOY_DRIVE_LIST_H
#define GAUGEBOY_DRIVE_LIST_H

/*
 * The LOG screen's list of recorded drives: what a drive log's header and
 * last row say (drive_log.h writes them), as list rows and detail lines.
 * Platform-free: the app reads the file's first and last bytes on the
 * worker and passes the text in.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DRIVE_LIST_MAX   100     /* newest first; older ones aren't listed */
#define DRIVE_HEAD_BYTES 1024    /* read this much of the start of a file */
#define DRIVE_TAIL_BYTES 512     /* and of the end, for the last row's time */
#define DRIVE_INFO_COLS  49
#define DRIVE_INFO_LINES 16

typedef struct {
    char name[32];          /* "drive-20261116-171230.csv" */
    uint32_t size;          /* bytes */
    bool ok;                /* a GaugeBoy drive log header was found */
    char date[20];          /* "2026-11-16 17:12:30" */
    char car[48];
    char vin[18];
    char protocol[48];
    char version[24];
    bool metric;
    float rate_hz;
    int columns;            /* value columns, time not counted */
    char values[160];       /* "RPM, Speed, Coolant, Battery" */
    float duration_s;       /* last row's time; -1 if unknown */
} drive_info_t;

/* True for a drive log file name: "drive-" ... ".csv". */
bool drive_list_is_log_name(const char *name);

/* Sorts names newest first (the names carry the date and time). */
void drive_list_sort(char (*names)[32], int n);

/* Fills d from the start of a file (name and size set by the caller).
 * Returns d->ok. */
bool drive_info_parse_head(drive_info_t *d, const char *text, size_t len);

/* The time of the last complete data row in the end of a file; -1 if
 * there is none. */
float drive_info_parse_tail(const char *text, size_t len);

/* A LIST row, "11-16 17:12  2023 Subaru Ascent  4:32", cols wide. */
void drive_info_row(const drive_info_t *d, char *out, size_t len, int cols);

/* The details for the top screen. Returns the line count. */
int drive_info_lines(const drive_info_t *d, char lines[][DRIVE_INFO_COLS + 1], int max);

#endif
