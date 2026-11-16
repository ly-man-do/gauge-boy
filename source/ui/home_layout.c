#include "home_layout.h"

#include <stddef.h>

/* Margins 13 px, gaps 6 px: the same grid as the owner-approved preview. */
#define MARGIN   13
#define GAP      6
#define STATUS_Y 5
#define STATUS_H 32
#define CARD_W   94
#define CARD_H   78
#define ROW1_Y   41
#define ROW2_Y   125

static const struct {
    const char *label;
    const char *sub;
} names[HOME_ITEM_COUNT] = {
    [HOME_CEL]     = { "", NULL },
    [HOME_ADAPTER] = { "", NULL },
    [HOME_LIVE]    = { "LIVE",  "Dash gauges" },
    [HOME_DIAG]    = { "DIAG",  "Codes & CEL" },
    [HOME_TRIM]    = { "TRIM",  "Fuel trims" },
    [HOME_FUN]     = { "FUN",   "Fun modes" },
    [HOME_LOG]     = { "LOG",   "Record drives" },
    [HOME_SETUP]   = { "SETUP", "Settings" },
};

home_rect_t home_item_rect(home_item_t item)
{
    home_rect_t r = { 0, 0, 0, 0 };
    int status_w = (HOME_SCREEN_W - 2 * MARGIN - GAP) / 2;   /* 144 */
    if (item == HOME_CEL || item == HOME_ADAPTER) {
        r.x = MARGIN + (item == HOME_ADAPTER ? status_w + GAP : 0);
        r.y = STATUS_Y;
        r.w = status_w;
        r.h = STATUS_H;
    } else if (item >= HOME_LIVE && item < HOME_ITEM_COUNT) {
        int k = item - HOME_LIVE;
        r.x = MARGIN + (k % 3) * (CARD_W + GAP);
        r.y = k < 3 ? ROW1_Y : ROW2_Y;
        r.w = CARD_W;
        r.h = CARD_H;
    }
    return r;
}

home_rect_t home_item_touch(home_item_t item)
{
    home_rect_t r = home_item_rect(item);
    if (item == HOME_CEL || item == HOME_ADAPTER) {
        r.h += r.y + 3;   /* from the top edge: 40 px */
        r.y = 0;
    }
    return r;
}

int home_hit(int px, int py)
{
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        home_rect_t r = home_item_touch((home_item_t)i);
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
            return i;
        }
    }
    return -1;
}

const char *home_item_label(home_item_t item)
{
    return item < HOME_ITEM_COUNT ? names[item].label : "";
}

const char *home_item_sub(home_item_t item)
{
    return item < HOME_ITEM_COUNT ? names[item].sub : NULL;
}

const char *home_cel_text(const home_view_t *v)
{
    if (!v->cel_known) {
        return "CEL not read";
    }
    return v->cel_on ? "CEL ON" : "CEL off";
}

const char *home_adapter_text(const home_view_t *v)
{
    return v->connected ? "Connected" : "Not connected";
}
