/* 3DS connection-test answers: DNS replies and the test page. */
#include <string.h>

#include "harness.h"
#include "transport/conntest.h"

static const uint8_t IP[4] = { 192, 168, 0, 11 };

/* Builds a one-question query for name/type with id 0x1234 and RD set. */
static size_t make_query(uint8_t *q, const char *name, unsigned qtype)
{
    static const uint8_t head[12] = { 0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0 };
    memcpy(q, head, sizeof head);
    size_t n = 12;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t l = dot ? (size_t)(dot - p) : strlen(p);
        q[n++] = (uint8_t)l;
        memcpy(q + n, p, l);
        n += l;
        p += l;
        if (*p == '.') {
            p++;
        }
    }
    q[n++] = 0;
    q[n++] = (uint8_t)(qtype >> 8);
    q[n++] = (uint8_t)qtype;
    q[n++] = 0;
    q[n++] = 1; /* class IN */
    return n;
}

static void test_test_hosts(void)
{
    CHECK(ct_is_test_host("conntest.nintendowifi.net"));
    CHECK(ct_is_test_host("CONNTEST.NintendoWiFi.net."));
    CHECK(ct_is_test_host("ctest.cdn.nintendo.net"));
    CHECK(ct_is_test_host("nncs1.app.nintendowifi.net"));
    CHECK(ct_is_test_host("nncs2.app.nintendowifi.net"));
    CHECK(!ct_is_test_host("nasc.nintendowifi.net"));
    CHECK(!ct_is_test_host("conntest.nintendowifi.net.evil"));
    CHECK(!ct_is_test_host(""));
}

static void test_dns_answers_test_name(void)
{
    uint8_t q[CT_DNS_MAX], r[CT_DNS_MAX];
    char note[128];
    size_t ql = make_query(q, "conntest.nintendowifi.net", 1);
    size_t rl = ct_dns_reply(q, ql, IP, r, sizeof r, note, sizeof note);

    CHECK_EQ_INT((int)rl, (int)ql + 16);
    CHECK(r[0] == 0x12 && r[1] == 0x34);         /* same id */
    CHECK(r[2] == 0x85 && r[3] == 0x00);         /* QR, AA, RD; NOERROR */
    CHECK(r[5] == 1 && r[7] == 1);               /* 1 question, 1 answer */
    CHECK(memcmp(r + 12, q + 12, ql - 12) == 0); /* question copied */
    static const uint8_t rr[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 0, 11 };
    CHECK(memcmp(r + ql, rr, sizeof rr) == 0);
    CHECK_EQ_STR(note, "conntest.nintendowifi.net A -> 192.168.0.11");
}

static void test_dns_other_names_not_found(void)
{
    uint8_t q[CT_DNS_MAX], r[CT_DNS_MAX];
    char note[128];
    size_t ql = make_query(q, "nasc.nintendowifi.net", 1);
    size_t rl = ct_dns_reply(q, ql, IP, r, sizeof r, note, sizeof note);
    CHECK_EQ_INT((int)rl, (int)ql);
    CHECK_EQ_INT(r[3] & 0x0F, 3);                /* NXDOMAIN */
    CHECK(r[7] == 0);                            /* no answer */
    CHECK_EQ_STR(note, "nasc.nintendowifi.net type 1: not found");
}

static void test_dns_test_name_other_type_has_no_record(void)
{
    uint8_t q[CT_DNS_MAX], r[CT_DNS_MAX];
    char note[128];
    size_t ql = make_query(q, "conntest.nintendowifi.net", 28); /* AAAA */
    size_t rl = ct_dns_reply(q, ql, IP, r, sizeof r, note, sizeof note);
    CHECK_EQ_INT((int)rl, (int)ql);
    CHECK_EQ_INT(r[3] & 0x0F, 0);
    CHECK(r[7] == 0);
}

static void test_dns_bad_packets(void)
{
    uint8_t q[CT_DNS_MAX], r[CT_DNS_MAX];
    char note[128];
    size_t ql = make_query(q, "conntest.nintendowifi.net", 1);

    /* Too short: ignored. */
    CHECK_EQ_INT((int)ct_dns_reply(q, 11, IP, r, sizeof r, note, sizeof note), 0);
    /* A response, not a query: ignored. */
    q[2] |= 0x80;
    CHECK_EQ_INT((int)ct_dns_reply(q, ql, IP, r, sizeof r, note, sizeof note), 0);
    q[2] &= 0x7F;
    /* Truncated in the middle of the name: FORMERR header only. */
    CHECK_EQ_INT((int)ct_dns_reply(q, 20, IP, r, sizeof r, note, sizeof note), 12);
    CHECK_EQ_INT(r[3] & 0x0F, 1);
    /* Compressed name in the question: FORMERR. */
    q[12] = 0xC0;
    CHECK_EQ_INT((int)ct_dns_reply(q, ql, IP, r, sizeof r, note, sizeof note), 12);
    CHECK_EQ_INT(r[3] & 0x0F, 1);
    /* Two questions: FORMERR. */
    ql = make_query(q, "conntest.nintendowifi.net", 1);
    q[5] = 2;
    CHECK_EQ_INT((int)ct_dns_reply(q, ql, IP, r, sizeof r, note, sizeof note), 12);
    /* Output buffer too small: ignored. */
    q[5] = 1;
    CHECK_EQ_INT((int)ct_dns_reply(q, ql, IP, r, 100, note, sizeof note), 0);
}

static void test_http_reply(void)
{
    size_t len;
    const char *reply = ct_http_reply(&len);
    CHECK_EQ_INT((int)len, (int)strlen(reply));
    CHECK(strncmp(reply, "HTTP/1.1 200 OK\r\n", 17) == 0);
    CHECK(strstr(reply, "\r\nX-Organization: Nintendo\r\n") != NULL);

    /* Content-Length matches the body. */
    const char *body = strstr(reply, "\r\n\r\n");
    CHECK(body != NULL);
    if (body != NULL) {
        body += 4;
        char expect[64];
        snprintf(expect, sizeof expect, "Content-Length: %d\r\n", (int)strlen(body));
        CHECK(strstr(reply, expect) != NULL);
    }
}

static void test_http_request_complete(void)
{
    const char *full = "GET / HTTP/1.1\r\nHost: conntest.nintendowifi.net\r\nUser-Agent: CTR AC/02\r\n\r\n";
    CHECK(ct_http_request_complete(full, strlen(full)));
    CHECK(!ct_http_request_complete(full, strlen(full) - 2));
    CHECK(ct_http_request_complete("GET / HTTP/1.0\n\n", 16));
    CHECK(!ct_http_request_complete("", 0));
}

static void test_http_only_test_hosts(void)
{
    CHECK(ct_http_is_test_request("GET / HTTP/1.1\r\nHost: conntest.nintendowifi.net\r\nUser-Agent: CTR AC/02\r\n\r\n"));
    CHECK(ct_http_is_test_request("GET / HTTP/1.1\r\nhost:CTEST.cdn.nintendo.net:80\r\n\r\n"));
    /* Another device on the network (seen on hardware). */
    CHECK(!ct_http_is_test_request("GET /api/status HTTP/1.1\r\nHost: 192.168.1.187\r\n\r\n"));
    CHECK(!ct_http_is_test_request("GET / HTTP/1.0\r\n\r\n"));
    CHECK(!ct_http_is_test_request(""));
    size_t len;
    const char *nf = ct_http_not_found(&len);
    CHECK_EQ_INT((int)len, (int)strlen(nf));
    CHECK(strncmp(nf, "HTTP/1.1 404 ", 13) == 0);
}

void suite_conntest(void)
{
    gb_suite("conntest");
    RUN_TEST(test_test_hosts);
    RUN_TEST(test_dns_answers_test_name);
    RUN_TEST(test_dns_other_names_not_found);
    RUN_TEST(test_dns_test_name_other_type_has_no_record);
    RUN_TEST(test_dns_bad_packets);
    RUN_TEST(test_http_reply);
    RUN_TEST(test_http_request_complete);
    RUN_TEST(test_http_only_test_hosts);
}
