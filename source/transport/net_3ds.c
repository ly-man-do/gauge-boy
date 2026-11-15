/* libctru soc / ac service setup. 3DS build only (not part of the host tests). */
#include "net.h"

#include <3ds.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SOC_ALIGN      0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *soc_buffer;
static bool ac_ok;          /* ac session open; kept open so a join we asked for lasts */
static Result ac_init_result;
static Handle join_event;   /* pending ACU_ConnectAsync, 0 if none */
static int wifi_slot;       /* 0 = any slot, 1-3 = only that one */

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

/* True if the 3DS lets apps open sockets, i.e. it is on a network
 * (socket() fails with ENETDOWN otherwise). This also covers a network
 * the system joined itself, which ac does not report to this app. */
static bool sockets_up(void)
{
    if (soc_buffer == NULL) {
        return false;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
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
        /* 0xE0A09D2E: this app hasn't joined; the system may have. */
        if (sockets_up()) {
            char ssid[0x21];
            memset(ssid, 0, sizeof ssid);
            if (R_SUCCEEDED(ACU_GetSSID(ssid)) && ssid[0] != '\0') {
                ssid[0x20] = '\0';
                snprintf(wifi, sizeof wifi, "Wi-Fi on, \"%s\" (joined by the system)", ssid);
            } else {
                snprintf(wifi, sizeof wifi, "Wi-Fi on (joined by the system)");
            }
        } else {
            snprintf(wifi, sizeof wifi, "Wi-Fi not connected (ac 0x%08lX)", (unsigned long)r);
        }
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

/* AC:u CloseAsync (0x8) / GetCloseResult (0x9): leave the network this
 * app joined. No libctru wrappers. */
static Result acu_close_async(Handle event)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x8, 0, 4);
    cmdbuf[1] = IPC_Desc_CurProcessId();
    cmdbuf[2] = 0;
    cmdbuf[3] = IPC_Desc_SharedHandles(1);
    cmdbuf[4] = event;
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    if (R_FAILED(ret)) {
        return ret;
    }
    return (Result)cmdbuf[1];
}

static Result acu_get_close_result(void)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x9, 0, 2);
    cmdbuf[1] = IPC_Desc_CurProcessId();
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    if (R_FAILED(ret)) {
        return ret;
    }
    return (Result)cmdbuf[1];
}

void net_set_wifi_slot(int slot)
{
    wifi_slot = slot >= 1 && slot <= 3 ? slot : 0;
}

bool net_slot_name(int slot, char *out, size_t len)
{
    if (len == 0) {
        return false;
    }
    snprintf(out, len, "(not set up)");
    if (slot < 1 || slot > 3 || !ac_open()) {
        return false;
    }
    char ssid[0x21];
    memset(ssid, 0, sizeof ssid);
    if (R_FAILED(ACI_LoadNetworkSetting((u32)(slot - 1))) ||
        R_FAILED(ACI_GetNetworkWirelessEssidSecuritySsid(ssid)) || ssid[0] == '\0') {
        return false;
    }
    ssid[0x20] = '\0';
    snprintf(out, len, "%s", ssid);
    return true;
}

/* ac:i getters for the loaded slot (after ACI_LoadNetworkSetting). Command
 * IDs from zaksabeast/3ds-Ghidra-Scripts ctr_services.py; the buffer
 * layout follows libctru's ACI_GetNetworkWirelessEssidSecuritySsid. */
static Result aci_get_word(u16 cmd, u32 *out)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(cmd, 0, 0);
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    if (R_FAILED(ret)) {
        return ret;
    }
    *out = cmdbuf[2];
    return (Result)cmdbuf[1];
}

static Result aci_get_buffer(u16 cmd, void *buf, size_t size)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    u32 *staticbufs = getThreadStaticBuffers();
    u32 backup[2];
    memcpy(backup, staticbufs, sizeof backup);
    cmdbuf[0] = IPC_MakeHeader(cmd, 0, 0);
    staticbufs[0] = IPC_Desc_StaticBuffer(size, 0);
    staticbufs[1] = (u32)buf;
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    memcpy(staticbufs, backup, sizeof backup);
    return R_SUCCEEDED(ret) ? (Result)cmdbuf[1] : ret;
}

#define ACI_GET_DHCP        0x447
#define ACI_GET_AUTO_DNS    0x449
#define ACI_GET_IP          0x44B
#define ACI_GET_GATEWAY     0x44D
#define ACI_GET_NETMASK     0x44F
#define ACI_GET_DNS1        0x451
#define ACI_GET_DNS2        0x453
#define ACI_GET_MTU         0x46D

static void ip_text(char *out, size_t len, const u8 b[4])
{
    snprintf(out, len, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

bool net_slot_read(int slot, net_ip_settings_t *out, char *err, size_t err_len)
{
    memset(out, 0, sizeof *out);
    if (err_len > 0) {
        err[0] = '\0';
    }
    if (slot < 1 || slot > 3 || !ac_open()) {
        snprintf(err, err_len, "Wi-Fi service unavailable");
        return false;
    }
    Result r = ACI_LoadNetworkSetting((u32)(slot - 1));
    if (R_FAILED(r)) {
        snprintf(err, err_len, "can't load slot %d (0x%08lX)", slot, (unsigned long)r);
        return false;
    }
    u32 dhcp = 0, auto_dns = 0;
    Result failed = 0;
#define GET(call) do { Result rr = (call); if (R_FAILED(rr) && failed == 0) failed = rr; } while (0)
    GET(aci_get_word(ACI_GET_DHCP, &dhcp));
    GET(aci_get_word(ACI_GET_AUTO_DNS, &auto_dns));
    GET(aci_get_buffer(ACI_GET_IP, out->ip, 4));
    GET(aci_get_buffer(ACI_GET_NETMASK, out->mask, 4));
    GET(aci_get_buffer(ACI_GET_GATEWAY, out->gateway, 4));
    GET(aci_get_buffer(ACI_GET_DNS1, out->dns1, 4));
    GET(aci_get_buffer(ACI_GET_DNS2, out->dns2, 4));
#undef GET
    /* Flags are the low byte of the reply word; the rest is left over
     * (seen on hardware: 0x00080000 = manual, 0x00010501 = auto). */
    out->dhcp = (dhcp & 0xFF) != 0;
    out->auto_dns = (auto_dns & 0xFF) != 0;
    if (failed != 0) {
        snprintf(err, err_len, "reading slot %d failed (0x%08lX)", slot, (unsigned long)failed);
        return false;
    }
    return true;
}

bool net_slot_ip_settings(int slot, char *out, size_t len)
{
    if (len == 0) {
        return false;
    }
    net_ip_settings_t st;
    char err[64];
    if (!net_slot_read(slot, &st, err, sizeof err)) {
        snprintf(out, len, "%s", err);
        return false;
    }
    u32 mtu = 0;
    (void)aci_get_word(ACI_GET_MTU, &mtu); /* still loaded by net_slot_read */
    char a[16], m[16], g[16], d1[16], d2[16];
    ip_text(a, sizeof a, st.ip);
    ip_text(m, sizeof m, st.mask);
    ip_text(g, sizeof g, st.gateway);
    ip_text(d1, sizeof d1, st.dns1);
    ip_text(d2, sizeof d2, st.dns2);
    snprintf(out, len, "IP %s %s mask %s gw %s, DNS %s %s / %s, MTU %lu",
             st.dhcp ? "auto" : "manual", a, m, g, st.auto_dns ? "auto" : "manual", d1, d2,
             (unsigned long)(mtu & 0xFFFF));
    return true;
}

/* ac:i setters for the loaded slot, as 3dsDS (MIT) uses them for the SSID
 * and password: a word, or a static buffer. */
static Result aci_set_word(u16 cmd, u32 value)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(cmd, 1, 0);
    cmdbuf[1] = value;
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    return R_FAILED(ret) ? ret : (Result)cmdbuf[1];
}

/* How an address is handed to the ac:i setters is not documented to us.
 * On hardware a 4-byte static buffer (ids 0-2) and a 4-byte mapped buffer
 * were all refused with 0xD9001830, which suggests the service checks
 * the buffer descriptor word exactly. So a grid of descriptor kinds, ids
 * and sizes (and a plain word, both byte orders) is tried, each verified
 * by reading the IP back. Refused tries change nothing; a try that is
 * accepted but stores something else is caught by the read-back, and
 * net_slot_write saves nothing unless the whole slot matches the plan. */
typedef struct {
    u8 kind;   /* 0 static buffer, 1 mapped buffer (read), 2 word LE, 3 word BE */
    u8 id;     /* static buffer id */
    u8 size;   /* bytes described */
} addr_mode_t;

static addr_mode_t addr_mode;
static bool addr_mode_known;

static void addr_mode_name(const addr_mode_t *m, char *out, size_t len)
{
    switch (m->kind) {
    case 0:  snprintf(out, len, "static buffer id %u size %u", m->id, m->size); break;
    case 1:  snprintf(out, len, "mapped buffer size %u", m->size); break;
    case 2:  snprintf(out, len, "word (little-endian)"); break;
    default: snprintf(out, len, "word (big-endian)"); break;
    }
}

static Result aci_set_addr(u16 cmd, const u8 addr[4], const addr_mode_t *m)
{
    static u8 buf[32];
    memset(buf, 0, sizeof buf);
    memcpy(buf, addr, 4);
    u32 *cmdbuf = getThreadCommandBuffer();
    if (m->kind >= 2) {
        u32 v;
        if (m->kind == 2) {
            memcpy(&v, addr, 4);
        } else {
            v = ((u32)addr[0] << 24) | ((u32)addr[1] << 16) | ((u32)addr[2] << 8) | addr[3];
        }
        cmdbuf[0] = IPC_MakeHeader(cmd, 1, 0);
        cmdbuf[1] = v;
    } else {
        cmdbuf[0] = IPC_MakeHeader(cmd, 0, 2);
        cmdbuf[1] = m->kind == 0 ? IPC_Desc_StaticBuffer(m->size, m->id)
                                 : IPC_Desc_Buffer(m->size, IPC_BUFFER_R);
        cmdbuf[2] = (u32)buf;
    }
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    return R_FAILED(ret) ? ret : (Result)cmdbuf[1];
}

static Result aci_get_buffer(u16 cmd, void *buf, size_t size);
#define ACI_GET_IP_FOR_PROBE 0x44B

/* Writes test IP 10.11.12.13 with each mode until one reads back. tried
 * gets a summary of the refusals for the log. */
static Result detect_addr_mode(u16 set_ip_cmd, char *tried, size_t tried_len)
{
    static const u8 probe[4] = { 10, 11, 12, 13 };
    static const u8 sizes[] = { 4, 8, 16, 32 };
    addr_mode_t modes[48];
    int n = 0;
    for (unsigned si = 0; si < sizeof sizes; si++) {
        for (u8 id = 0; id < 8; id++) {
            modes[n++] = (addr_mode_t){ 0, id, sizes[si] };
        }
    }
    modes[n++] = (addr_mode_t){ 1, 0, 4 };
    modes[n++] = (addr_mode_t){ 1, 0, 16 };
    modes[n++] = (addr_mode_t){ 2, 0, 4 };
    modes[n++] = (addr_mode_t){ 3, 0, 4 };

    Result last = -1, first_err = 0;
    int refused = 0, wrong = 0;
    for (int i = 0; i < n; i++) {
        Result r = aci_set_addr(set_ip_cmd, probe, &modes[i]);
        if (R_FAILED(r)) {
            refused++;
            if (first_err == 0) {
                first_err = r;
            } else if (r != first_err && strlen(tried) + 48 < tried_len) {
                char name[40];
                addr_mode_name(&modes[i], name, sizeof name);
                size_t used = strlen(tried);
                snprintf(tried + used, tried_len - used, "%s%s 0x%08lX", used ? ", " : "",
                         name, (unsigned long)r);
            }
            last = r;
            continue;
        }
        u8 back[4] = { 0, 0, 0, 0 };
        Result g = aci_get_buffer(ACI_GET_IP_FOR_PROBE, back, 4);
        if (R_SUCCEEDED(g) && memcmp(back, probe, 4) == 0) {
            addr_mode = modes[i];
            addr_mode_known = true;
            return 0;
        }
        wrong++;
        if (strlen(tried) + 64 < tried_len) {
            char name[40];
            addr_mode_name(&modes[i], name, sizeof name);
            size_t used = strlen(tried);
            snprintf(tried + used, tried_len - used, "%s%s accepted but stored %u.%u.%u.%u",
                     used ? ", " : "", name, back[0], back[1], back[2], back[3]);
        }
        last = -1;
    }
    size_t used = strlen(tried);
    snprintf(tried + used, tried_len - used, "%s%d of %d refused (first 0x%08lX), %d stored wrong",
             used ? "; " : "", refused, n, (unsigned long)first_err, wrong);
    return last;
}

static Result aci_set_buffer(u16 cmd, const u8 addr[4])
{
    return aci_set_addr(cmd, addr, &addr_mode);
}

static Result aci_empty(u16 cmd)
{
    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(cmd, 0, 0);
    Result ret = svcSendSyncRequest(*acGetSessionHandle());
    return R_FAILED(ret) ? ret : (Result)cmdbuf[1];
}

#define ACI_UPDATE_SETTING  0x402
#define ACI_FLUSH_SETTING   0x404
#define ACI_SET_DHCP        0x448
#define ACI_SET_AUTO_DNS    0x44A
#define ACI_SET_IP          0x44C
#define ACI_SET_GATEWAY     0x44E
#define ACI_SET_NETMASK     0x450
#define ACI_SET_DNS1        0x452
#define ACI_SET_DNS2        0x454

bool net_slot_write(int slot, const net_ip_settings_t *in, char *err, size_t err_len)
{
    if (err_len > 0) {
        err[0] = '\0';
    }
    if (slot < 1 || slot > 3 || !ac_open()) {
        snprintf(err, err_len, "Wi-Fi service unavailable");
        return false;
    }
    /* Static buffers must stay valid during the request: copy the inputs. */
    static u8 ip[4], mask[4], gw[4], dns1[4], dns2[4];
    memcpy(ip, in->ip, 4);
    memcpy(mask, in->mask, 4);
    memcpy(gw, in->gateway, 4);
    memcpy(dns1, in->dns1, 4);
    memcpy(dns2, in->dns2, 4);

    char tried[200] = "";
    const char *step = "load";
    Result r = ACI_LoadNetworkSetting((u32)(slot - 1));
#define STEP(name, call) do { if (R_SUCCEEDED(r)) { step = (name); r = (call); } } while (0)
    STEP("IP auto-obtain", aci_set_word(ACI_SET_DHCP, in->dhcp ? 1 : 0));
    STEP("DNS auto-obtain", aci_set_word(ACI_SET_AUTO_DNS, in->auto_dns ? 1 : 0));
    if (!addr_mode_known) {
        STEP("finding how to pass addresses", detect_addr_mode(ACI_SET_IP, tried, sizeof tried));
    }
    STEP("IP address", aci_set_buffer(ACI_SET_IP, ip));
    STEP("subnet mask", aci_set_buffer(ACI_SET_NETMASK, mask));
    STEP("gateway", aci_set_buffer(ACI_SET_GATEWAY, gw));
    STEP("primary DNS", aci_set_buffer(ACI_SET_DNS1, dns1));
    STEP("secondary DNS", aci_set_buffer(ACI_SET_DNS2, dns2));
    if (R_FAILED(r)) {
        /* Drop the half-edited copy so nothing else picks it up. */
        (void)ACI_LoadNetworkSetting((u32)(slot - 1));
        snprintf(err, err_len, "%s failed (0x%08lX; tried %s); nothing saved", step,
                 (unsigned long)r, tried[0] ? tried : "-");
        return false;
    }

    /* Check the edited copy holds exactly what was asked before saving it:
     * a wrong way of passing the addresses could be accepted but store
     * something else. */
    u32 dhcp = 0, auto_dns = 0;
    u8 got[5][4];
    memset(got, 0, sizeof got);
    Result rr = aci_get_word(ACI_GET_DHCP, &dhcp);
    if (R_SUCCEEDED(rr)) rr = aci_get_word(ACI_GET_AUTO_DNS, &auto_dns);
    if (R_SUCCEEDED(rr)) rr = aci_get_buffer(ACI_GET_IP, got[0], 4);
    if (R_SUCCEEDED(rr)) rr = aci_get_buffer(ACI_GET_NETMASK, got[1], 4);
    if (R_SUCCEEDED(rr)) rr = aci_get_buffer(ACI_GET_GATEWAY, got[2], 4);
    if (R_SUCCEEDED(rr)) rr = aci_get_buffer(ACI_GET_DNS1, got[3], 4);
    if (R_SUCCEEDED(rr)) rr = aci_get_buffer(ACI_GET_DNS2, got[4], 4);
    bool same = R_SUCCEEDED(rr) && ((dhcp & 0xFF) != 0) == in->dhcp &&
                ((auto_dns & 0xFF) != 0) == in->auto_dns &&
                memcmp(got[0], in->ip, 4) == 0 && memcmp(got[1], in->mask, 4) == 0 &&
                memcmp(got[2], in->gateway, 4) == 0 && memcmp(got[3], in->dns1, 4) == 0 &&
                memcmp(got[4], in->dns2, 4) == 0;
    if (!same) {
        (void)ACI_LoadNetworkSetting((u32)(slot - 1));
        addr_mode_known = false;
        snprintf(err, err_len, "check before saving found IP %u.%u.%u.%u DNS %u.%u.%u.%u "
                 "(read 0x%08lX); nothing saved",
                 got[0][0], got[0][1], got[0][2], got[0][3], got[3][0], got[3][1], got[3][2],
                 got[3][3], (unsigned long)rr);
        return false;
    }

    STEP("update slot", aci_set_word(ACI_UPDATE_SETTING, (u32)(slot - 1)));
    STEP("save settings", aci_empty(ACI_FLUSH_SETTING));
#undef STEP
    if (R_FAILED(r)) {
        (void)ACI_LoadNetworkSetting((u32)(slot - 1));
        snprintf(err, err_len, "%s failed (0x%08lX); not saved", step, (unsigned long)r);
        return false;
    }
    /* On success err says how the addresses were passed (for the log). */
    char how[40];
    addr_mode_name(&addr_mode, how, sizeof how);
    snprintf(err, err_len, "addresses passed as %s%s%s", how, tried[0] ? "; " : "", tried);
    return true;
}

static void join_failed(char *msg, size_t len, const char *what, Result r)
{
    u32 code = 0, detail = 0;
    (void)ACU_GetLastErrorCode(&code);
    (void)ACU_GetLastDetailErrorCode(&detail);
    size_t used = strlen(msg);
    snprintf(msg + used, len - used, "%s (0x%08lX, ac error %lu/%lu)", what, (unsigned long)r,
             (unsigned long)code, (unsigned long)detail);
}

static void append(char *msg, size_t len, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void append(char *msg, size_t len, const char *fmt, ...)
{
    size_t used = strlen(msg);
    if (used >= len) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg + used, len - used, fmt, ap);
    va_end(ap);
}

/* Leave the network this app joined (another slot). Waits up to 10 s. */
static bool leave_network(char *msg, size_t len)
{
    Handle ev = 0;
    Result r = svcCreateEvent(&ev, RESET_ONESHOT);
    if (R_SUCCEEDED(r)) {
        r = acu_close_async(ev);
    }
    if (R_SUCCEEDED(r)) {
        Result w = svcWaitSynchronization(ev, 10000000000LL);
        r = R_FAILED(w) ? w : acu_get_close_result();
    }
    if (ev != 0) {
        svcCloseHandle(ev);
    }
    if (R_FAILED(r)) {
        join_failed(msg, len, "could not leave it", r);
        append(msg, len, "; ");
        return false;
    }
    append(msg, len, "left it; ");
    return true;
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

    if (join_event == 0) {
        u32 ap = 0;
        bool ours = R_SUCCEEDED(ACU_GetWifiStatus(&ap)) && ap != 0; /* joined by this app */
        bool up = ours || sockets_up();

        if (wifi_slot == 0) {
            /* Any slot: already on a network (ours or the system's) is
             * fine; asking again would make the 3DS rejoin and drop open
             * connections. */
            if (up) {
                return true;
            }
        } else {
            char want[0x21];
            net_slot_name(wifi_slot, want, sizeof want);
            if (ours && slot_number(ap) == wifi_slot) {
                return true;
            }
            if (ours) {
                append(msg, len, "On slot %d, not slot %d \"%s\": ", slot_number(ap), wifi_slot, want);
                leave_network(msg, len);
            } else if (up) {
                /* Joined by the system: check the network name if the 3DS
                 * tells us; otherwise rejoin the chosen slot to be sure. */
                char ssid[0x21];
                memset(ssid, 0, sizeof ssid);
                if (R_SUCCEEDED(ACU_GetSSID(ssid)) && ssid[0] != '\0') {
                    ssid[0x20] = '\0';
                    if (strcmp(ssid, want) == 0) {
                        return true;
                    }
                    append(msg, len, "On \"%s\", not slot %d \"%s\": ", ssid, wifi_slot, want);
                } else {
                    append(msg, len, "On a network the system joined (name unknown); ");
                }
            }
        }

        acuConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        u8 allow = wifi_slot == 0 ? 0xFF : (u8)(1u << (wifi_slot - 1)); /* bit 0 = slot 1 */
        Result r = ACU_CreateDefaultConfig(&cfg);
        if (R_SUCCEEDED(r)) r = ACU_SetNetworkArea(&cfg, 2);
        if (R_SUCCEEDED(r)) r = ACU_SetAllowApType(&cfg, allow);
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
        append(msg, len, "3DS still joining Wi-Fi after %u s; tap CONNECT to keep waiting",
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
    if (wifi_slot == 0) {
        append(msg, len, "Asked the 3DS to join Wi-Fi: joined");
    } else {
        append(msg, len, "Asked the 3DS to join slot %d: joined", wifi_slot);
    }
    return true;
}
