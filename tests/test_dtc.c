/* Generic DTC descriptions (source/obd/dtc). */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "obd/dtc.h"
#include "obd/dtc_desc_data.h"

static void test_key(void)
{
    uint16_t k = 0;
    CHECK(obd_dtc_key("P0171", &k) && k == 0x0171);
    CHECK(obd_dtc_key("c0035", &k) && k == 0x4035);
    CHECK(obd_dtc_key("B0001", &k) && k == 0x8001);
    CHECK(obd_dtc_key("U0100", &k) && k == 0xC100);
    CHECK(obd_dtc_key("P3FFF", &k) && k == 0x3FFF);
    CHECK(!obd_dtc_key("P4000", &k));   /* first digit is 2 bits */
    CHECK(!obd_dtc_key("X0171", &k));
    CHECK(!obd_dtc_key("P017", &k));
    CHECK(!obd_dtc_key("P01711", &k));
    CHECK(!obd_dtc_key("P0G71", &k));
    CHECK(!obd_dtc_key(NULL, &k));
}

static void test_generic_ranges(void)
{
    CHECK(obd_dtc_is_generic("P0171"));
    CHECK(obd_dtc_is_generic("P2096"));
    CHECK(obd_dtc_is_generic("P3400"));
    CHECK(!obd_dtc_is_generic("P1456"));  /* Honda EVAP on the Ridgeline; other makes differ */
    CHECK(!obd_dtc_is_generic("P3000"));
    CHECK(!obd_dtc_is_generic("P33FF"));
    CHECK(obd_dtc_is_generic("B0001") && obd_dtc_is_generic("B3000"));
    CHECK(!obd_dtc_is_generic("B1000") && !obd_dtc_is_generic("B2000"));
    CHECK(obd_dtc_is_generic("C0035") && !obd_dtc_is_generic("C1234"));
    CHECK(obd_dtc_is_generic("U0100") && !obd_dtc_is_generic("U1000"));
}

static void test_descriptions(void)
{
    CHECK_EQ_STR(obd_dtc_description("P0171"), "System Too Lean Bank 1");
    CHECK_EQ_STR(obd_dtc_description("p0420"), "Catalyst System Efficiency Below Threshold Bank 1");
    CHECK_EQ_STR(obd_dtc_description("P0300"), "Random/Multiple Cylinder Misfire Detected");
    CHECK(obd_dtc_description("U0100") != NULL);
    CHECK(obd_dtc_description("P1456") == NULL);
    CHECK(obd_dtc_description("bad") == NULL);

    CHECK_EQ_STR(obd_dtc_text("P0171"), "System Too Lean Bank 1");
    CHECK_EQ_STR(obd_dtc_text("P1456"), "Manufacturer-specific code");
    CHECK_EQ_STR(obd_dtc_text("Z9999"), "Not a valid code");
    /* A generic code the table doesn't have (if any) gets a fallback. */
    const char *t = obd_dtc_text("P3FFF");
    CHECK(t != NULL && strlen(t) > 0);
}

static void test_table_sorted_and_clean(void)
{
    CHECK(obd_dtc_description_count() > 9000);
    CHECK_EQ_INT((int)obd_dtc_description_count(), (int)dtc_desc_count);
    for (unsigned i = 0; i < dtc_desc_count; i++) {
        if (i > 0 && dtc_desc_table[i - 1].key >= dtc_desc_table[i].key) {
            gb_check_failf(__FILE__, __LINE__, "table not sorted at %u", i);
            break;
        }
        const char *s = dtc_desc_table[i].text;
        size_t n = strlen(s);
        if (n == 0 || n > 120) {
            gb_check_failf(__FILE__, __LINE__, "entry %u has length %d", i, (int)n);
            break;
        }
        for (size_t j = 0; j < n; j++) {
            if ((unsigned char)s[j] < 32 || (unsigned char)s[j] > 126) {
                gb_check_failf(__FILE__, __LINE__, "entry %u has a non-ASCII byte", i);
                return;
            }
        }
    }
}

static void test_makes(void)
{
    CHECK_EQ_INT(obd_dtc_make_count(), 32);
    int honda = obd_dtc_make_find("Honda");
    CHECK(honda >= 0);
    CHECK_EQ_STR(obd_dtc_make_name(honda), "Honda");
    CHECK_EQ_INT(obd_dtc_make_find("HONDA"), honda);
    CHECK_EQ_INT(obd_dtc_make_find("chevy"), obd_dtc_make_find("Chevrolet"));
    CHECK(obd_dtc_make_find("Chevrolet") >= 0);
    CHECK_EQ_INT(obd_dtc_make_find("VW"), obd_dtc_make_find("Volkswagen"));
    CHECK_EQ_INT(obd_dtc_make_find("Suzuki Sidekick"), -1);
    CHECK_EQ_INT(obd_dtc_make_find(NULL), -1);
    CHECK(obd_dtc_make_name(-1) == NULL && obd_dtc_make_name(32) == NULL);
}

static void test_manufacturer_descriptions(void)
{
    int honda = obd_dtc_make_find("Honda");
    int ford = obd_dtc_make_find("Ford");
    /* The same code means different things on different makes. */
    CHECK_EQ_STR(obd_dtc_text_for("P1456", honda),
                 "Evaporative Emission Control System Leak Detected Fuel Tank System");
    CHECK_EQ_STR(obd_dtc_text_for("P1000", ford), "OBD System Readiness Test Not Complete");
    CHECK(obd_dtc_description_for("P1456", ford) == NULL ||
          strcmp(obd_dtc_description_for("P1456", ford),
                 obd_dtc_description_for("P1456", honda)) != 0);
    /* Unknown make or code not listed for the make. */
    CHECK_EQ_STR(obd_dtc_text_for("P1456", -1), "Manufacturer-specific code");
    CHECK_EQ_STR(obd_dtc_text_for("P1FFF", honda), "Manufacturer-specific code");
    /* Generic codes ignore the make. */
    CHECK_EQ_STR(obd_dtc_text_for("P0171", honda), "System Too Lean Bank 1");
    CHECK_EQ_STR(obd_dtc_text_for("P0171", -1), "System Too Lean Bank 1");
    CHECK_EQ_STR(obd_dtc_text_for("nope", honda), "Not a valid code");
}

static void test_mfr_table_sorted_and_clean(void)
{
    CHECK(dtc_mfr_count > 2000);
    for (unsigned i = 0; i < dtc_mfr_count; i++) {
        const dtc_mfr_desc_t *e = &dtc_mfr_table[i];
        if (i > 0 && dtc_mfr_table[i - 1].key > e->key) {
            gb_check_failf(__FILE__, __LINE__, "manufacturer table not sorted at %u", i);
            return;
        }
        if (e->makes == 0 || (e->makes >> dtc_make_count) != 0) {
            gb_check_failf(__FILE__, __LINE__, "entry %u has a bad make mask", i);
            return;
        }
        uint16_t key = e->key;
        char code[6];
        snprintf(code, sizeof code, "%c%04X", "PCBU"[key >> 14], key & 0x3FFF);
        if (obd_dtc_is_generic(code)) {
            gb_check_failf(__FILE__, __LINE__, "entry %u (%s) is in a generic range", i, code);
            return;
        }
        size_t n = strlen(e->text);
        if (n == 0 || n > 128) {
            gb_check_failf(__FILE__, __LINE__, "entry %u has length %d", i, (int)n);
            return;
        }
    }
}

void suite_dtc(void)
{
    gb_suite("dtc");
    RUN_TEST(test_key);
    RUN_TEST(test_generic_ranges);
    RUN_TEST(test_descriptions);
    RUN_TEST(test_table_sorted_and_clean);
    RUN_TEST(test_makes);
    RUN_TEST(test_manufacturer_descriptions);
    RUN_TEST(test_mfr_table_sorted_and_clean);
}
