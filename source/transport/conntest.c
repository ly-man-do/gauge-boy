#include "conntest.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *const TEST_HOSTS[] = {
    "conntest.nintendowifi.net",
    "ctest.cdn.nintendo.net",
    "nncs1.app.nintendowifi.net",
    "nncs2.app.nintendowifi.net",
};

#define TYPE_A   1
#define CLASS_IN 1
#define RCODE_OK       0
#define RCODE_FORMERR  1
#define RCODE_NXDOMAIN 3

bool ct_is_test_host(const char *name)
{
    size_t n = strlen(name);
    if (n > 0 && name[n - 1] == '.') {
        n--;
    }
    for (size_t i = 0; i < sizeof TEST_HOSTS / sizeof TEST_HOSTS[0]; i++) {
        const char *h = TEST_HOSTS[i];
        if (strlen(h) != n) {
            continue;
        }
        size_t j = 0;
        while (j < n && tolower((unsigned char)name[j]) == h[j]) {
            j++;
        }
        if (j == n) {
            return true;
        }
    }
    return false;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* Header-only reply (no question copied), for queries we can't parse. */
static size_t header_reply(const uint8_t *q, uint8_t *out, unsigned rcode)
{
    unsigned flags = 0x8000u | 0x0400u | ((((unsigned)q[2] << 8) | q[3]) & 0x7900u) | rcode;
    memset(out, 0, 12);
    out[0] = q[0];
    out[1] = q[1];
    put16(out + 2, flags);
    return 12;
}

size_t ct_dns_reply(const uint8_t *query, size_t len, const uint8_t ip[4],
                    uint8_t *out, size_t out_len, char *note, size_t note_len)
{
    if (note_len > 0) {
        note[0] = '\0';
    }
    if (len < 12 || len > CT_DNS_MAX || out_len < CT_DNS_MAX) {
        return 0;
    }
    if (query[2] & 0x80) {
        return 0; /* a response, not a query */
    }
    unsigned qdcount = ((unsigned)query[4] << 8) | query[5];
    if (qdcount != 1) {
        snprintf(note, note_len, "query with %u questions", qdcount);
        return header_reply(query, out, RCODE_FORMERR);
    }

    /* Question name: labels up to the zero byte; no compression allowed. */
    char name[256];
    size_t name_len = 0;
    size_t i = 12;
    for (;;) {
        if (i >= len) {
            snprintf(note, note_len, "truncated name");
            return header_reply(query, out, RCODE_FORMERR);
        }
        unsigned l = query[i];
        if (l == 0) {
            i++;
            break;
        }
        if ((l & 0xC0u) != 0 || i + 1 + l > len || name_len + l + 1 >= sizeof name) {
            snprintf(note, note_len, "bad name");
            return header_reply(query, out, RCODE_FORMERR);
        }
        if (name_len > 0) {
            name[name_len++] = '.';
        }
        for (unsigned k = 0; k < l; k++) {
            unsigned char c = query[i + 1 + k];
            name[name_len++] = (char)(isprint(c) ? tolower(c) : '?');
        }
        i += 1 + l;
    }
    name[name_len] = '\0';
    if (i + 4 > len) {
        snprintf(note, note_len, "truncated question");
        return header_reply(query, out, RCODE_FORMERR);
    }
    unsigned qtype = ((unsigned)query[i] << 8) | query[i + 1];
    unsigned qclass = ((unsigned)query[i + 2] << 8) | query[i + 3];
    size_t qend = i + 4;

    bool ours = ct_is_test_host(name);
    bool answer = ours && qtype == TYPE_A && qclass == CLASS_IN;
    unsigned rcode = ours ? RCODE_OK : RCODE_NXDOMAIN;

    /* Header, then the question copied as asked, then the answer. */
    unsigned flags = 0x8000u | 0x0400u | ((((unsigned)query[2] << 8) | query[3]) & 0x7900u) | rcode;
    out[0] = query[0];
    out[1] = query[1];
    put16(out + 2, flags);
    put16(out + 4, 1);
    put16(out + 6, answer ? 1 : 0);
    put16(out + 8, 0);
    put16(out + 10, 0);
    memcpy(out + 12, query + 12, qend - 12);
    size_t n = qend;
    if (answer) {
        static const uint8_t rr[] = {
            0xC0, 0x0C,             /* name: pointer to the question */
            0x00, TYPE_A, 0x00, CLASS_IN,
            0x00, 0x00, 0x00, 0x3C, /* TTL 60 s */
            0x00, 0x04,
        };
        memcpy(out + n, rr, sizeof rr);
        n += sizeof rr;
        memcpy(out + n, ip, 4);
        n += 4;
        snprintf(note, note_len, "%s A -> %u.%u.%u.%u", name, ip[0], ip[1], ip[2], ip[3]);
    } else if (ours) {
        snprintf(note, note_len, "%s type %u: no record", name, qtype);
    } else {
        snprintf(note, note_len, "%s type %u: not found", name, qtype);
    }
    return n;
}

static const char HTTP_REPLY[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "X-Organization: Nintendo\r\n"
    "Content-Length: 103\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>\n<html><head><title>HTML Page</title></head>\n"
    "<body>This is test.html page</body></html>\n";

const char *ct_http_reply(size_t *len)
{
    *len = sizeof HTTP_REPLY - 1;
    return HTTP_REPLY;
}

bool ct_http_request_complete(const char *buf, size_t len)
{
    for (size_t i = 0; i + 1 < len; i++) {
        if (buf[i] == '\n' && (buf[i + 1] == '\n' ||
                               (i + 2 < len && buf[i + 1] == '\r' && buf[i + 2] == '\n'))) {
            return true;
        }
    }
    return false;
}
