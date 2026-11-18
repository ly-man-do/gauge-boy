#include "live_views.h"

#include <string.h>

#include "obd/obd_pid.h"

#define D LIVE_PERIOD_DEFAULT

typedef struct {
    uint8_t pid;
    uint32_t period_ms;
} view_pid_t;

typedef struct {
    const char *name;
    const char *key;
    const char *purpose;
    const view_pid_t *pids;
    int count;
} view_def_t;

/* Engine: is it running right? Coolant every 5 s here. */
static const view_pid_t engine[] = {
    { 0x0C, D }, { 0x0D, D }, { 0x05, 5000 }, { 0x11, D }, { 0x04, D },
};
/* Idle & vacuum: vacuum leaks show as high short-term trims and low vacuum.
 * MAP is 0B, or 87 on cars without it (the turbo Ascent); baro 33 makes the
 * vacuum and boost figures exact. Unsupported PIDs are skipped. */
static const view_pid_t idle[] = {
    { 0x0C, D }, { 0x0B, D }, { 0x87, D }, { 0x06, D }, { 0x08, D }, { 0x0E, D }, { 0x33, D },
    { 0x03, D },
};
/* Air & fuel: lean / rich, air flow (MAF or MAP, whichever the car has).
 * Open or closed loop (03): the trims only steer in closed loop. */
static const view_pid_t air_fuel[] = {
    { 0x10, D }, { 0x0B, D }, { 0x87, D }, { 0x0F, D }, { 0x11, D },
    { 0x06, D }, { 0x08, D }, { 0x07, 2000 }, { 0x09, 2000 }, { 0x33, D },
    { 0x03, D }, { 0x44, D },
};
/* O2 sensors: is each one switching (narrow band) or near 1.000 (wide
 * band)? Whichever kind and sensors the car has; RPM and the short-term
 * trims beside them. */
static const view_pid_t o2[] = {
    { 0x0C, D }, { 0x06, D }, { 0x08, D },
    { 0x14, D }, { 0x15, D }, { 0x16, D }, { 0x17, D }, { 0x18, D }, { 0x19, D }, { 0x1A, D }, { 0x1B, D },
    { 0x24, D }, { 0x25, D }, { 0x26, D }, { 0x27, D }, { 0x28, D }, { 0x29, D }, { 0x2A, D }, { 0x2B, D },
    { 0x34, D }, { 0x35, D }, { 0x36, D }, { 0x37, D }, { 0x38, D }, { 0x39, D }, { 0x3A, D }, { 0x3B, D },
};
/* Warm-up: thermostat, cold start; the temperatures are the point here. */
static const view_pid_t warmup[] = {
    { 0x05, 1000 }, { 0x0F, 2000 }, { 0x5C, 2000 }, { 0x46, D }, { 0x1F, 1000 }, { 0x0C, D },
};
/* Drive: load under acceleration; load and timing as fast as RPM. The
 * gear ratio, torque and odometer where the car reports them (newer CAN
 * cars). Fuel rate and economy: the car's fuel rate PID, else MAF, else MAP
 * with intake air (the estimate), once a second: enough for a fuel figure
 * without slowing RPM and speed on slow buses. */
static const view_pid_t drive[] = {
    { 0x0C, D }, { 0x0D, D }, { 0x11, D }, { 0x04, 0 }, { 0x0E, 0 }, { 0x62, D }, { 0xA4, D },
    { 0xA6, D }, { 0x5E, 1000 }, { 0x9D, 1000 }, { 0x10, 1000 }, { 0x0B, 1000 }, { 0x87, 1000 },
    { 0x0F, 10000 },
};

#define N(a) ((int)(sizeof a / sizeof a[0]))

static const view_def_t views[LIVE_VIEW_COUNT] = {
    [LIVE_VIEW_ENGINE]   = { "Engine", "engine", "Is it running right?", engine, N(engine) },
    [LIVE_VIEW_IDLE]     = { "Idle & vacuum", "idle", "Vacuum leaks, rough idle", idle, N(idle) },
    [LIVE_VIEW_AIR_FUEL] = { "Air & fuel", "airfuel", "Lean or rich, air flow", air_fuel,
                             N(air_fuel) },
    [LIVE_VIEW_O2]       = { "O2 sensors", "o2", "Is each oxygen sensor working?", o2, N(o2) },
    [LIVE_VIEW_WARMUP]   = { "Warm-up", "warmup", "Thermostat, cold start", warmup, N(warmup) },
    [LIVE_VIEW_DRIVE]    = { "Drive", "drive", "Load, fuel rate, economy", drive, N(drive) },
    [LIVE_VIEW_ALL]      = { "All", "all", "Every PID the car supports", NULL, 0 },
    [LIVE_VIEW_CUSTOM]   = { "Custom", "custom", "Your own pick (EDIT)", NULL, 0 },
    [LIVE_VIEW_TRANS]    = { "Transmission", "trans", "The transmission module (CAN cars)", NULL, 0 },
};

static bool valid(int view)
{
    return view >= 0 && view < LIVE_VIEW_COUNT;
}

const char *live_view_name(int view)
{
    return valid(view) ? views[view].name : "?";
}

const char *live_view_key(int view)
{
    return valid(view) ? views[view].key : "";
}

const char *live_view_purpose(int view)
{
    return valid(view) ? views[view].purpose : "";
}

int live_view_find(const char *key)
{
    for (int v = 0; key != NULL && v < LIVE_VIEW_COUNT; v++) {
        if (strcmp(views[v].key, key) == 0) {
            return v;
        }
    }
    return -1;
}

bool live_view_warns(int view)
{
    return view == LIVE_VIEW_ALL || view == LIVE_VIEW_CUSTOM;
}

obd_module_t live_view_module(int view)
{
    return view == LIVE_VIEW_TRANS ? OBD_MODULE_TRANSMISSION : OBD_MODULE_ENGINE;
}

int live_view_request(int view, const uint8_t *custom, int ncustom, uint8_t *pids,
                      uint32_t *periods, int max)
{
    int n = 0;
    if (view == LIVE_VIEW_ALL || view == LIVE_VIEW_TRANS) {   /* whatever the module supports */
        for (int i = 0; i < obd_pid_count() && n < max; i++) {
            pids[n] = obd_pid_at(i)->pid;
            periods[n++] = D;
        }
    } else if (view == LIVE_VIEW_CUSTOM) {
        for (int i = 0; custom != NULL && i < ncustom && n < max; i++) {
            pids[n] = custom[i];
            periods[n++] = D;
        }
    } else if (valid(view)) {
        for (int i = 0; i < views[view].count && n < max; i++) {
            pids[n] = views[view].pids[i].pid;
            periods[n++] = views[view].pids[i].period_ms;
        }
    }
    return n;
}

float live_fast_interval(const obd_live_t *lv, float rate)
{
    if (rate <= 0.0f) {
        return 0.0f;
    }
    int fast = 0;
    float timed = lv->volts_failed ? 0.0f : 1000.0f / (float)OBD_LIVE_VOLTS_MS;
    for (int i = 0; i < lv->count; i++) {
        const obd_live_pid_t *p = &lv->pid[i];
        if (p->dropped) {
            continue;
        }
        if (p->period_ms == 0) {
            fast++;
        } else {
            timed += 1000.0f / (float)p->period_ms;
        }
    }
    if (fast == 0) {
        return 0.0f;
    }
    float left = rate - timed;
    if (left < rate * 0.1f) {
        left = rate * 0.1f;   /* the timed ones can't take it all: they wait too */
    }
    return (float)fast / left;
}
