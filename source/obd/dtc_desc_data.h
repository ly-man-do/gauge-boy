#ifndef GAUGEBOY_DTC_DESC_DATA_H
#define GAUGEBOY_DTC_DESC_DATA_H

/* Generated table behind obd/dtc.h (tools/make_dtc_table.py). Sorted by
 * key: the two DTC bytes as a 16-bit number (P0171 -> 0x0171). */

#include <stdint.h>

typedef struct {
    uint16_t key;
    const char *text;
} dtc_desc_t;

extern const unsigned dtc_desc_count;
extern const dtc_desc_t dtc_desc_table[];

#endif
