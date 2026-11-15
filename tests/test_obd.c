/* OBD-II reply assembly for every protocol family (source/obd/obd_reply). */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "obd/obd_reply.h"

static obd_reply_t reply; /* large: keep off the stack */
static char lines[ELM_MAX_LINES][ELM_LINE_MAX];

/* Copies up to ELM_MAX_LINES lines (NULL-terminated list) into lines. */
static int set_lines(const char *const *src)
{
    int n = 0;
    memset(lines, 0, sizeof lines);
    for (; src[n] != NULL && n < ELM_MAX_LINES; n++) {
        snprintf(lines[n], ELM_LINE_MAX, "%s", src[n]);
    }
    return n;
}

static obd_status_t run(const char *const *src, int protocol, bool headers)
{
    int n = set_lines(src);
    return obd_assemble(lines, n, protocol, headers, &reply);
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
    CHECK_EQ_INT(obd_assemble(lines, 0, 0, false, &reply), OBD_ERR_INVALID_ARG);
    CHECK_EQ_INT(obd_assemble(lines, 1, 6, false, NULL), OBD_ERR_INVALID_ARG);
    CHECK_EQ_INT(obd_assemble(lines, 0, 6, false, &reply), OBD_ERR_NO_DATA);
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
}
