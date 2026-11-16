/*
 * GaugeBoy: OBD-II Deluxe
 * Entry point: New 3DS check, speedup mode, startup safety notice, then
 * the M0 spike app.
 */
#include <3ds.h>
#include <stdbool.h>
#include <stdio.h>

#include "spike_ui.h"
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

int main(void)
{
    gfxInitDefault();
    PrintConsole top;
    consoleInit(GFX_TOP, &top);

    if (!is_new_3ds()) {
        printf("GaugeBoy %s\n\n", GB_VERSION_TEXT);
        printf("Unsupported device.\n\n");
        printf("GaugeBoy needs a New 3DS,\n");
        printf("New 3DS XL or New 2DS XL.\n\n");
        printf("Tap the touch screen or\n");
        printf("press START to exit.\n");
        wait_for_tap();
        gfxExit();
        return 0;
    }

    osSetSpeedupEnable(true);

    printf("GaugeBoy: OBD-II Deluxe %s\n\n", GB_VERSION_TEXT);
    printf("SAFETY NOTICE\n");
    printf("Not for use by the driver while\n");
    printf("driving. Use it parked, or let a\n");
    printf("passenger operate it.\n\n");
    printf("Tap the touch screen to continue.\n");

    if (wait_for_tap()) {
        spike_ui_run();
    }

    osSetSpeedupEnable(false);
    gfxExit();
    return 0;
}
