#include "track_ui.h"

#define TAB_W 128
#define TAB_Y 202

static const track_rect_t none = { 0, 0, 0, 0 };

int track_button_count(track_kind_t kind)
{
    switch (kind) {
    case TRK_MENU:    return TRACK_MENU_ITEMS;
    case TRK_NOTICE:  return 1;   /* I Understand */
    case TRK_TA_LIST: return 3;
    case TRK_SHIFT:   return 2;   /* -, + */
    case TRK_GMETER:  return 2;   /* Calibrate, Reset Peaks */
    case TRK_REVSYNC: return 1;   /* Start / Again */
    case TRK_ECO:     return 1;   /* Reset Trip */
    default:          return 0;
    }
}

static bool has_info(track_kind_t kind)
{
    return kind != TRK_NOTICE && kind != TRK_INFO;
}

track_rect_t track_rect(track_kind_t kind, int hit)
{
    if (hit == TRK_HIT_BACK) {
        return (track_rect_t){ 0, TAB_Y, TAB_W, TRACK_SCREEN_H - TAB_Y };
    }
    if (hit == TRK_HIT_INFO) {
        return has_info(kind) ? (track_rect_t){ TRACK_SCREEN_W - TAB_W, TAB_Y, TAB_W,
                                                TRACK_SCREEN_H - TAB_Y }
                              : none;
    }
    if (hit == TRK_HIT_STOP || hit == TRK_HIT_START) {
        if (kind != TRK_TA_RUN) {
            return none;
        }
        return (track_rect_t){ hit == TRK_HIT_STOP ? 54 : 160, 86, 106, 48 };
    }
    int i = hit - TRK_HIT_BUTTON;
    if (i < 0 || i >= track_button_count(kind)) {
        return none;
    }
    switch (kind) {
    case TRK_MENU:    return (track_rect_t){ 10 + (i % 2) * 154, 9 + (i / 2) * 62, 146, 54 };
    case TRK_NOTICE:  return (track_rect_t){ 20, 148, 280, 46 };
    case TRK_TA_LIST: return (track_rect_t){ 20, 19 + i * 56, 280, 48 };
    case TRK_SHIFT:   return (track_rect_t){ i == 0 ? 14 : 236, 84, 70, 48 };
    case TRK_GMETER:  return (track_rect_t){ 20, 42 + i * 62, 280, 52 };
    case TRK_REVSYNC: return (track_rect_t){ 40, 78, 240, 60 };
    case TRK_ECO:     return (track_rect_t){ 40, 69, 240, 60 };
    default:          return none;
    }
}

static bool in(track_rect_t r, int px, int py)
{
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

int track_hit(track_kind_t kind, int px, int py)
{
    static const int fixed[] = { TRK_HIT_BACK, TRK_HIT_INFO, TRK_HIT_STOP, TRK_HIT_START };
    for (unsigned k = 0; k < sizeof fixed / sizeof fixed[0]; k++) {
        if (in(track_rect(kind, fixed[k]), px, py)) {
            return fixed[k];
        }
    }
    for (int i = 0; i < track_button_count(kind); i++) {
        if (in(track_rect(kind, TRK_HIT_BUTTON + i), px, py)) {
            return TRK_HIT_BUTTON + i;
        }
    }
    return TRK_HIT_NONE;
}

const char *track_menu_label(int i)
{
    static const char *const labels[TRACK_MENU_ITEMS] = {
        "Time Attack", "Shift Light", "G-Meter", "Rev Sync", "Eco", "Achievements",
    };
    return i >= 0 && i < TRACK_MENU_ITEMS ? labels[i] : "";
}

const char *track_ta_label(int run, bool metric)
{
    switch (run) {
    case 0:  return metric ? "0-100 km/h" : "0-60 mph";
    case 1:  return metric ? "100-0 km/h" : "60-0 mph";
    default: return "1/4 Mile";
    }
}

/* The explanations on today's text screens, word for word where they still fit. */
const char *track_info_text(track_kind_t kind)
{
    switch (kind) {
    case TRK_MENU:
    case TRK_NOTICE:
        return "For closed courses, track days and passengers only.\n\n"
               "Nothing here needs a touch while moving: pick a run parked, then just drive.\n\n"
               "No online leaderboards.\n\n"
               "The top screen says what each mode does.";
    case TRK_TA_LIST:
    case TRK_TA_RUN:
        return "Pick a run while parked. The timer then starts and stops by itself, and arms "
               "again once you stop: no touch needed while moving.\n\n"
               "For closed courses, track days and passengers only. Never race on public "
               "roads.\n\n"
               "Best times are kept for each car.\n\n"
               "Times come from the car's speed, read a few times a second: they're estimates.";
    case TRK_SHIFT:
        return "The LEDs on the top screen fill as the RPM rises and flash at the shift "
               "point.\n\n"
               "Set the shift point while parked, a little under the redline (it's saved). - "
               "and + change it by 100 rpm.\n\n"
               "Older cars send RPM a few times a second; the light fills in between.";
    case TRK_GMETER:
        return "Mount the 3DS firmly, screen facing you, then tap Calibrate parked with the "
               "engine idling (the 3DS still).\n\n"
               "Up on the circle is speeding up, down is braking, sides are cornering. Which "
               "way is forward is learned on the first launch (OBD speed).\n\n"
               "Calibrate again after moving it. The sensor numbers wobble a little at rest: "
               "that's normal and averaged out.";
    case TRK_REVSYNC:
        return "Parked, engine warm, in neutral or park. Each round shows a target RPM; at GO, "
               "blip the throttle so the peak lands on it.\n\n"
               "PERFECT within 75 rpm, GREAT 200, GOOD 400, OK 700. GREAT or better in a row "
               "builds a combo.\n\n"
               "Locked unless the speed reads 0.";
    case TRK_ECO:
        return "Drive smoothly for a high score: gentle starts and stops, RPM under 2800, and no "
               "long idling (the first minute of a stop is free).\n\n"
               "Rank: S 90+, A 80+, B 65+, else C.\n\n"
               "The trip runs while this screen is open; Back keeps a best score. Reset Trip "
               "starts a new one while parked.";
    case TRK_ACHIEVE:
        return "Stickers for milestones: scans, clean cars, recorded miles and track modes.\n\n"
               "They unlock by themselves and pop up at the bottom of any screen.";
    default:
        return "";
    }
}
