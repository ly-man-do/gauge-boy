#include "home_layout.h"

#include <stddef.h>

/* Mockup v7 (docs/mockups/home-v7.html): margins 8 px. */
#define STATUS_Y 6
#define STATUS_H 34
#define STATUS_W 148
#define CARD_H   74
#define ROW1_Y   45
#define ROW2_Y   122

/* The three columns, 98 px wide. */
static const int col_x[3] = { 8, 111, 214 };
static const int col_w[3] = { 98, 98, 98 };
#define MARGIN   8

static const struct {
    const char *label;
    const char *sub;
} names[HOME_ITEM_COUNT] = {
    [HOME_GARAGE]     = { "", NULL },
    [HOME_ADAPTER] = { "", NULL },
    [HOME_LIVE]    = { "LIVE",  NULL },
    [HOME_DIAG]    = { "DIAG",  NULL },
    [HOME_TRIM]    = { "TRIM",  NULL },
    [HOME_TRACK]   = { "TRACK", NULL },
    [HOME_LOG]     = { "LOG",   NULL },
    [HOME_SETUP]   = { "SETUP", NULL },
    [HOME_CLOSE]   = { "", NULL },
};

home_rect_t home_item_rect(home_item_t item)
{
    home_rect_t r = { 0, 0, 0, 0 };
    if (item == HOME_GARAGE || item == HOME_ADAPTER) {
        r.x = item == HOME_ADAPTER ? HOME_SCREEN_W - MARGIN - STATUS_W : MARGIN;
        r.y = STATUS_Y;
        r.w = STATUS_W;
        r.h = STATUS_H;
    } else if (item == HOME_CLOSE) {
        r.x = 0;
        r.y = HOME_BAR_Y;
        r.w = HOME_SCREEN_W;
        r.h = HOME_SCREEN_H - HOME_BAR_Y;
    } else if (item >= HOME_LIVE && item <= HOME_SETUP) {
        int k = item - HOME_LIVE;
        r.x = col_x[k % 3];
        r.y = k < 3 ? ROW1_Y : ROW2_Y;
        r.w = col_w[k % 3];
        r.h = CARD_H;
    }
    return r;
}

home_rect_t home_item_touch(home_item_t item)
{
    home_rect_t r = home_item_rect(item);
    if (item == HOME_GARAGE || item == HOME_ADAPTER) {
        r.h += r.y;   /* from the top edge */
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

const char *home_diag_label(const home_view_t *v)
{
    return v->cel_known && v->cel_on ? "CEL ON" : home_item_label(HOME_DIAG);
}

const char *home_adapter_text(const home_view_t *v)
{
    return v->connected ? "Connected" : "Not connected";
}
