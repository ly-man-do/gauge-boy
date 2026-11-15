#include "obd_reply.h"

#include <string.h>

obd_family_t obd_family(int protocol)
{
    if (protocol >= 1 && protocol <= 5) {
        return OBD_FAMILY_LEGACY;
    }
    if (protocol >= 6 && protocol <= 12) {
        return OBD_FAMILY_CAN;
    }
    return OBD_FAMILY_UNKNOWN;
}

/* 29-bit CAN IDs: ISO 15765-4 29-bit (7, 9) and SAE J1939 (A). USER1/2
 * (B, C) default to 11-bit. */
static bool can_29bit(int protocol)
{
    return protocol == 7 || protocol == 9 || protocol == 10;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static uint32_t hex_value(const char *s, size_t n)
{
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        v = (v << 4) | (uint32_t)hex_digit(s[i]);
    }
    return v;
}

/* One reply line, split into its parts. */
typedef struct {
    int frame_index;       /* "N:" prefix (CAN headers off), or -1 */
    bool length_line;      /* lone 3-digit length line (CAN headers off) */
    unsigned length;
    bool has_id;           /* CAN headers on */
    uint32_t id;
    uint8_t b[OBD_MSG_MAX];
    int n;
} line_t;

/* Hex digits of s with spaces removed, into digits (NUL-terminated).
 * Records where each space-separated token starts so a 3-digit 11-bit ID
 * or length can be told apart from byte pairs. Returns false on a
 * non-hex character. */
typedef struct {
    char digits[ELM_LINE_MAX];
    int len;
    int first_token_len;   /* digits in the first token; len if no spaces */
    bool spaced;
} digits_t;

static bool collect_digits(const char *s, digits_t *d)
{
    memset(d, 0, sizeof *d);
    d->first_token_len = -1;
    bool in_token = false;
    for (; *s != '\0'; s++) {
        if (*s == ' ') {
            if (in_token && d->first_token_len < 0) {
                d->first_token_len = d->len;
            }
            in_token = false;
            d->spaced = true;
            continue;
        }
        if (hex_digit(*s) < 0 || d->len + 1 >= (int)sizeof d->digits) {
            return false;
        }
        d->digits[d->len++] = *s;
        in_token = true;
    }
    if (d->first_token_len < 0) {
        d->first_token_len = d->len;
    }
    return d->len > 0;
}

static bool parse_line(const char *s, obd_family_t family, bool can29, bool headers, line_t *L)
{
    memset(L, 0, sizeof *L);
    L->frame_index = -1;
    while (*s == ' ') {
        s++;
    }
    /* "0:" ... "F:" frame index (CAN, headers off, multi-frame). */
    if (family == OBD_FAMILY_CAN && !headers && hex_digit(s[0]) >= 0 && s[1] == ':') {
        L->frame_index = hex_digit(s[0]);
        s += 2;
    }

    digits_t d;
    if (!collect_digits(s, &d)) {
        return false;
    }
    int pos = 0;

    if (family == OBD_FAMILY_CAN && !headers && L->frame_index < 0 && d.len == 3) {
        L->length_line = true;
        L->length = hex_value(d.digits, 3);
        return L->length > 0;
    }
    if (family == OBD_FAMILY_CAN && headers) {
        int id_digits = can29 ? 8 : 3;
        /* With spaces, an 11-bit ID is one 3-digit token and a 29-bit ID
         * four byte tokens; without spaces, the first 3 or 8 digits. */
        if (d.spaced && !can29 && d.first_token_len != 3) {
            return false;
        }
        if (d.len < id_digits) {
            return false;
        }
        L->has_id = true;
        L->id = hex_value(d.digits, (size_t)id_digits);
        pos = id_digits;
    }
    if ((d.len - pos) % 2 != 0) {
        return false;
    }
    for (; pos < d.len; pos += 2) {
        if (L->n >= (int)sizeof L->b) {
            return false;
        }
        L->b[L->n++] = (uint8_t)hex_value(d.digits + pos, 2);
    }
    return L->n > 0;
}

/* ---------- building the result ---------- */

typedef struct {
    obd_reply_t *out;
    /* Per message: complete yet? (messages being reassembled aren't) */
    bool done[OBD_MAX_ECUS][OBD_MAX_MSGS];
    uint16_t want[OBD_MAX_ECUS][OBD_MAX_MSGS];  /* total length expected */
    int next_seq[OBD_MAX_ECUS][OBD_MAX_MSGS];
} builder_t;

/* ECU with this ID, or a new one (always new when has_id is false). */
static int ecu_slot(builder_t *bld, bool has_id, uint32_t id)
{
    obd_reply_t *r = bld->out;
    if (has_id) {
        for (int i = 0; i < r->ecu_count; i++) {
            if (r->ecu[i].has_id && r->ecu[i].id == id) {
                return i;
            }
        }
    }
    if (r->ecu_count >= OBD_MAX_ECUS) {
        return -1;
    }
    int i = r->ecu_count++;
    r->ecu[i].has_id = has_id;
    r->ecu[i].id = id;
    r->ecu[i].msg_count = 0;
    return i;
}

/* New message on ECU e; returns its index or -1. */
static int new_msg(builder_t *bld, int e, uint16_t want)
{
    obd_ecu_t *ecu = &bld->out->ecu[e];
    if (ecu->msg_count >= OBD_MAX_MSGS) {
        return -1;
    }
    int m = ecu->msg_count++;
    ecu->msg[m].len = 0;
    bld->done[e][m] = false;
    bld->want[e][m] = want;
    bld->next_seq[e][m] = 1;
    return m;
}

/* Append up to the expected length; marks the message done when full. */
static void append(builder_t *bld, int e, int m, const uint8_t *b, int n)
{
    obd_msg_t *msg = &bld->out->ecu[e].msg[m];
    uint16_t want = bld->want[e][m];
    for (int i = 0; i < n && msg->len < want && msg->len < OBD_MSG_MAX; i++) {
        msg->data[msg->len++] = b[i];
    }
    if (msg->len >= want) {
        bld->done[e][m] = true;
    }
}

static void add_whole(builder_t *bld, int e, const uint8_t *b, int n, int *bad)
{
    if (e < 0 || n <= 0 || n > OBD_MSG_MAX) {
        (*bad)++;
        return;
    }
    int m = new_msg(bld, e, (uint16_t)n);
    if (m < 0) {
        (*bad)++;
        return;
    }
    append(bld, e, m, b, n);
}

/* Last message of ECU e still waiting for consecutive frames, or -1. */
static int pending_msg(builder_t *bld, int e)
{
    obd_ecu_t *ecu = &bld->out->ecu[e];
    for (int m = ecu->msg_count - 1; m >= 0; m--) {
        if (!bld->done[e][m]) {
            return m;
        }
    }
    return -1;
}

/* Drop unfinished messages and empty ECUs. Returns how many were dropped. */
static int compact(builder_t *bld)
{
    obd_reply_t *r = bld->out;
    int dropped = 0;
    int w = 0;
    for (int e = 0; e < r->ecu_count; e++) {
        obd_ecu_t *ecu = &r->ecu[e];
        int mw = 0;
        for (int m = 0; m < ecu->msg_count; m++) {
            if (!bld->done[e][m]) {
                dropped++;
                continue;
            }
            if (mw != m) {
                ecu->msg[mw] = ecu->msg[m];
            }
            mw++;
        }
        ecu->msg_count = mw;
        if (mw == 0) {
            continue;
        }
        if (w != e) {
            r->ecu[w] = *ecu;
        }
        w++;
    }
    r->ecu_count = w;
    return dropped;
}

static void can_headers_off(builder_t *bld, const line_t *L, int *pend_e, int *pend_m, int *bad)
{
    if (L->length_line) {
        if (L->length > OBD_MSG_MAX) {
            (*bad)++;
            *pend_e = -1;
            return;
        }
        int e = ecu_slot(bld, false, 0);
        int m = e >= 0 ? new_msg(bld, e, (uint16_t)L->length) : -1;
        if (m < 0) {
            (*bad)++;
            *pend_e = -1;
            return;
        }
        *pend_e = e;
        *pend_m = m;
        bld->next_seq[e][m] = 0;
        return;
    }
    if (L->frame_index >= 0) {
        if (*pend_e < 0 || bld->done[*pend_e][*pend_m] ||
            L->frame_index != (bld->next_seq[*pend_e][*pend_m] & 0xF)) {
            (*bad)++;
            return;
        }
        bld->next_seq[*pend_e][*pend_m]++;
        append(bld, *pend_e, *pend_m, L->b, L->n);
        return;
    }
    /* Single frame: an ECU of its own (headers off: can't tell which). */
    add_whole(bld, ecu_slot(bld, false, 0), L->b, L->n, bad);
}

static void can_headers_on(builder_t *bld, const line_t *L, int *bad)
{
    int e = ecu_slot(bld, true, L->id);
    if (e < 0 || L->n < 1) {
        (*bad)++;
        return;
    }
    uint8_t pci = L->b[0];
    switch (pci >> 4) {
    case 0x0: { /* single frame */
        int len = pci & 0x0F;
        if (len == 0 || len > L->n - 1) {
            (*bad)++;
            return;
        }
        add_whole(bld, e, L->b + 1, len, bad);
        return;
    }
    case 0x1: { /* first frame */
        if (L->n < 2) {
            (*bad)++;
            return;
        }
        unsigned total = ((unsigned)(pci & 0x0F) << 8) | L->b[1];
        int m = (total > 0 && total <= OBD_MSG_MAX) ? new_msg(bld, e, (uint16_t)total) : -1;
        if (m < 0) {
            (*bad)++;
            return;
        }
        append(bld, e, m, L->b + 2, L->n - 2);
        return;
    }
    case 0x2: { /* consecutive frame */
        int m = pending_msg(bld, e);
        if (m < 0 || (pci & 0x0F) != (bld->next_seq[e][m] & 0x0F)) {
            (*bad)++;
            return;
        }
        bld->next_seq[e][m]++;
        append(bld, e, m, L->b + 1, L->n - 1);
        return;
    }
    case 0x3: /* flow control: nothing to keep */
        return;
    default:
        (*bad)++;
        return;
    }
}

static void legacy_headers_on(builder_t *bld, const line_t *L, int protocol, int *bad)
{
    int hdr = 3;
    /* KWP: a format byte with no length in it means a separate length byte. */
    if ((protocol == 4 || protocol == 5) && L->n >= 1 && (L->b[0] & 0x80) && (L->b[0] & 0x3F) == 0) {
        hdr = 4;
    }
    if (L->n < hdr + 2) { /* header, at least one data byte, check byte */
        (*bad)++;
        return;
    }
    /* ISO 9141-2 and KWP end with a sum check; J1850's CRC isn't checked. */
    if (protocol >= 3) {
        unsigned sum = 0;
        for (int i = 0; i < L->n - 1; i++) {
            sum += L->b[i];
        }
        if ((sum & 0xFF) != L->b[L->n - 1]) {
            (*bad)++;
            return;
        }
    }
    int e = ecu_slot(bld, true, L->b[2]);
    add_whole(bld, e, L->b + hdr, L->n - hdr - 1, bad);
}

obd_status_t obd_assemble(const char lines[][ELM_LINE_MAX], int line_count, int protocol,
                          bool headers, obd_reply_t *out)
{
    if (out == NULL || (lines == NULL && line_count > 0) || line_count < 0) {
        return OBD_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof *out);
    obd_family_t family = obd_family(protocol);
    if (family == OBD_FAMILY_UNKNOWN) {
        return OBD_ERR_INVALID_ARG;
    }

    static builder_t bld;   /* large; obd_assemble isn't reentrant */
    memset(&bld, 0, sizeof bld);
    bld.out = out;
    int bad = 0;
    int pend_e = -1, pend_m = -1;
    int legacy_ecu = -1;
    bool can29 = can_29bit(protocol);

    for (int i = 0; i < line_count; i++) {
        line_t L;
        if (!parse_line(lines[i], family, can29, headers, &L)) {
            bad++;
            continue;
        }
        if (family == OBD_FAMILY_CAN) {
            if (headers) {
                can_headers_on(&bld, &L, &bad);
            } else {
                can_headers_off(&bld, &L, &pend_e, &pend_m, &bad);
            }
        } else if (headers) {
            legacy_headers_on(&bld, &L, protocol, &bad);
        } else {
            /* Headers off: frames can't be told apart by ECU; keep them
             * together in order. */
            if (legacy_ecu < 0) {
                legacy_ecu = ecu_slot(&bld, false, 0);
            }
            add_whole(&bld, legacy_ecu, L.b, L.n, &bad);
        }
    }
    bad += compact(&bld);
    out->bad_lines = bad;
    return out->ecu_count > 0 ? OBD_OK : OBD_ERR_NO_DATA;
}

const char *obd_status_str(obd_status_t status)
{
    switch (status) {
    case OBD_OK:              return "OK";
    case OBD_ERR_NO_DATA:     return "No usable data in the reply";
    case OBD_ERR_INVALID_ARG: return "Invalid argument";
    case OBD_STATUS_COUNT:    break;
    }
    return "Unknown status";
}
