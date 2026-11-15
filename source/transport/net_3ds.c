/* libctru soc / ac service setup. 3DS build only (not part of the host tests). */
#include "net.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *soc_buffer;
static bool ac_ok;

bool net_init(void)
{
    if (soc_buffer != NULL) {
        return true;
    }
    /* ac only reports the Wi-Fi state for the log; soc works without it. */
    ac_ok = R_SUCCEEDED(acInit());

    soc_buffer = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (soc_buffer == NULL) {
        return false;
    }
    if (R_FAILED(socInit(soc_buffer, SOC_BUFFERSIZE))) {
        free(soc_buffer);
        soc_buffer = NULL;
        return false;
    }
    return true;
}

void net_exit(void)
{
    if (ac_ok) {
        acExit();
        ac_ok = false;
    }
    if (soc_buffer == NULL) {
        return;
    }
    socExit();
    free(soc_buffer);
    soc_buffer = NULL;
}

/* "slot N" from the AC_AP_TYPE_SLOTn bits, or "" if none is set. */
static const char *slot_text(u32 ap_type)
{
    if (ap_type & AC_AP_TYPE_SLOT1) return " (slot 1)";
    if (ap_type & AC_AP_TYPE_SLOT2) return " (slot 2)";
    if (ap_type & AC_AP_TYPE_SLOT3) return " (slot 3)";
    return "";
}

void net_describe(char *out, size_t len)
{
    if (len == 0) {
        return;
    }
    out[0] = '\0';

    char wifi[80];
    u32 ap_type = 0;
    if (!ac_ok || R_FAILED(ACU_GetWifiStatus(&ap_type))) {
        snprintf(wifi, sizeof wifi, "Wi-Fi state unknown");
    } else if (ap_type == 0) {
        snprintf(out, len, "Wi-Fi not connected (join the adapter's network in System Settings)");
        return;
    } else {
        char ssid[0x21];
        memset(ssid, 0, sizeof ssid);
        if (R_SUCCEEDED(ACU_GetSSID(ssid)) && ssid[0] != '\0') {
            ssid[0x20] = '\0';
            snprintf(wifi, sizeof wifi, "Wi-Fi on, \"%s\"%s", ssid, slot_text(ap_type));
        } else {
            snprintf(wifi, sizeof wifi, "Wi-Fi on%s", slot_text(ap_type));
        }
    }

    if (soc_buffer == NULL) {
        snprintf(out, len, "%s, network service not started", wifi);
        return;
    }
    /* gethostid: the console's IPv4 address in network byte order, 0 if none. */
    struct in_addr ip;
    ip.s_addr = (in_addr_t)gethostid();
    if (ip.s_addr == 0 || ip.s_addr == INADDR_NONE) {
        snprintf(out, len, "%s, no IP address (try a manual IP in the connection's settings)", wifi);
    } else {
        snprintf(out, len, "%s, 3DS IP %s", wifi, inet_ntoa(ip));
    }
}
