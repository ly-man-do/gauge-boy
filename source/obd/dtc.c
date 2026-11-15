#include "dtc.h"

#include <stddef.h>

#include "dtc_desc_data.h"

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool obd_dtc_key(const char *code, uint16_t *key)
{
    if (code == NULL) {
        return false;
    }
    unsigned system;
    switch (code[0]) {
    case 'P': case 'p': system = 0; break;
    case 'C': case 'c': system = 1; break;
    case 'B': case 'b': system = 2; break;
    case 'U': case 'u': system = 3; break;
    default: return false;
    }
    unsigned v = 0;
    for (int i = 1; i <= 4; i++) {
        int d = hex_digit(code[i]);
        if (d < 0) {
            return false;
        }
        v = (v << 4) | (unsigned)d;
    }
    /* The first digit is only two bits in the DTC bytes (0-3). */
    if (code[5] != '\0' || (v >> 12) > 3) {
        return false;
    }
    if (key != NULL) {
        *key = (uint16_t)((system << 14) | v);
    }
    return true;
}

static bool key_is_generic(uint16_t key)
{
    unsigned system = key >> 14;
    unsigned d1 = (key >> 12) & 0x3;
    unsigned d2 = (key >> 8) & 0xF;
    if (system == 0) { /* P */
        return d1 == 0 || d1 == 2 || (d1 == 3 && d2 >= 4);
    }
    return d1 == 0 || d1 == 3; /* B0/B3, C0/C3, U0/U3 */
}

bool obd_dtc_is_generic(const char *code)
{
    uint16_t key;
    return obd_dtc_key(code, &key) && key_is_generic(key);
}

const char *obd_dtc_description(const char *code)
{
    uint16_t key;
    if (!obd_dtc_key(code, &key) || !key_is_generic(key)) {
        return NULL;
    }
    unsigned lo = 0, hi = dtc_desc_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (dtc_desc_table[mid].key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < dtc_desc_count && dtc_desc_table[lo].key == key) {
        return dtc_desc_table[lo].text;
    }
    return NULL;
}

const char *obd_dtc_text(const char *code)
{
    uint16_t key;
    if (!obd_dtc_key(code, &key)) {
        return "Not a valid code";
    }
    if (!key_is_generic(key)) {
        return "Manufacturer-specific code";
    }
    const char *d = obd_dtc_description(code);
    return d != NULL ? d : "Generic code (no description)";
}

unsigned obd_dtc_description_count(void)
{
    return dtc_desc_count;
}
