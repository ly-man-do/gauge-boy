#ifndef GAUGEBOY_OBD_CODES_H
#define GAUGEBOY_OBD_CODES_H

/*
 * Trouble codes from the code-reading services:
 *   0x03  stored (confirmed) codes, the ones that turn the CEL on
 *   0x07  pending codes, seen once but not yet confirmed
 *   0x0A  permanent codes (2010+ US cars; older cars refuse the service)
 *
 * Works on assembled replies (obd_reply.h), so it handles every protocol:
 *   CAN     one message per ECU: service byte, a count byte, then 2 bytes
 *           per code ("43 02 01 33 01 34" = P0133, P0134)
 *   legacy  one frame per message: service byte, then three 2-byte codes,
 *           padded with 00 00 ("43 01 33 00 00 00 00" = P0133)
 *
 * Platform-free.
 */

#include <stdbool.h>
#include <stdint.h>

#include "obd_reply.h"

#define OBD_SERVICE_STORED    0x03
#define OBD_SERVICE_PENDING   0x07
#define OBD_SERVICE_PERMANENT 0x0A

#define OBD_MAX_CODES 64

typedef struct {
    char code[6];      /* "P0171" */
    bool has_ecu;      /* false with headers off */
    uint32_t ecu;      /* CAN ID or legacy source address of the first ECU reporting it */
} obd_code_t;

typedef struct {
    obd_code_t code[OBD_MAX_CODES];
    int count;
    int ecus_answered;     /* ECUs that sent a positive reply (possibly 0 codes) */
    int ecus_refused;      /* ECUs that refused the service (7F reply) */
    int bad_messages;      /* messages that didn't match the service or were cut short */
    bool truncated;        /* more than OBD_MAX_CODES codes */
} obd_code_list_t;

/* Two DTC bytes -> "P0171" (out must hold 6 chars). */
void obd_code_from_bytes(uint8_t a, uint8_t b, char out[6]);

/*
 * Decodes an assembled reply to service 0x03, 0x07 or 0x0A. Codes are
 * listed once each, in the order received. Returns false for a bad
 * service number. A reply with no ECU answering positively is not an
 * error: check ecus_answered (0 and ecus_refused > 0 means the car
 * doesn't support the service, e.g. permanent codes before 2010).
 * An adapter "NO DATA" reply (no messages) also gives an empty list.
 */
bool obd_decode_codes(const obd_reply_t *reply, int protocol, uint8_t service,
                      obd_code_list_t *out);

#endif
