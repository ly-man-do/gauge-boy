#include "wifi_guide.h"

#include <stdio.h>
#include <string.h>

#include "data/config.h"

static const char *const titles[WIFI_GUIDE_STEPS] = {
    "Before you start",
    "Save the adapter's Wi-Fi",
    "Set the IP address",
    "Set the DNS",
    "Back in GaugeBoy",
};

const char *wifi_guide_title(int step)
{
    return step >= 0 && step < WIFI_GUIDE_STEPS ? titles[step] : "";
}

char *wifi_guide_3digit(const char *ip, char *out, size_t len)
{
    unsigned a, b, c, d;
    if (ip != NULL && config_valid_ipv4(ip) && sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        snprintf(out, len, "%03u.%03u.%03u.%03u", a, b, c, d);
    } else {
        snprintf(out, len, "%s", ip != NULL ? ip : "");
    }
    return out;
}

/* The 3DS's own address: the adapter's /24 with .11 (.12 if the adapter
 * itself is .11), as SETUP SLOT plans it. */
static void own_ip(const char *adapter, char *out, size_t len)
{
    unsigned a, b, c, d;
    if (adapter != NULL && config_valid_ipv4(adapter) &&
        sscanf(adapter, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        snprintf(out, len, "%u.%u.%u.%u", a, b, c, d == 11 ? 12u : 11u);
    } else {
        snprintf(out, len, "192.168.0.11");
    }
}

int wifi_guide_lines(int step, const char *adapter_host, int cols,
                     char lines[][DIAG_VIEW_COLS + 1], int max)
{
    char me[16], me3[20], gw3[20];
    own_ip(adapter_host, me, sizeof me);
    wifi_guide_3digit(me, me3, sizeof me3);
    wifi_guide_3digit(adapter_host, gw3, sizeof gw3);

    char text[700];
    switch (step) {
    case 0:
        snprintf(text, sizeof text,
                 "Plug the Wi-Fi OBD-II adapter into the car and turn the ignition on: most "
                 "adapters only power up then. The adapter makes its own Wi-Fi network (the "
                 "Vgate iCar Pro calls it V-LINK). It has no internet, so the 3DS fails its "
                 "connection test there; GaugeBoy answers that test itself once the slot is set "
                 "up as shown in the next steps. Close GaugeBoy and open System Settings.");
        break;
    case 1:
        snprintf(text, sizeof text,
                 "System Settings > Internet Settings > Connection Settings > New Connection > "
                 "Manual Setup > Search for an Access Point, and pick the adapter's network. "
                 "Save it in a free slot and note its number. The connection test fails (error "
                 "003-2002): that's expected, keep the slot.");
        break;
    case 2:
        snprintf(text, sizeof text,
                 "Open that slot > Change Settings > IP Address:\n"
                 "\n"
                 "  Auto-Obtain IP Address: No\n"
                 "  IP Address:  %s\n"
                 "  Subnet Mask: 255.255.255.000\n"
                 "  Gateway:     %s (the adapter)",
                 me3, gw3);
        break;
    case 3:
        snprintf(text, sizeof text,
                 "Then DNS:\n"
                 "\n"
                 "  Auto-Obtain DNS: No\n"
                 "  Primary DNS:   %s\n"
                 "  Secondary DNS: 000.000.000.000\n"
                 "\n"
                 "The primary DNS is the 3DS itself, so the connection test asks GaugeBoy. "
                 "Save. The test still fails because GaugeBoy isn't running: that's expected.",
                 me3);
        break;
    default:
        snprintf(text, sizeof text,
                 "Open GaugeBoy, SETUP > WI-FI > Wi-Fi slot: tap until it shows the adapter's slot. "
                 "Then come back here and tap CHECK: it reads the slot's settings and tries the "
                 "adapter. When all is OK, go to DIAG and tap SCAN with the engine running.");
        break;
    }
    /* Wrap each line of the text on its own ("\n" separated). */
    int n = 0;
    char *p = text;
    while (p != NULL && n < max) {
        char *nl = strchr(p, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        if (*p == '\0') {
            lines[n++][0] = '\0';
        } else {
            n += diag_wrap(p, cols, 2, lines + n, max - n);
        }
        p = nl != NULL ? nl + 1 : NULL;
    }
    return n;
}
