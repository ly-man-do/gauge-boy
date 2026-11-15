#include "dtc.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

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
    return obd_dtc_text_for(code, -1);
}

unsigned obd_dtc_description_count(void)
{
    return dtc_desc_count;
}

/* ---------- manufacturer-specific ---------- */

int obd_dtc_make_count(void)
{
    return (int)dtc_make_count;
}

const char *obd_dtc_make_name(int make)
{
    return make >= 0 && make < (int)dtc_make_count ? dtc_make_names[make] : NULL;
}

static bool same_name(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

int obd_dtc_make_find(const char *name)
{
    if (name == NULL) {
        return -1;
    }
    /* Other spellings of a listed make. */
    static const char *const aliases[][2] = {
        { "Chevy", "Chevrolet" },
        { "Mercedes", "Mercedes-Benz" },
        { "VW", "Volkswagen" },
        { "General Motors", "GM" },
    };
    for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; i++) {
        if (same_name(name, aliases[i][0])) {
            name = aliases[i][1];
            break;
        }
    }
    for (unsigned i = 0; i < dtc_make_count; i++) {
        if (same_name(name, dtc_make_names[i])) {
            return (int)i;
        }
    }
    return -1;
}

const char *obd_dtc_description_for(const char *code, int make)
{
    uint16_t key;
    if (!obd_dtc_key(code, &key)) {
        return NULL;
    }
    if (key_is_generic(key)) {
        return obd_dtc_description(code);
    }
    if (make < 0 || make >= (int)dtc_make_count) {
        return NULL;
    }
    uint64_t bit = (uint64_t)1 << make;
    unsigned lo = 0, hi = dtc_mfr_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (dtc_mfr_table[mid].key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (; lo < dtc_mfr_count && dtc_mfr_table[lo].key == key; lo++) {
        if (dtc_mfr_table[lo].makes & bit) {
            return dtc_mfr_table[lo].text;
        }
    }
    return NULL;
}

const char *obd_dtc_text_for(const char *code, int make)
{
    uint16_t key;
    if (!obd_dtc_key(code, &key)) {
        return "Not a valid code";
    }
    const char *d = obd_dtc_description_for(code, make);
    if (d != NULL) {
        return d;
    }
    return key_is_generic(key) ? "Generic code (no description)" : "Manufacturer-specific code";
}
