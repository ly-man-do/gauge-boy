#ifndef GAUGEBOY_OBD_VIN_H
#define GAUGEBOY_OBD_VIN_H

/*
 * VIN from Mode 09 PID 02, and the generic facts in it (CLAUDE.md section
 * 7: manufacturer from the WMI, model year from position 10; no model
 * names).
 *
 * Reply layouts (after obd_reply.h assembly):
 *   CAN     one message: 49 02 01 then the 17 VIN characters
 *   legacy  five frames: 49 02 0N then 4 bytes each, N = 1..5; frame 1
 *           is padded with 00 00 00 before the first character
 *
 * Platform-free.
 */

#include <stdbool.h>

#include "obd_reply.h"

#define OBD_VIN_LEN 17

/* Reads the VIN from an assembled reply to "09 02". vin gets 17
 * characters and a NUL. Returns false if no ECU sent 17 VIN characters. */
bool obd_decode_vin(const obd_reply_t *reply, int protocol, char vin[OBD_VIN_LEN + 1]);

/* 17 characters of 0-9 and A-Z without I, O, Q. */
bool obd_vin_chars_ok(const char *vin);

/* The North American check digit (position 9) matches. Many non-US VINs
 * don't use one, so a mismatch is a warning, not an error. */
bool obd_vin_check_digit_ok(const char *vin);

/* Model year from position 10. Each letter/digit stands for two years 30
 * apart; current_year (e.g. 2026) picks the latest one not after next
 * year. With current_year 0, position 7 decides as for North American
 * cars (a digit means 1980-2009, a letter 2010-2039). 0 if unknown. */
int obd_vin_model_year(const char *vin, int current_year);

typedef struct {
    const char *maker;      /* e.g. "Honda", or a group such as
                             * "Chrysler group (Chrysler, Dodge, Jeep, Ram)" */
    const char *dtc_make;   /* make name for obd_dtc_make_find(), or NULL */
} obd_vin_maker_t;

/* Manufacturer from the first three characters (WMI), from a short
 * built-in list of common makers. False if the WMI isn't listed. */
bool obd_vin_maker(const char *vin, obd_vin_maker_t *out);

#endif
