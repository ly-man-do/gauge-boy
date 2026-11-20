/*
 * Gauge Boy: OBD-II Deluxe
 * Entry point: New 3DS check, speedup mode, startup safety notice, then
 * the M0 spike app.
 */
#include <3ds.h>
#include <stdbool.h>
#include <stdio.h>

#include "spike_ui.h"
#include "ui/kit.h"
#include "ui/start_draw.h"
#include "version.h"

/* Wait for a tap on the touch screen or START. Returns false if the app
 * should quit (HOME menu close). */
static bool wait_for_tap(void)
{
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & (KEY_START | KEY_TOUCH)) {
            return true;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    return false;
}

static bool is_new_3ds(void)
{
    bool is_new = false;
    if (R_FAILED(APT_CheckNew3DS(&is_new))) {
        return false;
    }
    return is_new;
}

/* The first screen, drawn with the UI kit; false if the kit can't start (then
 * the text version is used). Waits for a tap (or START). *go is false if the app
 * should quit. */
static bool start_screen(bool unsupported, bool *go)
{
    PrintConsole top, bottom;
    consoleInit(GFX_TOP, &top);         /* the consoles' RGB565 framebuffers, as the app uses */
    consoleInit(GFX_BOTTOM, &bottom);
    if (!kit_init()) {
        return false;
    }
    *go = false;
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & (KEY_START | KEY_TOUCH)) {
            *go = true;
            break;
        }
        float t = (float)(osGetTime() % 2400) / 2400.0f;   /* 2.4 s a pulse */
        float pulse = 0.35f + 0.65f * (t < 0.5f ? 1.0f - 2.0f * t : 2.0f * t - 1.0f);
        start_draw(unsupported, GB_VERSION_TEXT, pulse);
    }
    kit_exit();   /* the app starts the kit again with its own screens */
    return true;
}

int main(void)
{
    gfxInitDefault();
    bool go = false;
    bool unsupported = !is_new_3ds();

    if (unsupported) {
        if (!start_screen(true, &go)) {
            PrintConsole top;
            consoleInit(GFX_TOP, &top);
            printf("Gauge Boy %s\n\n", GB_VERSION_TEXT);
            printf("Unsupported device.\n\n");
            printf("Gauge Boy needs a New 3DS,\n");
            printf("New 3DS XL or New 2DS XL.\n\n");
            printf("Tap the touch screen or\n");
            printf("press START to exit.\n");
            wait_for_tap();
        }
        gfxExit();
        return 0;
    }

    osSetSpeedupEnable(true);

    if (!start_screen(false, &go)) {
        PrintConsole top;
        consoleInit(GFX_TOP, &top);
        printf("Gauge Boy: OBD-II Deluxe %s\n\n", GB_VERSION_TEXT);
        printf("SAFETY NOTICE\n");
        printf("Not for use by the driver while\n");
        printf("driving. Use it parked, or let a\n");
        printf("passenger operate it.\n\n");
        printf("Tap the touch screen to continue.\n");
        go = wait_for_tap();
    }
    if (go) {
        spike_ui_run();
    }

    osSetSpeedupEnable(false);
    gfxExit();
    return 0;
}
