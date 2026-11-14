#include "textlog.h"

#include <string.h>

void textlog_init(textlog_t *log)
{
    log->head = 0;
    log->count = 0;
    log->version = 0;
}

static char *push_line(textlog_t *log)
{
    int slot;
    if (log->count < TEXTLOG_LINES) {
        slot = (log->head + log->count) % TEXTLOG_LINES;
        log->count++;
    } else {
        slot = log->head;
        log->head = (log->head + 1) % TEXTLOG_LINES;
    }
    log->lines[slot][0] = '\0';
    return log->lines[slot];
}

void textlog_add(textlog_t *log, const char *text)
{
    const char *p = text;

    do {
        /* One logical line: up to '\n' or the end. */
        size_t len = strcspn(p, "\n");
        size_t pos = 0;
        do {
            size_t indent = pos == 0 ? 0 : TEXTLOG_INDENT;
            size_t room = TEXTLOG_COLS - indent;
            size_t take = len - pos < room ? len - pos : room;
            char *line = push_line(log);

            memset(line, ' ', indent);
            for (size_t i = 0; i < take; i++) {
                unsigned char c = (unsigned char)p[pos + i];
                line[indent + i] = (c >= 0x20 && c <= 0x7e) ? (char)c : '?';
            }
            line[indent + take] = '\0';
            pos += take;
        } while (pos < len);

        p += len;
        if (*p == '\n') {
            p++;
        }
    } while (*p != '\0');

    log->version++;
}

int textlog_count(const textlog_t *log)
{
    return log->count;
}

const char *textlog_line(const textlog_t *log, int i)
{
    if (i < 0 || i >= log->count) {
        return "";
    }
    return log->lines[(log->head + i) % TEXTLOG_LINES];
}
