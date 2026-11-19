#include "log_ui.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define TAB_W    128
#define TAB_Y    202

static const log_rect_t none = { 0, 0, 0, 0 };

int log_button_count(log_kind_t kind)
{
    return kind == LOG_DRIVE ? 2 : kind == LOG_GRAPH ? 5 : 0;
}

log_rect_t log_rect(log_kind_t kind, int hit)
{
    switch (hit) {
    case LOG_HIT_BACK:
        return (log_rect_t){ 0, TAB_Y, TAB_W, SCREEN_H - TAB_Y };
    case LOG_HIT_INFO:
        return kind == LOG_LIST ? (log_rect_t){ SCREEN_W - TAB_W, TAB_Y, TAB_W, SCREEN_H - TAB_Y } : none;
    case LOG_HIT_PREV:
        return kind == LOG_GRAPH ? (log_rect_t){ 0, 66, 40, 106 } : none;
    case LOG_HIT_NEXT:
        return kind == LOG_GRAPH ? (log_rect_t){ SCREEN_W - 40, 66, 40, 106 } : none;
    default:
        break;
    }
    int i = hit - LOG_HIT_BUTTON;
    if (i < 0 || i >= log_button_count(kind)) {
        return none;
    }
    if (kind == LOG_DRIVE) {
        return (log_rect_t){ 20, 42 + i * 62, 280, 52 };
    }
    if (i < 4) {   /* GRAPH's row: earlier, zoom out, zoom in, later */
        return (log_rect_t){ 8 + i * 78, 10, 70, 44 };
    }
    return (log_rect_t){ 70, 126, 180, 40 };   /* Whole Drive */
}

static bool in(log_rect_t r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

int log_hit(log_kind_t kind, int px, int py)
{
    static const int fixed[] = { LOG_HIT_BACK, LOG_HIT_INFO, LOG_HIT_PREV, LOG_HIT_NEXT };
    for (unsigned k = 0; k < sizeof fixed / sizeof fixed[0]; k++) {
        if (in(log_rect(kind, fixed[k]), px, py)) {
            return fixed[k];
        }
    }
    for (int i = 0; i < log_button_count(kind); i++) {
        if (in(log_rect(kind, LOG_HIT_BUTTON + i), px, py)) {
            return LOG_HIT_BUTTON + i;
        }
    }
    return LOG_HIT_NONE;
}

void log_duration(float secs, char *out, size_t len)
{
    if (!(secs >= 0.0f)) {
        snprintf(out, len, "--");
        return;
    }
    unsigned s = (unsigned)(secs + 0.5f);
    if (s >= 3600) {
        snprintf(out, len, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
    } else {
        snprintf(out, len, "%u:%02u", s / 60, s % 60);
    }
}

void log_size(unsigned long bytes, char *out, size_t len)
{
    if (bytes >= 1024UL * 1024UL) {
        snprintf(out, len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(out, len, "%lu KB", (bytes + 1023UL) / 1024UL);
    }
}

void log_row_parts(const drive_info_t *d, char *when, size_t wlen, char *car, size_t clen,
                   char *dur, size_t dlen)
{
    /* "2026-11-19 08:42:10" -> "11/19 08:42" */
    if (strlen(d->date) >= 16) {
        snprintf(when, wlen, "%.2s/%.2s %.5s", d->date + 5, d->date + 8, d->date + 11);
    } else {
        snprintf(when, wlen, "%.11s", d->name);
    }
    snprintf(car, clen, "%s", !d->ok ? "Not a drive log" : d->car[0] ? d->car : "Car not known");
    log_duration(d->duration_s, dur, dlen);
}

const char *log_info_text(void)
{
    return "Record a drive with REC on the LIVE screen: it saves that view's values to a CSV "
           "file, 5 rows a second, in sdmc:/3ds/gauge-boy/drives/.\n\n"
           "Tap a drive for its details, its graph, or to delete it.";
}
