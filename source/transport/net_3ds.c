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
static bool ac_ok;          /* ac session open; kept open so a join we asked for lasts */
static Result ac_init_result;
static Handle join_event;   /* pending ACU_ConnectAsync, 0 if none */

static bool ac_open(void)
{
    if (!ac_ok) {
        ac_init_result = acInit();
        ac_ok = R_SUCCEEDED(ac_init_result);
    }
    return ac_ok;
}

bool net_init(void)
{
    if (soc_buffer != NULL) {
        return true;
    }
    /* ac reports and requests the Wi-Fi link; soc works without it. */
    (void)ac_open();

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
    if (join_event != 0) {
        svcCloseHandle(join_event);
        join_event = 0;
    }
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

/* Connection slot from ACU_GetWifiStatus: bit 0 = slot 1 (as Luma3DS and
 * emulators use it; libctru's AC_AP_TYPE_SLOTn names are one bit off). */
static int slot_number(u32 ap)
{
    for (int i = 0; i < 3; i++) {
        if (ap & (1u << i)) {
            return i + 1;
        }
    }
    return 0;
}

void net_describe(char *out, size_t len)
{
    if (len == 0) {
        return;
    }
    out[0] = '\0';

    char wifi[96];
    u32 ap = 0, status = 0;
    Result r = ac_open() ? ACU_GetWifiStatus(&ap) : ac_init_result;
    Result rs = ac_ok ? ACU_GetStatus(&status) : ac_init_result;
    if (R_FAILED(r)) {
        snprintf(wifi, sizeof wifi, "Wi-Fi state unknown (ac 0x%08lX)", (unsigned long)r);
    } else if (ap == 0) {
        snprintf(wifi, sizeof wifi, "Wi-Fi not connected (ac status %lu)",
                 R_SUCCEEDED(rs) ? (unsigned long)status : 0ul);
    } else {
        char ssid[0x21];
        memset(ssid, 0, sizeof ssid);
        int slot = slot_number(ap);
        if (R_SUCCEEDED(ACU_GetSSID(ssid)) && ssid[0] != '\0') {
            ssid[0x20] = '\0';
            snprintf(wifi, sizeof wifi, "Wi-Fi on, \"%s\" (slot %d)", ssid, slot);
        } else {
            snprintf(wifi, sizeof wifi, "Wi-Fi on (slot %d)", slot);
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
        snprintf(out, len, "%s, no IP address", wifi);
    } else {
        snprintf(out, len, "%s, 3DS IP %s", wifi, inet_ntoa(ip));
    }
}

/* AC:u GetConnectResult (command 0x5): the outcome of ACU_ConnectAsync.
 * libctru has no wrapper for it. */
static Result acu_get_connect_result(void)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x5, 0, 2);
    cmdbuf[1] = IPC_Desc_CurProcessId();
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    if (R_FAILED(ret)) {
        return ret;
    }
    return (Result)cmdbuf[1];
}

static void join_failed(char *msg, size_t len, const char *what, Result r)
{
    u32 code = 0, detail = 0;
    (void)ACU_GetLastErrorCode(&code);
    (void)ACU_GetLastDetailErrorCode(&detail);
    snprintf(msg, len, "%s (0x%08lX, ac error %lu/%lu)", what, (unsigned long)r,
             (unsigned long)code, (unsigned long)detail);
}

bool net_bring_up(char *msg, size_t len)
{
    if (len == 0) {
        return false;
    }
    msg[0] = '\0';
    if (!ac_open()) {
        snprintf(msg, len, "Wi-Fi service unavailable (ac 0x%08lX)",
                 (unsigned long)ac_init_result);
        return false;
    }

    u32 ap = 0;
    if (join_event == 0 && R_SUCCEEDED(ACU_GetWifiStatus(&ap)) && ap != 0) {
        return true; /* already on Wi-Fi */
    }

    if (join_event == 0) {
        acuConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        Result r = ACU_CreateDefaultConfig(&cfg);
        if (R_SUCCEEDED(r)) r = ACU_SetNetworkArea(&cfg, 2);
        if (R_SUCCEEDED(r)) r = ACU_SetAllowApType(&cfg, 0xFF); /* any saved slot */
        if (R_SUCCEEDED(r)) r = ACU_SetRequestEulaVersion(&cfg);
        if (R_SUCCEEDED(r)) r = svcCreateEvent(&join_event, RESET_ONESHOT);
        if (R_FAILED(r)) {
            join_event = 0;
            join_failed(msg, len, "Could not ask the 3DS to join Wi-Fi", r);
            return false;
        }
        r = ACU_ConnectAsync(&cfg, join_event);
        if (R_FAILED(r)) {
            svcCloseHandle(join_event);
            join_event = 0;
            join_failed(msg, len, "3DS refused the Wi-Fi join request", r);
            return false;
        }
    }

    Result w = svcWaitSynchronization(join_event, (s64)NET_JOIN_TIMEOUT_MS * 1000000LL);
    if (R_DESCRIPTION(w) == RD_TIMEOUT) {
        /* Keep the request; the next CONNECT waits on it again. */
        snprintf(msg, len, "3DS still joining Wi-Fi after %u s; tap CONNECT to keep waiting",
                 NET_JOIN_TIMEOUT_MS / 1000u);
        return false;
    }
    svcCloseHandle(join_event);
    join_event = 0;
    if (R_FAILED(w)) {
        join_failed(msg, len, "Waiting for Wi-Fi failed", w);
        return false;
    }

    Result cr = acu_get_connect_result();
    if (R_FAILED(cr)) {
        join_failed(msg, len, "3DS could not join Wi-Fi", cr);
        return false;
    }
    snprintf(msg, len, "Asked the 3DS to join Wi-Fi: joined");
    return true;
}
