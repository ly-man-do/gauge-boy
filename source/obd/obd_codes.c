#include "obd_codes.h"

#include <string.h>

void obd_code_from_bytes(uint8_t a, uint8_t b, char out[6])
{
    static const char systems[] = "PCBU";
    static const char hex[] = "0123456789ABCDEF";
    out[0] = systems[a >> 6];
    out[1] = hex[(a >> 4) & 0x3];
    out[2] = hex[a & 0xF];
    out[3] = hex[b >> 4];
    out[4] = hex[b & 0xF];
    out[5] = '\0';
}

static void add_code(obd_code_list_t *out, const obd_ecu_t *ecu, uint8_t a, uint8_t b)
{
    if (a == 0 && b == 0) {
        return; /* padding (P0000 is not a real code) */
    }
    char code[6];
    obd_code_from_bytes(a, b, code);
    for (int i = 0; i < out->count; i++) {
        if (strcmp(out->code[i].code, code) == 0) {
            return;
        }
    }
    if (out->count >= OBD_MAX_CODES) {
        out->truncated = true;
        return;
    }
    obd_code_t *c = &out->code[out->count++];
    memcpy(c->code, code, sizeof c->code);
    c->has_ecu = ecu->has_id;
    c->ecu = ecu->id;
}

bool obd_decode_codes(const obd_reply_t *reply, int protocol, uint8_t service,
                      obd_code_list_t *out)
{
    memset(out, 0, sizeof *out);
    if (service != OBD_SERVICE_STORED && service != OBD_SERVICE_PENDING &&
        service != OBD_SERVICE_PERMANENT) {
        return false;
    }
    if (reply == NULL) {
        return true;
    }
    obd_family_t family = obd_family(protocol);
    uint8_t positive = (uint8_t)(service + 0x40);

    for (int e = 0; e < reply->ecu_count; e++) {
        const obd_ecu_t *ecu = &reply->ecu[e];
        bool answered = false, refused = false;
        for (int m = 0; m < ecu->msg_count; m++) {
            const obd_msg_t *msg = &ecu->msg[m];
            if (msg->len >= 2 && msg->data[0] == 0x7F && msg->data[1] == service) {
                refused = true; /* negative response: service not supported */
                continue;
            }
            if (msg->len < 1 || msg->data[0] != positive) {
                out->bad_messages++;
                continue;
            }
            const uint8_t *p = msg->data + 1;
            int n = msg->len - 1;
            if (family == OBD_FAMILY_CAN) {
                /* Count byte, then the codes. Some ECUs send "43 00" for
                 * none. A count larger than the bytes present is a cut
                 * reply: keep the whole codes that arrived. */
                if (n < 1) {
                    out->bad_messages++;
                    continue;
                }
                int count = p[0];
                p++;
                n--;
                if (n < 2 * count || (n % 2) != 0) {
                    out->bad_messages++;
                }
                if (2 * count < n) {
                    n = 2 * count;
                }
            } else if ((n % 2) != 0) {
                out->bad_messages++;
            }
            answered = true;
            for (int i = 0; i + 1 < n; i += 2) {
                add_code(out, ecu, p[i], p[i + 1]);
            }
        }
        if (answered) {
            out->ecus_answered++;
        } else if (refused) {
            out->ecus_refused++;
        }
    }
    return true;
}
