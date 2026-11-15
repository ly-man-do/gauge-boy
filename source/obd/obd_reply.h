#ifndef GAUGEBOY_OBD_REPLY_H
#define GAUGEBOY_OBD_REPLY_H

/*
 * OBD-II reply assembly: turns the cleaned lines of an ELM327 reply into
 * messages per ECU, for every protocol the ELM327 handles (brief 1.7.0
 * section 4). Decoders (VIN, DTCs, PIDs) then read whole messages and
 * never see headers, frame counters or line layouts.
 *
 * Protocol families (ELM327 protocol numbers, from ATDPN):
 *   legacy  1-5  SAE J1850 PWM / VPW, ISO 9141-2, ISO 14230-4 KWP
 *   CAN     6-C  ISO 15765-4 (11 / 29-bit, 500 / 250k), SAE J1939, USER1/2
 *
 * Line layouts handled, with spaces on (ATS1) or off (ATS0):
 *   CAN, headers off   "41 00 BE 3E B8 11"                 single frame
 *                      "014" then "0: 49 02 01 31 44 34",
 *                      "1: ..." ...                          multi-frame
 *   CAN, headers on    "7E8 06 41 00 BE 3E B8 11"          11-bit ID + PCI
 *                      "18 DA F1 10 06 41 00 ..."           29-bit ID + PCI
 *                      PCI 0x = single, 1x = first, 2x = consecutive frame
 *   legacy, headers off "49 02 01 00 00 00 31"             one frame a line
 *   legacy, headers on  "48 6B 10 49 02 01 ... CS"         3-byte header
 *                      (KWP: 4 bytes when the format byte holds no length),
 *                      ECU = source address, trailing check byte dropped
 *
 * Output messages start at the response service byte (e.g. 0x41, 0x43,
 * 0x49). CAN gives one reassembled message per ECU and request; legacy
 * gives one message per frame, in order (a VIN is five frames).
 *
 * Platform-free, no allocation; obd_reply_t is large (keep it static or
 * in a long-lived struct, not on a small thread stack).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "elm327/elm327.h"

#define OBD_MAX_ECUS 8
#define OBD_MAX_MSGS 8     /* messages (legacy frames) per ECU */
#define OBD_MSG_MAX  256   /* bytes per message */

typedef enum {
    OBD_FAMILY_UNKNOWN = 0,
    OBD_FAMILY_LEGACY,     /* ELM protocols 1-5 */
    OBD_FAMILY_CAN,        /* ELM protocols 6-C */
} obd_family_t;

typedef struct {
    uint8_t data[OBD_MSG_MAX];
    uint16_t len;
} obd_msg_t;

typedef struct {
    bool has_id;           /* false with headers off: the ECU isn't known */
    uint32_t id;           /* CAN ID (e.g. 0x7E8, 0x18DAF110) or legacy source address */
    obd_msg_t msg[OBD_MAX_MSGS];
    int msg_count;
} obd_ecu_t;

typedef struct {
    obd_ecu_t ecu[OBD_MAX_ECUS];
    int ecu_count;
    int bad_lines;         /* lines skipped as malformed, incomplete or out of order */
} obd_reply_t;

typedef enum {
    OBD_OK = 0,
    OBD_ERR_NO_DATA,       /* no usable message in the reply */
    OBD_ERR_INVALID_ARG,
    OBD_STATUS_COUNT
} obd_status_t;

/* Family for an ELM327 protocol number (0 = automatic / unknown). */
obd_family_t obd_family(int protocol);

/*
 * Assemble lines (as in elm_response_t) for the given ELM protocol
 * number (1-12) and header setting. Malformed or partial lines are
 * counted in out->bad_lines and skipped. Returns OBD_OK if at least one
 * message was found.
 */
obd_status_t obd_assemble(const char lines[][ELM_LINE_MAX], int line_count, int protocol,
                          bool headers, obd_reply_t *out);

const char *obd_status_str(obd_status_t status);

#endif
