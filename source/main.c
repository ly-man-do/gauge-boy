/*
 * GaugeBoy: OBD-II Deluxe
 * Entry point: New 3DS check, speedup mode, startup safety notice.
 */
#include <3ds.h>
#include <stdbool.h>
#include <stdio.h>

#include "version.h"

/* Show the console until START is pressed or the touch screen is tapped. */
static void wait_for_exit(void)
{
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & (KEY_START | KEY_TOUCH)) {
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
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
    consoleInit(GFX_TOP, NULL);

    if (!is_new_3ds()) {
        printf("GaugeBoy %s\n\n", GB_VERSION);
        printf("Unsupported device.\n\n");
        printf("GaugeBoy needs a New 3DS,\n");
        printf("New 3DS XL or New 2DS XL.\n\n");
        printf("Tap the touch screen or\n");
        printf("press START to exit.\n");
        wait_for_exit();
        gfxExit();
        return 0;
    }

    osSetSpeedupEnable(true);

    printf("GaugeBoy: OBD-II Deluxe %s\n\n", GB_VERSION);
    printf("SAFETY NOTICE\n");
    printf("Not for use by the driver while\n");
    printf("driving. Use it parked, or let a\n");
    printf("passenger operate it.\n\n");
    printf("Tap the touch screen or\n");
    printf("press START to exit.\n");
    wait_for_exit();

    osSetSpeedupEnable(false);
    gfxExit();
    return 0;
}
