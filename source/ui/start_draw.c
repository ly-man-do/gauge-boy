#include "start_draw.h"

#include <stdio.h>

#include "icons.h"
#include "kit.h"

static void title(const char *version)
{
    float hs = KIT_PX(20);
    float w1 = kit_text_width("GAUGE BOY: ", hs), w2 = kit_text_width("OBD-II DELUXE", hs);
    float x = (KIT_TOP_W - (24 + 7 + w1 + w2)) / 2;
    kit_image(kit_icon(icons_app_idx), x, 25);
    kit_text("GAUGE BOY: ", x + 31, 24, hs, KIT_INK, -1, true);
    kit_text("OBD-II DELUXE", x + 31 + w1, 24, hs, KIT_ORANGE, -1, true);
    kit_rule(58.5f);
    char v[40];
    snprintf(v, sizeof v, "version %s", version);
    kit_text(v, KIT_TOP_W - 12, 4, KIT_PX(9.5f), KIT_RGB(122, 122, 122), 1, false);
}

static void panel(float y, float h)
{
    kit_rrect(17, y, 366, h, 6, KIT_PANEL_EDGE);
    kit_rrect(18, y + 1, 364, h - 2, 5, KIT_PANEL);
}

static void lines(const char *text, float x, float y, float w)
{
    char out[4][96];
    float s = KIT_PX(14);
    int n = kit_wrap(text, w, s, out, 4);
    for (int i = 0; i < n; i++) {
        kit_text(out[i], x, y + i * 20, s, KIT_INK, -1, true);
    }
}

void start_draw(bool unsupported, const char *version, float pulse)
{
    kit_begin(true, 20);
    title(version);
    if (unsupported) {
        panel(72, 100);
        kit_text("Unsupported device", 32, 84, KIT_PX(18), KIT_RED_HI, -1, true);
        lines("Gauge Boy needs a New 3DS, New 3DS XL or New 2DS XL.", 32, 114, 336);
    } else {
        panel(72, 120);
        /* An amber warning sign. */
        float tx = 50, ty = 88;
        kit_quad(tx, ty, tx + 15, ty + 26, tx + 15, ty + 26, tx - 15, ty + 26, KIT_AMBER);
        kit_rrect(tx - 1.5f, ty + 8, 3, 10, 1, KIT_BG);
        kit_rrect(tx - 1.5f, ty + 20, 3, 3, 1, KIT_BG);
        kit_text("SAFETY NOTICE", 80, 84, KIT_PX(18), KIT_AMBER, -1, true);
        lines("Not for use by the driver while driving. Use it parked, or let a passenger "
              "operate it.", 80, 114, 290);
        kit_text("Unofficial homebrew. Not made, endorsed or supported by Nintendo.", KIT_TOP_W / 2.0f,
                 212, KIT_PX(10.5f), KIT_INK3, 0, false);
    }
    kit_end();

    kit_begin(false, 0);
    float s = KIT_PX(16);
    if (unsupported) {
        kit_text("Tap the touch screen or", 160, 96, s, KIT_INK, 0, true);
        kit_text("press START to exit.", 160, 120, s, KIT_INK, 0, true);
    } else {
        /* A slowly pulsing ring: tap anywhere. */
        u32 c = KIT_RGBA(242, 244, 245, (int)(pulse * 255));
        kit_ring(130, 56, 60, 60, 30, 3, c);
        kit_circle(160, 86, 10, KIT_INK);
        kit_text("Tap the touch screen to continue.", 160, 140, KIT_PX(17), KIT_INK, 0, true);
    }
    kit_end();
}
