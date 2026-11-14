#include "textlog.h"

#include <stdbool.h>
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

/* How many chars of s[0..len) go on a line with room columns: break after
 * the last word that fits, or split hard when the only space is too early
 * (e.g. inside "[   0.485]") or there is none (long hex dumps). */
static size_t fit(const char *s, size_t len, size_t room)
{
    if (len <= room) {
        return len;
    }
    for (size_t k = room; k >= room / 3 && k > 0; k--) {
        if (s[k] == ' ') {
            return k;
        }
    }
    return room;
}

void textlog_add(textlog_t *log, const char *text)
{
    const char *p = text;

    do {
        /* One logical line: up to '\n' or the end. */
        size_t len = strcspn(p, "\n");
        size_t pos = 0;
        bool first = true;
        do {
            size_t indent = first ? 0 : TEXTLOG_INDENT;
            size_t take = fit(p + pos, len - pos, TEXTLOG_COLS - indent);
            char *line = push_line(log);

            memset(line, ' ', indent);
            for (size_t i = 0; i < take; i++) {
                unsigned char c = (unsigned char)p[pos + i];
                line[indent + i] = (c >= 0x20 && c <= 0x7e) ? (char)c : '?';
            }
            /* A line ending at a word break carries no trailing spaces. */
            size_t end = indent + take;
            if (pos + take < len) {
                while (end > indent && line[end - 1] == ' ') {
                    end--;
                }
            }
            line[end] = '\0';
            pos += take;
            first = false;
            /* Continuation lines start at the next word. */
            if (pos < len) {
                while (pos < len && p[pos] == ' ') {
                    pos++;
                }
            }
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
