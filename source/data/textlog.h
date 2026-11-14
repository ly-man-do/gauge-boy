#ifndef GAUGEBOY_TEXTLOG_H
#define GAUGEBOY_TEXTLOG_H

/*
 * Ring buffer of fixed-width text lines for an on-screen log. Long
 * entries are wrapped; continuation lines are indented. When full, the
 * oldest lines are dropped. Platform-free and not thread-safe: callers
 * that share it between threads must lock around every call.
 */

#include <stddef.h>

#define TEXTLOG_COLS   49   /* top screen console is 50 columns; keep the last free */
#define TEXTLOG_LINES 256
#define TEXTLOG_INDENT  2   /* spaces before wrapped continuation lines */

typedef struct {
    char lines[TEXTLOG_LINES][TEXTLOG_COLS + 1];
    int head;            /* index of the oldest line */
    int count;
    unsigned version;    /* bumped on every change, for redraw checks */
} textlog_t;

void textlog_init(textlog_t *log);

/* Add an entry; '\n' starts a new line. Non-printable bytes become '?'. */
void textlog_add(textlog_t *log, const char *text);

int textlog_count(const textlog_t *log);

/* Line i, 0 = oldest. Returns "" if i is out of range. */
const char *textlog_line(const textlog_t *log, int i);

#endif
