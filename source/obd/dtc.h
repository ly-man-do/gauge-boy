#ifndef GAUGEBOY_OBD_DTC_H
#define GAUGEBOY_OBD_DTC_H

/*
 * Plain-English descriptions for generic diagnostic trouble codes.
 *
 * Generic codes (SAE-controlled ranges P0, P2, P34-P3F, B0, B3, C0, C3,
 * U0, U3) mean the same on every car and come with a description.
 * Codes in the manufacturer ranges (P1, P30-P33, B1, B2, C1, C2, U1, U2)
 * mean different things on different makes, so they are reported as
 * "Manufacturer-specific code" (brief section 5d).
 *
 * Descriptions: generated table from Wal33D/dtc-database (MIT), see
 * tools/make_dtc_table.py and CREDITS.md. Platform-free.
 */

#include <stdbool.h>
#include <stdint.h>

/* "P0171" (letter + 4 hex digits, any case) -> the two DTC bytes as a
 * 16-bit key (0x0171). False if the text isn't a DTC. */
bool obd_dtc_key(const char *code, uint16_t *key);

/* True for codes in the generic (SAE-controlled) ranges. */
bool obd_dtc_is_generic(const char *code);

/* Description of a generic code, or NULL if the code is invalid,
 * manufacturer-specific or not in the table. */
const char *obd_dtc_description(const char *code);

/* Text to show next to a code; never NULL: the description,
 * "Manufacturer-specific code", "Generic code (no description)" or
 * "Not a valid code". */
const char *obd_dtc_text(const char *code);

/* Number of codes with a description (for the credits / about screen). */
unsigned obd_dtc_description_count(void);

#endif
