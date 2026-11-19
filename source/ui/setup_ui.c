#include "setup_ui.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define TAB_W    128
#define TAB_Y    202

static const setup_rect_t none = { 0, 0, 0, 0 };

int setup_button_count(setup_kind_t kind)
{
    switch (kind) {
    case SET_MENU:    return 4;
    case SET_WIFI:    return 3;   /* Check & Connect, Wi-Fi slot, Connection Guide */
    case SET_GUIDE:   return 1;   /* Check & Connect */
    case SET_VEHICLE: return SET_VEH_COUNT;
    case SET_OTHER:   return 1;   /* About */
    case SET_TESTS:   return 8;
    default:          return 0;
    }
}

int setup_rows(setup_kind_t kind)
{
    return kind == SET_WIFI ? 3 : kind == SET_VEHICLE ? 5 : kind == SET_OTHER ? 1 : 0;
}

setup_rect_t setup_row(setup_kind_t kind, int i)
{
    switch (kind) {
    case SET_WIFI:    return (setup_rect_t){ 14, 30 + i * 46, 292, 36 };
    case SET_VEHICLE: return (setup_rect_t){ 14, 4 + i * 40, 292, 36 };
    case SET_OTHER:   return (setup_rect_t){ 14, 118, 292, 36 };
    default:          return none;
    }
}

/* A row's touch zone: the row, 2 px taller each way (40 px). */
static setup_rect_t row_touch(setup_kind_t kind, int i)
{
    setup_rect_t r = setup_row(kind, i);
    return (setup_rect_t){ r.x, r.y - 2, r.w, r.h + 4 };
}

setup_rect_t setup_rect(setup_kind_t kind, int hit)
{
    switch (hit) {
    case SET_HIT_BACK: return (setup_rect_t){ 0, TAB_Y, TAB_W, SCREEN_H - TAB_Y };
    case SET_HIT_PREV: return kind == SET_GUIDE ? (setup_rect_t){ 0, 30, 40, 106 } : none;
    case SET_HIT_NEXT: return kind == SET_GUIDE ? (setup_rect_t){ SCREEN_W - 40, 30, 40, 106 } : none;
    case SET_HIT_US:     return kind == SET_OTHER ? (setup_rect_t){ 54, 46, 106, 48 } : none;
    case SET_HIT_METRIC: return kind == SET_OTHER ? (setup_rect_t){ 160, 46, 106, 48 } : none;
    default: break;
    }
    int i = hit - SET_HIT_BUTTON;
    if (i < 0 || i >= setup_button_count(kind)) {
        return none;
    }
    switch (kind) {
    case SET_MENU:  return (setup_rect_t){ 42, 19 + i * 40, 236, 40 };   /* drawn 36 tall from y + 2 */
    case SET_WIFI:  return row_touch(SET_WIFI, i);
    case SET_GUIDE: return (setup_rect_t){ 40, 140, 240, 50 };
    case SET_OTHER: return row_touch(SET_OTHER, 0);
    case SET_TESTS: return (setup_rect_t){ 10 + (i % 2) * 154, 8 + (i / 2) * 47, 146, 42 };
    case SET_VEHICLE: {
        static const int row_of[SET_VEH_COUNT] = { 0, 1, 2, 2, 3, 3, 4 };
        setup_rect_t r = row_touch(SET_VEHICLE, row_of[i]);
        if (i == SET_VEH_YEAR_DOWN || i == SET_VEH_ENGINE_DOWN) {
            return (setup_rect_t){ 214, r.y, 44, r.h };
        }
        if (i == SET_VEH_YEAR_UP || i == SET_VEH_ENGINE_UP) {
            return (setup_rect_t){ 258, r.y, 48, r.h };
        }
        return r;
    }
    default:
        return none;
    }
}

static bool in(setup_rect_t r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

int setup_hit(setup_kind_t kind, int px, int py)
{
    for (int h = SET_HIT_BACK; h < SET_HIT_BUTTON; h++) {
        if (in(setup_rect(kind, h), px, py)) {
            return h;
        }
    }
    for (int i = 0; i < setup_button_count(kind); i++) {
        if (in(setup_rect(kind, SET_HIT_BUTTON + i), px, py)) {
            return SET_HIT_BUTTON + i;
        }
    }
    return SET_HIT_NONE;
}

const char *setup_menu_label(int i)
{
    static const char *const labels[4] = { "Wi-Fi", "Vehicle", "Other", "Tests" };
    return i >= 0 && i < 4 ? labels[i] : "";
}
