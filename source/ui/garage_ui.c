#include "garage_ui.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define TAB_W    128
#define TAB_Y    202

static const garage_rect_t none = { 0, 0, 0, 0 };

int garage_button_count(garage_kind_t kind)
{
    switch (kind) {
    case GAR_CARD:   return 4;
    case GAR_CAMERA: return 1;
    case GAR_SHOT:   return 3;
    case GAR_TRIP:   return 1;
    case GAR_ENTRY:  return 2;
    default:         return 0;
    }
}

garage_rect_t garage_rect(garage_kind_t kind, int hit)
{
    switch (hit) {
    case GAR_HIT_BACK:
        return (garage_rect_t){ 0, TAB_Y, TAB_W, SCREEN_H - TAB_Y };
    case GAR_HIT_RIGHT:
        return kind == GAR_CARD || kind == GAR_TRIP || kind == GAR_MILES
                   ? (garage_rect_t){ SCREEN_W - TAB_W, TAB_Y, TAB_W, SCREEN_H - TAB_Y } : none;
    case GAR_HIT_PREV:
        return kind == GAR_CARD ? (garage_rect_t){ 0, 12, 40, 106 } : none;
    case GAR_HIT_NEXT:
        return kind == GAR_CARD ? (garage_rect_t){ SCREEN_W - 40, 12, 40, 106 } : none;
    default:
        break;
    }
    int i = hit - GAR_HIT_BUTTON;
    if (i < 0 || i >= garage_button_count(kind)) {
        return none;
    }
    switch (kind) {
    case GAR_CARD:
        return i < 2 ? (garage_rect_t){ 48 + i * 114, 78, 110, 44 }
                     : (garage_rect_t){ 48 + (i - 2) * 114, 128, 110, 48 };
    case GAR_CAMERA: return (garage_rect_t){ 40, 60, 240, 70 };
    case GAR_SHOT:
        return i == GAR_USE ? (garage_rect_t){ 20, 36, 280, 56 }
                            : (garage_rect_t){ 20 + (i - 1) * 144, 102, 136, 46 };
    case GAR_TRIP:   return (garage_rect_t){ 40, 69, 240, 60 };
    case GAR_ENTRY:  return (garage_rect_t){ 20, 42 + i * 62, 280, 52 };
    default:         return none;
    }
}

static bool in(garage_rect_t r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

int garage_hit(garage_kind_t kind, int px, int py)
{
    for (int h = GAR_HIT_BACK; h < GAR_HIT_BUTTON; h++) {
        if (in(garage_rect(kind, h), px, py)) {
            return h;
        }
    }
    for (int i = 0; i < garage_button_count(kind); i++) {
        if (in(garage_rect(kind, GAR_HIT_BUTTON + i), px, py)) {
            return GAR_HIT_BUTTON + i;
        }
    }
    return GAR_HIT_NONE;
}

garage_rect_t garage_photo_frame(void)
{
    return (garage_rect_t){ 29, 76, 100, 75 };
}

const char *garage_info_text(garage_kind_t kind)
{
    if (kind == GAR_TRIP) {
        return "Distance from the car's own odometer since you last tapped Reset Trip.\n\n"
               "Connected to this car, it reads the odometer live; otherwise it uses the last "
               "reading Gauge Boy saved.";
    }
    return "A card for each car Gauge Boy knows: by its VIN, or the make, model and year set in "
           "SETUP > VEHICLE.\n\n"
           "Logged distance counts recorded drives (LIVE or TRIM, REC).\n\n"
           "The half circles go to the other cars. Nickname opens the keyboard; an empty name "
           "removes it. Photo takes the card's picture with the outer camera.";
}
