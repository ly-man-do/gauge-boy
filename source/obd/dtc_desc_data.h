#ifndef GAUGEBOY_DTC_DESC_DATA_H
#define GAUGEBOY_DTC_DESC_DATA_H

/* Generated tables behind obd/dtc.h (tools/make_dtc_table.py). Sorted by
 * key: the two DTC bytes as a 16-bit number (P0171 -> 0x0171). */

#include <stdint.h>

typedef struct {
    uint16_t key;
    const char *text;
} dtc_desc_t;

extern const unsigned dtc_desc_count;
extern const dtc_desc_t dtc_desc_table[];

/* Makes with manufacturer-specific descriptions; index = bit in makes. */
extern const unsigned dtc_make_count;
extern const char *const dtc_make_names[];

/* Manufacturer-range codes, sorted by key then text; one entry per text,
 * with a bit for each make it applies to. */
typedef struct {
    uint16_t key;
    uint64_t makes;
    const char *text;
} dtc_mfr_desc_t;

extern const unsigned dtc_mfr_count;
extern const dtc_mfr_desc_t dtc_mfr_table[];

#endif
