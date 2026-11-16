#include "obd_vin.h"

#include "vin_model_data.h"

#include <ctype.h>
#include <string.h>

/* Takes the VIN out of bytes that may start with 00 padding: skips the
 * zeros, then needs 17 letters / digits. */
static bool vin_from_bytes(const uint8_t *p, int n, char vin[OBD_VIN_LEN + 1])
{
    int i = 0;
    while (i < n && p[i] == 0x00) {
        i++;
    }
    if (n - i < OBD_VIN_LEN) {
        return false;
    }
    for (int k = 0; k < OBD_VIN_LEN; k++) {
        unsigned char c = p[i + k];
        if (!isdigit(c) && !isupper(c)) {
            return false;
        }
        vin[k] = (char)c;
    }
    vin[OBD_VIN_LEN] = '\0';
    return true;
}

static bool vin_can(const obd_ecu_t *ecu, char vin[OBD_VIN_LEN + 1])
{
    for (int m = 0; m < ecu->msg_count; m++) {
        const obd_msg_t *msg = &ecu->msg[m];
        if (msg->len < 3 || msg->data[0] != 0x49 || msg->data[1] != 0x02) {
            continue;
        }
        /* 49 02, then the number of data items (01), then the VIN. */
        int start = msg->data[2] == 0x01 ? 3 : 2;
        if (vin_from_bytes(msg->data + start, msg->len - start, vin)) {
            return true;
        }
    }
    return false;
}

static bool vin_legacy(const obd_ecu_t *ecu, char vin[OBD_VIN_LEN + 1])
{
    uint8_t buf[20];
    bool have[5] = { false, false, false, false, false };
    memset(buf, 0, sizeof buf);
    for (int m = 0; m < ecu->msg_count; m++) {
        const obd_msg_t *msg = &ecu->msg[m];
        if (msg->len != 7 || msg->data[0] != 0x49 || msg->data[1] != 0x02) {
            continue;
        }
        int n = msg->data[2];
        if (n < 1 || n > 5) {
            continue;
        }
        memcpy(buf + (n - 1) * 4, msg->data + 3, 4);
        have[n - 1] = true;
    }
    for (int i = 0; i < 5; i++) {
        if (!have[i]) {
            return false;
        }
    }
    return vin_from_bytes(buf, (int)sizeof buf, vin);
}

bool obd_decode_vin(const obd_reply_t *reply, int protocol, char vin[OBD_VIN_LEN + 1])
{
    vin[0] = '\0';
    if (reply == NULL) {
        return false;
    }
    obd_family_t family = obd_family(protocol);
    for (int e = 0; e < reply->ecu_count; e++) {
        bool ok = family == OBD_FAMILY_CAN ? vin_can(&reply->ecu[e], vin)
                : family == OBD_FAMILY_LEGACY ? vin_legacy(&reply->ecu[e], vin)
                : false;
        if (ok) {
            return true;
        }
    }
    vin[0] = '\0';
    return false;
}

bool obd_vin_chars_ok(const char *vin)
{
    if (vin == NULL || strlen(vin) != OBD_VIN_LEN) {
        return false;
    }
    for (int i = 0; i < OBD_VIN_LEN; i++) {
        char c = vin[i];
        if (!(isdigit((unsigned char)c) || (c >= 'A' && c <= 'Z')) || c == 'I' || c == 'O' ||
            c == 'Q') {
            return false;
        }
    }
    return true;
}

static int vin_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    /* A-H 1-8, J-R 1-9 (no O or Q: P 7, R 9), S-Z 2-9. */
    static const char *const letters = "ABCDEFGHJKLMNPRSTUVWXYZ";
    static const int values[] = { 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 7, 9,
                                  2, 3, 4, 5, 6, 7, 8, 9 };
    const char *p = strchr(letters, c);
    return (p != NULL && c != '\0') ? values[p - letters] : -1;
}

bool obd_vin_check_digit_ok(const char *vin)
{
    static const int weights[OBD_VIN_LEN] = { 8, 7, 6, 5, 4, 3, 2, 10, 0, 9, 8, 7, 6, 5, 4, 3, 2 };
    if (!obd_vin_chars_ok(vin)) {
        return false;
    }
    int sum = 0;
    for (int i = 0; i < OBD_VIN_LEN; i++) {
        int v = vin_value(vin[i]);
        if (v < 0) {
            return false;
        }
        sum += v * weights[i];
    }
    int r = sum % 11;
    char want = r == 10 ? 'X' : (char)('0' + r);
    return vin[8] == want;
}

int obd_vin_model_year(const char *vin, int current_year)
{
    if (!obd_vin_chars_ok(vin)) {
        return 0;
    }
    /* Year codes in order from 1980 (A) to 2009 (9); U, Z and 0 unused. */
    static const char codes[] = "ABCDEFGHJKLMNPRSTVWXY123456789";
    const char *p = strchr(codes, vin[9]);
    if (p == NULL) {
        return 0;
    }
    int early = 1980 + (int)(p - codes);
    int late = early + 30;
    if (current_year > 0) {
        return late <= current_year + 1 ? late : early;
    }
    return isdigit((unsigned char)vin[6]) ? early : late;
}

/* Common world manufacturer identifiers (first three VIN characters).
 * Best-effort generic facts, not a complete list; makers that share WMIs
 * are reported as a group with a make whose trouble codes apply. */
typedef struct {
    const char *wmi;
    const char *maker;
    const char *dtc_make;
} wmi_t;

#define CHRYSLER_GROUP "Chrysler group (Chrysler, Dodge, Jeep, Ram)"

static const wmi_t wmis[] = {
    /* Honda / Acura */
    { "1HG", "Honda", "Honda" }, { "2HG", "Honda", "Honda" }, { "2HJ", "Honda", "Honda" },
    { "2HK", "Honda", "Honda" }, { "3HG", "Honda", "Honda" }, { "5FN", "Honda", "Honda" }, { "5FP", "Honda", "Honda" },
    { "5J6", "Honda", "Honda" }, { "JHM", "Honda", "Honda" }, { "JHL", "Honda", "Honda" },
    { "SHH", "Honda", "Honda" }, { "SHS", "Honda", "Honda" },
    { "19U", "Acura", "Acura" }, { "JH4", "Acura", "Acura" }, { "5J8", "Acura", "Acura" },
    /* Toyota / Lexus */
    { "4T1", "Toyota", "Toyota" }, { "4T3", "Toyota", "Toyota" }, { "4T4", "Toyota", "Toyota" },
    { "5TD", "Toyota", "Toyota" }, { "5TF", "Toyota", "Toyota" }, { "5TB", "Toyota", "Toyota" },
    { "2T1", "Toyota", "Toyota" }, { "2T3", "Toyota", "Toyota" }, { "JT2", "Toyota", "Toyota" },
    { "JT3", "Toyota", "Toyota" }, { "JTD", "Toyota", "Toyota" }, { "JTE", "Toyota", "Toyota" },
    { "JTM", "Toyota", "Toyota" }, { "JTN", "Toyota", "Toyota" },
    { "JTH", "Lexus", "Lexus" }, { "JTJ", "Lexus", "Lexus" }, { "2T2", "Lexus", "Lexus" },
    /* Ford / Lincoln / Mercury */
    { "1FA", "Ford", "Ford" }, { "1FB", "Ford", "Ford" }, { "1FC", "Ford", "Ford" },
    { "1FD", "Ford", "Ford" }, { "1FM", "Ford", "Ford" }, { "1FT", "Ford", "Ford" },
    { "2FA", "Ford", "Ford" }, { "2FM", "Ford", "Ford" }, { "2FT", "Ford", "Ford" },
    { "3FA", "Ford", "Ford" }, { "3FM", "Ford", "Ford" }, { "3FT", "Ford", "Ford" },
    { "1LN", "Lincoln", "Lincoln" }, { "2LM", "Lincoln", "Lincoln" }, { "5LM", "Lincoln", "Lincoln" },
    { "1ME", "Mercury", "Mercury" }, { "2ME", "Mercury", "Mercury" }, { "4M2", "Mercury", "Mercury" },
    /* GM brands */
    { "1G1", "Chevrolet", "Chevrolet" }, { "1GC", "Chevrolet", "Chevrolet" },
    { "1GN", "Chevrolet", "Chevrolet" }, { "2G1", "Chevrolet", "Chevrolet" },
    { "3G1", "Chevrolet", "Chevrolet" }, { "3GC", "Chevrolet", "Chevrolet" },
    { "1GT", "GMC", "GMC" }, { "2GT", "GMC", "GMC" }, { "3GT", "GMC", "GMC" }, { "1GK", "GMC", "GMC" },
    { "1G4", "Buick", "Buick" }, { "2G4", "Buick", "Buick" },
    { "1G6", "Cadillac", "Cadillac" }, { "1GY", "Cadillac", "Cadillac" },
    { "1G2", "Pontiac", "Pontiac" }, { "2G2", "Pontiac", "Pontiac" },
    { "1G3", "Oldsmobile", "Oldsmobile" }, { "2G3", "Oldsmobile", "Oldsmobile" },
    { "1G8", "Saturn", "Saturn" }, { "5GZ", "Saturn", "Saturn" },
    /* Chrysler group: shared WMIs */
    { "1C3", CHRYSLER_GROUP, "Chrysler" }, { "2C3", CHRYSLER_GROUP, "Chrysler" },
    { "1C4", CHRYSLER_GROUP, "Chrysler" }, { "2C4", CHRYSLER_GROUP, "Chrysler" },
    { "1C6", CHRYSLER_GROUP, "Chrysler" }, { "3C6", CHRYSLER_GROUP, "Chrysler" },
    { "1B3", "Dodge", "Dodge" }, { "1B7", "Dodge", "Dodge" }, { "1D7", "Dodge", "Dodge" },
    { "2B3", "Dodge", "Dodge" }, { "3D7", "Dodge", "Dodge" },
    { "1J4", "Jeep", "Jeep" }, { "1J8", "Jeep", "Jeep" },
    { "1P3", "Plymouth", "Plymouth" },
    /* Nissan / Infiniti */
    { "1N4", "Nissan", "Nissan" }, { "1N6", "Nissan", "Nissan" }, { "3N1", "Nissan", "Nissan" },
    { "5N1", "Nissan", "Nissan" }, { "JN1", "Nissan", "Nissan" }, { "JN8", "Nissan", "Nissan" },
    { "JNK", "Infiniti", "Infiniti" }, { "JNR", "Infiniti", "Infiniti" }, { "5N3", "Infiniti", "Infiniti" },
    /* Others */
    { "JM1", "Mazda", "Mazda" }, { "JM3", "Mazda", "Mazda" }, { "4F2", "Mazda", "Mazda" },
    { "4F4", "Mazda", "Mazda" },
    { "JA3", "Mitsubishi", "Mitsubishi" }, { "JA4", "Mitsubishi", "Mitsubishi" },
    { "4A3", "Mitsubishi", "Mitsubishi" }, { "4A4", "Mitsubishi", "Mitsubishi" },
    { "JF1", "Subaru", "Subaru" }, { "JF2", "Subaru", "Subaru" }, { "4S3", "Subaru", "Subaru" },
    { "4S4", "Subaru", "Subaru" },
    { "KNA", "Kia", "Kia" }, { "KND", "Kia", "Kia" }, { "5XY", "Kia", "Kia" },
    { "JS2", "Suzuki", "Suzuki" }, { "JS3", "Suzuki", "Suzuki" }, { "2S3", "Suzuki", "Suzuki" },
    { "WVW", "Volkswagen", "Volkswagen" }, { "WVG", "Volkswagen", "Volkswagen" },
    { "3VW", "Volkswagen", "Volkswagen" }, { "1VW", "Volkswagen", "Volkswagen" },
    { "WAU", "Audi", "Audi" }, { "WA1", "Audi", "Audi" },
    { "WBA", "BMW", "BMW" }, { "WBS", "BMW", "BMW" }, { "WBX", "BMW", "BMW" },
    { "5UX", "BMW", "BMW" }, { "4US", "BMW", "BMW" },
    { "WDB", "Mercedes-Benz", "Mercedes-Benz" }, { "WDC", "Mercedes-Benz", "Mercedes-Benz" },
    { "WDD", "Mercedes-Benz", "Mercedes-Benz" }, { "4JG", "Mercedes-Benz", "Mercedes-Benz" },
    { "SAJ", "Jaguar", "Jaguar" },
};

bool obd_vin_maker(const char *vin, obd_vin_maker_t *out)
{
    out->maker = NULL;
    out->dtc_make = NULL;
    if (vin == NULL || strlen(vin) < 3) {
        return false;
    }
    for (size_t i = 0; i < sizeof wmis / sizeof wmis[0]; i++) {
        if (strncmp(vin, wmis[i].wmi, 3) == 0) {
            out->maker = wmis[i].maker;
            out->dtc_make = wmis[i].dtc_make;
            return true;
        }
    }
    return false;
}

/* vPIC's key match: pattern against the start of keys; '*' is any one
 * character, "[...]" a set (with ranges such as "A-H"). */
static bool vin_keys_match(const char *pat, const char *keys)
{
    for (; *pat != '\0'; pat++, keys++) {
        if (*keys == '\0') {
            return false;
        }
        if (*pat == '*') {
            continue;
        }
        if (*pat == '[') {
            bool in = false;
            pat++;
            for (; *pat != '\0' && *pat != ']'; pat++) {
                if (pat[1] == '-' && pat[2] != '\0' && pat[2] != ']') {
                    in = in || (*keys >= pat[0] && *keys <= pat[2]);
                    pat += 2;
                } else {
                    in = in || *keys == *pat;
                }
            }
            if (!in || *pat == '\0') {
                return false;
            }
            continue;
        }
        if (*pat != *keys) {
            return false;
        }
    }
    return true;
}

bool obd_vin_model(const char *vin, int model_year, obd_vin_model_t *out)
{
    if (vin == NULL || strlen(vin) != OBD_VIN_LEN || model_year < VIN_MODEL_FIRST_YEAR ||
        model_year > VIN_MODEL_FIRST_YEAR + VIN_MODEL_OPEN_YEAR) {
        return false;
    }
    char wmi[7] = { vin[0], vin[1], vin[2], '\0' };
    if (vin[2] == '9') {
        memcpy(wmi + 3, vin + 11, 3);
        wmi[6] = '\0';
    }
    char keys[15];
    memcpy(keys, vin + 3, 5);
    keys[5] = '|';
    memcpy(keys + 6, vin + 9, 8);
    keys[14] = '\0';

    int lo = 0, hi = vin_model_wmi_count - 1;
    const vin_model_wmi_t *w = NULL;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(vin_model_wmis[mid].wmi, wmi);
        if (c == 0) {
            w = &vin_model_wmis[mid];
            break;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (w == NULL) {
        return false;
    }
    int y = model_year - VIN_MODEL_FIRST_YEAR;
    for (uint32_t i = 0; i < w->count; i++) {
        const vin_model_rule_t *r = &vin_model_rules[w->first + i];
        if (y < r->year_from || (r->year_to != VIN_MODEL_OPEN_YEAR && y > r->year_to)) {
            continue;
        }
        if (vin_keys_match(vin_model_keys[r->keys], keys)) {
            if (out != NULL) {
                out->make = vin_model_names[r->name].make;
                out->model = vin_model_names[r->name].model;
            }
            return true;
        }
    }
    return false;
}
