#ifndef GAUGEBOY_CONNTEST_H
#define GAUGEBOY_CONNTEST_H

/*
 * Answers for the Nintendo 3DS internet connection test, so the 3DS
 * keeps a Wi-Fi OBD-II adapter's network (which has no internet) instead
 * of dropping it with error 003-2002.
 *
 * The test looks up a few Nintendo host names and fetches a page; it
 * passes on HTTP 200 with "X-Organization: Nintendo". Gauge Boy runs a
 * tiny DNS server that answers those names with the 3DS's own address
 * and a web server that serves that reply (see tools/fake_conntest.py,
 * the same logic on a computer).
 *
 * Platform-free: packet building only, no sockets, so the host tests
 * can check it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CT_DNS_MAX 512   /* classic UDP DNS message size */

/* True for the host names the 3DS connection test looks up. name is
 * dotted, case-insensitive, with or without a trailing dot. */
bool ct_is_test_host(const char *name);

/*
 * Builds the reply to one DNS query.
 *   ip       answer for the test names, 4 bytes in network order
 *   out      receives the reply (at least CT_DNS_MAX bytes)
 *   note     receives a short log text, e.g. "conntest.nintendowifi.net A"
 * Test names: an A record with ip for type A, an empty answer for other
 * types. Other names: NXDOMAIN (there is no internet to forward to).
 * Returns the reply length, or 0 if the packet should be ignored (too
 * short, or not a query).
 */
size_t ct_dns_reply(const uint8_t *query, size_t len, const uint8_t ip[4],
                    uint8_t *out, size_t out_len, char *note, size_t note_len);

/* The complete HTTP response for the test page (any path). */
const char *ct_http_reply(size_t *len);

/* True if the request's Host header is one of the test host names (the
 * 3DS's connection test), so other devices' requests can be refused. */
bool ct_http_is_test_request(const char *request);

/* A short "404 Not Found" response for any other request. */
const char *ct_http_not_found(size_t *len);

/* True once buf (len bytes read so far) holds a whole HTTP request head
 * (ends with an empty line). */
bool ct_http_request_complete(const char *buf, size_t len);

#endif
