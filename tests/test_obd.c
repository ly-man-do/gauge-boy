/* OBD-II reply assembly for every protocol family (source/obd/obd_reply). */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "obd/obd_codes.h"
#include "obd/obd_readiness.h"
#include "obd/obd_vin.h"
#include "obd/dtc.h"
#include "obd/obd_reply.h"

static obd_reply_t reply; /* large: keep off the stack */
static elm_response_t resp;

/* Copies up to ELM_MAX_LINES lines (NULL-terminated list) into resp. */
static void set_lines(const char *const *src)
{
    int n = 0;
    memset(&resp, 0, sizeof resp);
    for (; src[n] != NULL && n < ELM_MAX_LINES; n++) {
        snprintf(resp.lines[n], ELM_LINE_MAX, "%s", src[n]);
    }
    resp.line_count = n;
}

static obd_status_t run(const char *const *src, int protocol, bool headers)
{
    set_lines(src);
    return obd_assemble(&resp, protocol, headers, &reply);
}

static void check_msg(int e, int m, const uint8_t *want, int n, int line)
{
    if (e >= reply.ecu_count || m >= reply.ecu[e].msg_count) {
        gb_check_failf(__FILE__, line, "no message %d on ECU %d", m, e);
        return;
    }
    const obd_msg_t *msg = &reply.ecu[e].msg[m];
    if (msg->len != n || memcmp(msg->data, want, (size_t)n) != 0) {
        char got[3 * OBD_MSG_MAX + 1] = "";
        for (int i = 0; i < msg->len && i < 64; i++) {
            snprintf(got + strlen(got), sizeof got - strlen(got), "%02X ", msg->data[i]);
        }
        gb_check_failf(__FILE__, line, "ECU %d message %d: got %d bytes [%s], want %d", e, m,
                       msg->len, got, n);
    }
}
#define CHECK_MSG(e, m, ...)                                                   \
    do {                                                                       \
        static const uint8_t want_[] = { __VA_ARGS__ };                        \
        check_msg((e), (m), want_, (int)sizeof want_, __LINE__);               \
    } while (0)

/* The ELM327 datasheet's example VIN, 1D4GP00R55B123456. */
#define VIN_BYTES 0x31, 0x44, 0x34, 0x47, 0x50, 0x30, 0x30, 0x52, 0x35, 0x35, \
                  0x42, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36

static void test_family(void)
{
    CHECK_EQ_INT(obd_family(0), OBD_FAMILY_UNKNOWN);
    CHECK_EQ_INT(obd_family(1), OBD_FAMILY_LEGACY);  /* J1850 PWM */
    CHECK_EQ_INT(obd_family(3), OBD_FAMILY_LEGACY);  /* ISO 9141-2 */
    CHECK_EQ_INT(obd_family(5), OBD_FAMILY_LEGACY);  /* KWP fast init */
    CHECK_EQ_INT(obd_family(6), OBD_FAMILY_CAN);
    CHECK_EQ_INT(obd_family(10), OBD_FAMILY_CAN);    /* J1939 */
    CHECK_EQ_INT(obd_family(12), OBD_FAMILY_CAN);    /* USER2 */
    CHECK_EQ_INT(obd_family(13), OBD_FAMILY_UNKNOWN);
}

/* ---------- legacy (J1850 / ISO 9141-2 / KWP) ---------- */

static void test_legacy_headers_off_ridgeline(void)
{
    /* Real lines from the 2008 Ridgeline on ISO 9141-2 (VIN bytes made up:
     * "TESTVIN0123456789"). */
    static const char *const pids[] = { "41 00 BF BE A8 91", NULL };
    CHECK_EQ_INT(run(pids, 3, false), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 1);
    CHECK(!reply.ecu[0].has_id);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBF, 0xBE, 0xA8, 0x91);

    static const char *const vin[] = {
        "49 02 01 00 00 00 54", "49 02 02 45 53 54 56", "49 02 03 49 4E 30 31",
        "49 02 04 32 33 34 35", "49 02 05 36 37 38 39", NULL,
    };
    CHECK_EQ_INT(run(vin, 3, false), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 1);
    CHECK_EQ_INT(reply.ecu[0].msg_count, 5);
    CHECK_MSG(0, 0, 0x49, 0x02, 0x01, 0x00, 0x00, 0x00, 0x54);
    CHECK_MSG(0, 4, 0x49, 0x02, 0x05, 0x36, 0x37, 0x38, 0x39);
    CHECK_EQ_INT(reply.bad_lines, 0);
}

/* Appends the ISO 9141 / KWP sum check to a hex line. */
static void with_check(char *out, size_t len, const char *hex)
{
    unsigned sum = 0, v;
    const char *p = hex;
    int used = 0;
    while (sscanf(p, "%2x%n", &v, &used) == 1) {
        sum += v;
        p += used;
        while (*p == ' ') {
            p++;
        }
    }
    snprintf(out, len, "%s %02X", hex, sum & 0xFF);
}

static void test_legacy_headers_on(void)
{
    char a[ELM_LINE_MAX], b[ELM_LINE_MAX];
    /* ISO 9141-2: two ECUs (engine 0x10, transmission 0x18). */
    with_check(a, sizeof a, "48 6B 10 41 00 BE 3E B8 11");
    with_check(b, sizeof b, "48 6B 18 41 00 80 00 00 01");
    const char *const two[] = { a, b, NULL };
    CHECK_EQ_INT(run(two, 3, true), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 2);
    CHECK(reply.ecu[0].has_id && reply.ecu[0].id == 0x10);
    CHECK(reply.ecu[1].has_id && reply.ecu[1].id == 0x18);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11);
    CHECK_MSG(1, 0, 0x41, 0x00, 0x80, 0x00, 0x00, 0x01);

    /* A wrong check byte is rejected. */
    char bad[ELM_LINE_MAX];
    snprintf(bad, sizeof bad, "48 6B 10 41 00 BE 3E B8 11 00");
    const char *const wrong[] = { bad, NULL };
    CHECK_EQ_INT(run(wrong, 3, true), OBD_ERR_NO_DATA);
    CHECK_EQ_INT(reply.bad_lines, 1);

    /* KWP with the length in the format byte (0x86 = 6 data bytes). */
    with_check(a, sizeof a, "86 F1 10 41 00 BE 3E B8 11");
    const char *const kwp[] = { a, NULL };
    CHECK_EQ_INT(run(kwp, 5, true), OBD_OK);
    CHECK_EQ_INT(reply.ecu[0].id, 0x10);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11);

    /* KWP with a separate length byte (format 0x80). */
    with_check(a, sizeof a, "80 F1 10 06 41 00 BE 3E B8 11");
    const char *const kwp4[] = { a, NULL };
    CHECK_EQ_INT(run(kwp4, 4, true), OBD_OK);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11);

    /* J1850 VPW: the trailing CRC is dropped, not checked. */
    const char *const vpw[] = { "48 6B 10 41 00 BE 3E B8 11 C4", NULL };
    CHECK_EQ_INT(run(vpw, 2, true), OBD_OK);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11);
}

/* ---------- CAN ---------- */

static void test_can_headers_off(void)
{
    /* Single frames from two ECUs (headers off: each its own entry). */
    static const char *const sf[] = { "41 00 BE 3E B8 11", "41 00 98 18 80 11", NULL };
    CHECK_EQ_INT(run(sf, 6, false), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 2);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11);
    CHECK_MSG(1, 0, 0x41, 0x00, 0x98, 0x18, 0x80, 0x11);

    /* Multi-frame VIN (ELM327 datasheet layout). 0x14 = 20 bytes. */
    static const char *const vin[] = {
        "014", "0: 49 02 01 31 44 34", "1: 47 50 30 30 52 35 35", "2: 42 31 32 33 34 35 36", NULL,
    };
    CHECK_EQ_INT(run(vin, 6, false), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 1);
    CHECK_MSG(0, 0, 0x49, 0x02, 0x01, VIN_BYTES);
    CHECK_EQ_INT(reply.bad_lines, 0);

    /* Spaces off. */
    static const char *const nospace[] = {
        "014", "0:490201314434", "1:47503030523535", "2:42313233343536", NULL,
    };
    CHECK_EQ_INT(run(nospace, 6, false), OBD_OK);
    CHECK_MSG(0, 0, 0x49, 0x02, 0x01, VIN_BYTES);

    /* Mode 04 positive reply is a single byte. */
    static const char *const clear[] = { "44", NULL };
    CHECK_EQ_INT(run(clear, 6, false), OBD_OK);
    CHECK_MSG(0, 0, 0x44);
}

static void test_can_headers_on(void)
{
    /* 11-bit, two ECUs. */
    static const char *const two[] = { "7E8 06 41 00 BE 3E B8 11", "7E9 06 41 00 98 18 80 11", NULL };
    CHECK_EQ_INT(run(two, 6, true), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 2);
    CHECK(reply.ecu[0].id == 0x7E8 && reply.ecu[1].id == 0x7E9);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11);
    CHECK_MSG(1, 0, 0x41, 0x00, 0x98, 0x18, 0x80, 0x11);

    /* 11-bit multi-frame VIN, with another ECU's frame in between. */
    static const char *const vin[] = {
        "7E8 10 14 49 02 01 31 44 34", "7E9 03 7F 09 11",
        "7E8 21 47 50 30 30 52 35 35", "7E8 22 42 31 32 33 34 35 36", NULL,
    };
    CHECK_EQ_INT(run(vin, 6, true), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 2);
    CHECK_MSG(0, 0, 0x49, 0x02, 0x01, VIN_BYTES);
    CHECK_MSG(1, 0, 0x7F, 0x09, 0x11);   /* negative response: kept for the decoder */

    /* 29-bit IDs, spaces on and off. */
    static const char *const ext[] = {
        "18 DA F1 10 10 14 49 02 01 31 44 34", "18 DA F1 10 21 47 50 30 30 52 35 35",
        "18DAF1102242313233343536", NULL,
    };
    CHECK_EQ_INT(run(ext, 7, true), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 1);
    CHECK(reply.ecu[0].id == 0x18DAF110);
    CHECK_MSG(0, 0, 0x49, 0x02, 0x01, VIN_BYTES);

    /* 11-bit without spaces. */
    static const char *const nospace[] = { "7E8064100BE3EB811", NULL };
    CHECK_EQ_INT(run(nospace, 6, true), OBD_OK);
    CHECK(reply.ecu[0].id == 0x7E8);
    CHECK_MSG(0, 0, 0x41, 0x00, 0xBE, 0x3E, 0xB8, 0x11);

    /* Flow control frames are skipped quietly. */
    static const char *const fc[] = { "7E0 30 00 00", "7E8 03 41 0D 00", NULL };
    CHECK_EQ_INT(run(fc, 6, true), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, 1);
    CHECK_MSG(0, 0, 0x41, 0x0D, 0x00);
    CHECK_EQ_INT(reply.bad_lines, 0);
}

/* ---------- bad and partial replies (clone adapters misbehave) ---------- */

static void test_bad_and_partial(void)
{
    /* Garbage, odd digit counts, non-hex. */
    static const char *const junk[] = { "41 0", "ZZ TOP", "41 00 BE 3", NULL };
    CHECK_EQ_INT(run(junk, 6, false), OBD_ERR_NO_DATA);
    CHECK_EQ_INT(reply.bad_lines, 3);

    /* Multi-frame cut short: the message is dropped, not returned half. */
    static const char *const cut[] = { "014", "0: 49 02 01 31 44 34", "1: 47 50 30 30 52 35 35", NULL };
    CHECK_EQ_INT(run(cut, 6, false), OBD_ERR_NO_DATA);
    CHECK(reply.bad_lines >= 1);

    /* Frame out of order. */
    static const char *const order[] = { "014", "0: 49 02 01 31 44 34", "2: 42 31 32 33 34 35 36", NULL };
    CHECK_EQ_INT(run(order, 6, false), OBD_ERR_NO_DATA);

    /* Consecutive frame without a first frame; good frame still kept. */
    static const char *const orphan[] = { "7E8 21 47 50 30", "7E8 03 41 0D 2A", NULL };
    CHECK_EQ_INT(run(orphan, 6, true), OBD_OK);
    CHECK_EQ_INT(reply.bad_lines, 1);
    CHECK_MSG(0, 0, 0x41, 0x0D, 0x2A);

    /* Single-frame length larger than the bytes present. */
    static const char *const shortsf[] = { "7E8 06 41 00 BE", NULL };
    CHECK_EQ_INT(run(shortsf, 6, true), OBD_ERR_NO_DATA);

    /* Unknown protocol and bad arguments. */
    resp.line_count = 0;
    CHECK_EQ_INT(obd_assemble(&resp, 0, false, &reply), OBD_ERR_INVALID_ARG);
    CHECK_EQ_INT(obd_assemble(&resp, 6, false, NULL), OBD_ERR_INVALID_ARG);
    CHECK_EQ_INT(obd_assemble(NULL, 6, false, &reply), OBD_ERR_INVALID_ARG);
    CHECK_EQ_INT(obd_assemble(&resp, 6, false, &reply), OBD_ERR_NO_DATA);
}

static void test_limits(void)
{
    /* More ECUs than fit: the extra ones count as bad lines. */
    static const char *const many[] = {
        "7E0 02 41 00", "7E1 02 41 00", "7E2 02 41 00", "7E3 02 41 00", "7E4 02 41 00",
        "7E5 02 41 00", "7E6 02 41 00", "7E7 02 41 00", "7E8 02 41 00", NULL,
    };
    CHECK_EQ_INT(run(many, 6, true), OBD_OK);
    CHECK_EQ_INT(reply.ecu_count, OBD_MAX_ECUS);
    CHECK_EQ_INT(reply.bad_lines, 1);

    /* A first frame claiming more than OBD_MSG_MAX bytes is rejected. */
    static const char *const huge[] = { "7E8 1F FF 49 02 01 31 44 34", NULL };
    CHECK_EQ_INT(run(huge, 6, true), OBD_ERR_NO_DATA);
}

/* ---------- trouble codes (Mode 03 / 07 / 0A) ---------- */

static obd_code_list_t codes;

static void decode(const char *const *src, int protocol, bool headers, uint8_t service)
{
    run(src, protocol, headers);
    CHECK(obd_decode_codes(&reply, protocol, service, &codes));
}

#define CHECK_CODES(...)                                                         \
    do {                                                                         \
        static const char *const want_[] = { __VA_ARGS__ };                      \
        int n_ = (int)(sizeof want_ / sizeof want_[0]);                          \
        CHECK_EQ_INT(codes.count, n_);                                           \
        for (int i_ = 0; i_ < n_ && i_ < codes.count; i_++) {                    \
            CHECK_EQ_STR(codes.code[i_].code, want_[i_]);                        \
        }                                                                        \
    } while (0)

static void test_code_from_bytes(void)
{
    char c[6];
    obd_code_from_bytes(0x01, 0x71, c);
    CHECK_EQ_STR(c, "P0171");
    obd_code_from_bytes(0x20, 0x96, c);
    CHECK_EQ_STR(c, "P2096");
    obd_code_from_bytes(0x14, 0x56, c);
    CHECK_EQ_STR(c, "P1456");
    obd_code_from_bytes(0x41, 0x23, c);
    CHECK_EQ_STR(c, "C0123");
    obd_code_from_bytes(0x81, 0x00, c);
    CHECK_EQ_STR(c, "B0100");
    obd_code_from_bytes(0xC1, 0x00, c);
    CHECK_EQ_STR(c, "U0100");
    obd_code_from_bytes(0xFF, 0xFF, c);
    CHECK_EQ_STR(c, "U3FFF");
}

static void test_codes_can(void)
{
    /* Headers off: count byte then codes. */
    static const char *const two[] = { "43 02 01 33 01 34", NULL };
    decode(two, 6, false, OBD_SERVICE_STORED);
    CHECK_CODES("P0133", "P0134");
    CHECK_EQ_INT(codes.ecus_answered, 1);
    CHECK_EQ_INT(codes.bad_messages, 0);

    /* No codes. */
    static const char *const none[] = { "43 00", NULL };
    decode(none, 6, false, OBD_SERVICE_STORED);
    CHECK_EQ_INT(codes.count, 0);
    CHECK_EQ_INT(codes.ecus_answered, 1);

    /* Many codes: multi-frame (5 codes = 12 bytes). */
    static const char *const many[] = {
        "00C", "0: 43 05 01 71 01 72 03", "1: 00 04 20 C1 00 41 23", NULL,
    };
    decode(many, 6, false, OBD_SERVICE_STORED);
    CHECK_CODES("P0171", "P0172", "P0300", "P0420", "U0100");

    /* Headers on: engine and transmission, the same code listed once. */
    static const char *const ecus[] = {
        "7E8 06 47 02 01 71 04 20", "7E9 04 47 01 07 00", "7EA 04 47 01 01 71", NULL,
    };
    decode(ecus, 6, true, OBD_SERVICE_PENDING);
    CHECK_CODES("P0171", "P0420", "P0700");
    CHECK_EQ_INT(codes.ecus_answered, 3);
    CHECK(codes.code[2].has_ecu && codes.code[2].ecu == 0x7E9);

    /* Permanent codes refused (a 2008 car): 7F 0A 11. */
    static const char *const refused[] = { "7E8 03 7F 0A 11", NULL };
    decode(refused, 6, true, OBD_SERVICE_PERMANENT);
    CHECK_EQ_INT(codes.count, 0);
    CHECK_EQ_INT(codes.ecus_answered, 0);
    CHECK_EQ_INT(codes.ecus_refused, 1);

    /* A count larger than the bytes present: keep what arrived. */
    static const char *const cut[] = { "43 03 01 71 01", NULL };
    decode(cut, 6, false, OBD_SERVICE_STORED);
    CHECK_CODES("P0171");
    CHECK_EQ_INT(codes.bad_messages, 1);
}

static void test_codes_legacy(void)
{
    /* ISO 9141-2, headers off: three codes per frame, 00 00 padding. */
    static const char *const one[] = { "43 01 33 00 00 00 00", NULL };
    decode(one, 3, false, OBD_SERVICE_STORED);
    CHECK_CODES("P0133");
    CHECK_EQ_INT(codes.ecus_answered, 1);

    static const char *const four[] = { "43 01 71 01 72 03 00", "43 04 20 00 00 00 00", NULL };
    decode(four, 3, false, OBD_SERVICE_STORED);
    CHECK_CODES("P0171", "P0172", "P0300", "P0420");

    /* A Honda code on the Ridgeline's protocol. */
    static const char *const honda[] = { "47 14 56 00 00 00 00", NULL };
    decode(honda, 3, false, OBD_SERVICE_PENDING);
    CHECK_CODES("P1456");

    /* All padding: no codes. */
    static const char *const empty[] = { "43 00 00 00 00 00 00", NULL };
    decode(empty, 3, false, OBD_SERVICE_STORED);
    CHECK_EQ_INT(codes.count, 0);
    CHECK_EQ_INT(codes.ecus_answered, 1);

    /* Headers on (J1850 VPW), ECU 0x10. */
    static const char *const vpw[] = { "48 6B 10 43 01 33 00 00 00 00 9A", NULL };
    decode(vpw, 2, true, OBD_SERVICE_STORED);
    CHECK_CODES("P0133");
    CHECK(codes.code[0].has_ecu && codes.code[0].ecu == 0x10);
}

static void test_codes_bad(void)
{
    /* NO DATA from the adapter: nothing assembled, empty list. */
    CHECK(obd_decode_codes(NULL, 6, OBD_SERVICE_STORED, &codes));
    CHECK_EQ_INT(codes.count, 0);
    CHECK_EQ_INT(codes.ecus_answered, 0);

    /* Wrong service in the reply (a 41 reply to a 03 request). */
    static const char *const wrong[] = { "41 00 BE 3E B8 11", NULL };
    decode(wrong, 6, false, OBD_SERVICE_STORED);
    CHECK_EQ_INT(codes.count, 0);
    CHECK_EQ_INT(codes.bad_messages, 1);

    /* Bad service number. */
    CHECK(!obd_decode_codes(&reply, 6, 0x01, &codes));
}

/* ---------- Mode 01 PID 01: CEL and readiness ---------- */

static obd_readiness_t rd;

static const obd_monitor_t *monitor(const char *name)
{
    for (int i = 0; i < OBD_MONITOR_COUNT; i++) {
        if (rd.monitor[i].name != NULL && strcmp(rd.monitor[i].name, name) == 0) {
            return &rd.monitor[i];
        }
    }
    return NULL;
}

#define CHECK_MON(name, st)                                                  \
    do {                                                                     \
        const obd_monitor_t *m_ = monitor(name);                             \
        CHECK(m_ != NULL);                                                   \
        if (m_ != NULL) {                                                    \
            CHECK_EQ_STR(obd_monitor_state_str(m_->state), obd_monitor_state_str(st)); \
        }                                                                    \
    } while (0)

static void test_readiness_spark(void)
{
    /* MIL on, 1 code; spark; misfire/fuel/components supported, fuel not
     * complete; catalyst, evap, O2, O2 heater supported; evap not complete. */
    static const uint8_t abcd[4] = { 0x81, 0x27, 0x65, 0x04 };
    obd_readiness_from_bytes(abcd, &rd);
    CHECK(rd.valid && rd.mil_on);
    CHECK_EQ_INT(rd.dtc_count, 1);
    CHECK(!rd.compression);
    CHECK_MON("Misfire", OBD_MONITOR_COMPLETE);
    CHECK_MON("Fuel system", OBD_MONITOR_INCOMPLETE);
    CHECK_MON("Components", OBD_MONITOR_COMPLETE);
    CHECK_MON("Catalyst", OBD_MONITOR_COMPLETE);
    CHECK_MON("Heated catalyst", OBD_MONITOR_NOT_SUPPORTED);
    CHECK_MON("Evaporative system", OBD_MONITOR_INCOMPLETE);
    CHECK_MON("Oxygen sensor", OBD_MONITOR_COMPLETE);
    CHECK_MON("Oxygen sensor heater", OBD_MONITOR_COMPLETE);
    CHECK_MON("EGR / VVT system", OBD_MONITOR_NOT_SUPPORTED);
    CHECK_EQ_INT(obd_readiness_ready_count(&rd), 5);
    CHECK_EQ_INT(obd_readiness_not_ready_count(&rd), 2);

    /* Light off, no codes, everything complete. */
    static const uint8_t clean[4] = { 0x00, 0x07, 0x65, 0x00 };
    obd_readiness_from_bytes(clean, &rd);
    CHECK(!rd.mil_on && rd.dtc_count == 0);
    CHECK_EQ_INT(obd_readiness_not_ready_count(&rd), 0);
}

static void test_readiness_diesel(void)
{
    /* Compression ignition: different names; reserved bits never shown. */
    static const uint8_t abcd[4] = { 0x02, 0x0F, 0xFF, 0x40 };
    obd_readiness_from_bytes(abcd, &rd);
    CHECK(rd.compression && !rd.mil_on && rd.dtc_count == 2);
    CHECK_MON("NMHC catalyst", OBD_MONITOR_COMPLETE);
    CHECK_MON("Particulate filter", OBD_MONITOR_INCOMPLETE);
    CHECK_MON("Boost pressure", OBD_MONITOR_COMPLETE);
    CHECK(monitor("Catalyst") == NULL);
    CHECK_EQ_INT(rd.monitor[3 + 2].state, OBD_MONITOR_NOT_SUPPORTED); /* reserved bit 2 */
    CHECK(rd.monitor[3 + 2].name == NULL);
}

static void test_readiness_from_reply(void)
{
    /* ISO 9141-2 headers off (one ECU). */
    static const char *const one[] = { "41 01 81 07 65 04", NULL };
    run(one, 3, false);
    CHECK(obd_decode_readiness(&reply, &rd));
    CHECK(rd.mil_on && rd.ecus == 1);
    CHECK_MON("Evaporative system", OBD_MONITOR_INCOMPLETE);

    /* CAN headers on: engine (light on, 2 codes) + transmission (1 code). */
    static const char *const two[] = { "7E8 06 41 01 82 07 65 00", "7E9 06 41 01 01 00 00 00", NULL };
    run(two, 6, true);
    CHECK(obd_decode_readiness(&reply, &rd));
    CHECK(rd.mil_on);
    CHECK_EQ_INT(rd.dtc_count, 3);
    CHECK_EQ_INT(rd.ecus, 2);

    /* A second ECU marking a monitor incomplete wins. */
    static const char *const inc[] = { "7E8 06 41 01 00 07 65 00", "7E9 06 41 01 00 07 01 01", NULL };
    run(inc, 6, true);
    CHECK(obd_decode_readiness(&reply, &rd));
    CHECK_MON("Catalyst", OBD_MONITOR_INCOMPLETE);
    CHECK_MON("Oxygen sensor", OBD_MONITOR_COMPLETE);

    /* No usable reply: too short, wrong PID, refusal, NO DATA. */
    static const char *const bad[] = { "41 01 81 07", "41 00 BE 3E B8 11", "7F 01 12", NULL };
    run(bad, 6, false);
    CHECK(!obd_decode_readiness(&reply, &rd));
    CHECK(!obd_decode_readiness(NULL, &rd));
}

/* ---------- VIN (Mode 09 PID 02) ---------- */

/* Made-up VINs with valid check digits. */
#define HONDA_VIN "2HJYK16618H000123"

static void test_vin_decode(void)
{
    char vin[OBD_VIN_LEN + 1];

    /* ISO 9141-2 (the Ridgeline's layout), headers off; frames in any order. */
    static const char *const legacy[] = {
        "49 02 01 00 00 00 32", "49 02 02 48 4A 59 4B", "49 02 04 38 48 30 30",
        "49 02 03 31 36 36 31", "49 02 05 30 31 32 33", NULL,
    };
    run(legacy, 3, false);
    CHECK(obd_decode_vin(&reply, 3, vin));
    CHECK_EQ_STR(vin, HONDA_VIN);

    /* A missing frame: no VIN rather than a wrong one. */
    static const char *const gap[] = {
        "49 02 01 00 00 00 32", "49 02 02 48 4A 59 4B", "49 02 03 31 36 36 31",
        "49 02 05 30 31 32 33", NULL,
    };
    run(gap, 3, false);
    CHECK(!obd_decode_vin(&reply, 3, vin));
    CHECK_EQ_STR(vin, "");

    /* CAN headers off, multi-frame (ELM327 datasheet layout). */
    static const char *const can[] = {
        "014", "0: 49 02 01 32 48 4A", "1: 59 4B 31 36 36 31 38", "2: 48 30 30 30 31 32 33", NULL,
    };
    run(can, 6, false);
    CHECK(obd_decode_vin(&reply, 6, vin));
    CHECK_EQ_STR(vin, HONDA_VIN);

    /* CAN headers on, 29-bit, transmission ECU refuses first. */
    static const char *const can29[] = {
        "18 DA F1 18 03 7F 09 12", "18 DA F1 10 10 14 49 02 01 32 48 4A",
        "18 DA F1 10 21 59 4B 31 36 36 31 38", "18 DA F1 10 22 48 30 30 30 31 32 33", NULL,
    };
    run(can29, 7, true);
    CHECK(obd_decode_vin(&reply, 7, vin));
    CHECK_EQ_STR(vin, HONDA_VIN);

    /* Not a VIN reply. */
    static const char *const other[] = { "41 00 BE 3E B8 11", NULL };
    run(other, 6, false);
    CHECK(!obd_decode_vin(&reply, 6, vin));
    CHECK(!obd_decode_vin(NULL, 6, vin));
}

static void test_vin_facts(void)
{
    CHECK(obd_vin_chars_ok(HONDA_VIN));
    CHECK(obd_vin_check_digit_ok(HONDA_VIN));
    CHECK(obd_vin_check_digit_ok("1M8GDM9AXKP042788"));   /* check digit X */
    CHECK(!obd_vin_check_digit_ok("2HJYK16628H000123"));  /* wrong check digit */
    CHECK(!obd_vin_chars_ok("2HJYK1661OH000123"));        /* letter O not allowed */
    CHECK(!obd_vin_chars_ok("2HJYK16618H00012"));         /* 16 characters */

    /* Model year: 8 = 2008 or 2038; A = 1980 or 2010; K = 1989 or 2019. */
    CHECK_EQ_INT(obd_vin_model_year(HONDA_VIN, 2026), 2008);
    CHECK_EQ_INT(obd_vin_model_year(HONDA_VIN, 0), 2008);
    CHECK_EQ_INT(obd_vin_model_year("5FNYF4H59AB000042", 2026), 2010);
    CHECK_EQ_INT(obd_vin_model_year("5FNYF4H59AB000042", 0), 2010);  /* position 7 is a letter */
    CHECK_EQ_INT(obd_vin_model_year("1M8GDM9AXKP042788", 0), 1989);  /* position 7 is a digit */
    CHECK_EQ_INT(obd_vin_model_year("1M8GDM9AXKP042788", 2026), 2019);
    CHECK_EQ_INT(obd_vin_model_year("bad", 2026), 0);

    /* Maker from the WMI, and the make for manufacturer-specific codes. */
    obd_vin_maker_t mk;
    CHECK(obd_vin_maker(HONDA_VIN, &mk));
    CHECK_EQ_STR(mk.maker, "Honda");
    int honda = obd_dtc_make_find(mk.dtc_make);
    CHECK(honda >= 0);
    CHECK_EQ_STR(obd_dtc_text_for("P1456", honda),
                 "Evaporative Emission Control System Leak Detected Fuel Tank System");
    CHECK(obd_vin_maker("1C4RJFAG0FC000001", &mk));
    CHECK(strstr(mk.maker, "Jeep") != NULL);
    CHECK(obd_dtc_make_find(mk.dtc_make) >= 0);
    CHECK(!obd_vin_maker("ZZZ00000000000000", &mk));
    CHECK(mk.maker == NULL);

    /* Every listed maker's DTC make is known to the description tables. */
    static const char *const sample_wmis[] = {
        "1HG", "19U", "4T1", "JTH", "1FA", "1LN", "1ME", "1G1", "1GT", "1G4", "1G6", "1G2",
        "1G3", "1G8", "1C3", "1B3", "1J4", "1P3", "1N4", "JNK", "JM1", "JA3", "JF1", "KNA",
        "JS2", "WVW", "WAU", "WBA", "WDB", "SAJ", NULL,
    };
    for (int i = 0; sample_wmis[i] != NULL; i++) {
        char v[OBD_VIN_LEN + 1];
        snprintf(v, sizeof v, "%s00000000000000", sample_wmis[i]);
        CHECK(obd_vin_maker(v, &mk));
        if (mk.dtc_make != NULL && obd_dtc_make_find(mk.dtc_make) < 0) {
            gb_check_failf(__FILE__, __LINE__, "%s: make %s unknown to the DTC tables",
                           sample_wmis[i], mk.dtc_make);
        }
    }
}

static void test_vin_model(void)
{
    /* Made-up VINs (serials 000001), decoded with NHTSA vPIC's patterns. */
    obd_vin_model_t md;
    CHECK(obd_vin_model("4S4WMAHD0P3000001", 2023, &md));
    CHECK_EQ_STR(md.make, "Subaru");
    CHECK_EQ_STR(md.model, "Ascent");
    CHECK(obd_vin_model("5FPYK16528B000001", 2008, &md));
    CHECK_EQ_STR(md.make, "Honda");
    CHECK_EQ_STR(md.model, "Ridgeline");
    CHECK(obd_vin_model("1C4PJMDS0FW000001", 2015, &md));   /* a group WMI: vPIC's make */
    CHECK_EQ_STR(md.make, "Jeep");
    CHECK_EQ_STR(md.model, "Cherokee");
    CHECK(obd_vin_model("1FTFW1E50JF000001", 2018, &md));
    CHECK_EQ_STR(md.model, "F-150");

    CHECK(!obd_vin_model("5FPYK16628B000001", 2008, NULL));  /* no pattern for YK166 */
    CHECK(!obd_vin_model("4S4WMAHD0P3000001", 1990, NULL));  /* before the table */
    CHECK(!obd_vin_model("ZZZWMAHD0P3000001", 2023, NULL));  /* unknown WMI */
    CHECK(!obd_vin_model("4S4WMAHD", 2023, NULL));
    CHECK(!obd_vin_model(NULL, 2023, NULL));
}

void suite_obd(void)
{
    gb_suite("obd");
    RUN_TEST(test_family);
    RUN_TEST(test_legacy_headers_off_ridgeline);
    RUN_TEST(test_legacy_headers_on);
    RUN_TEST(test_can_headers_off);
    RUN_TEST(test_can_headers_on);
    RUN_TEST(test_bad_and_partial);
    RUN_TEST(test_limits);
    RUN_TEST(test_code_from_bytes);
    RUN_TEST(test_codes_can);
    RUN_TEST(test_codes_legacy);
    RUN_TEST(test_codes_bad);
    RUN_TEST(test_readiness_spark);
    RUN_TEST(test_readiness_diesel);
    RUN_TEST(test_readiness_from_reply);
    RUN_TEST(test_vin_decode);
    RUN_TEST(test_vin_facts);
    RUN_TEST(test_vin_model);
}
