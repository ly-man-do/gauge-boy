/* Saved engine-off MAP per car (source/data/baro_store) and its use. */
#include <string.h>

#include "harness.h"
#include "data/baro_store.h"
#include "obd/obd_live.h"

static void test_keyval_store(void)
{
    static char out[1024];
    size_t n = keyval_store_set("", "car,km,date\n", "Geo/Tracker/1996", 48212.4f, "2026-11-18", out,
                                sizeof out);
    CHECK(n > 0);
    CHECK_EQ_STR(out, "car,km,date\nGeo/Tracker/1996,48212.4,2026-11-18\n");
    float km = 0.0f;
    char date[16];
    CHECK(baro_store_get(out, "Geo/Tracker/1996", &km, date, sizeof date));
    CHECK(km > 48212.3f && km < 48212.5f);
    CHECK_EQ_STR(date, "2026-11-18");
}

static void test_baro_store(void)
{
    static obd_diag_t d;
    memset(&d, 0, sizeof d);
    char key[64];
    baro_store_key(&d, "", "", 0, key, sizeof key);
    CHECK_EQ_STR(key, "");                                  /* nothing identifies it */
    baro_store_key(&d, "Geo", "Tracker", 1996, key, sizeof key);
    CHECK_EQ_STR(key, "Geo/Tracker/1996");
    d.have_vin = true;
    strcpy(d.vin, "2HJYK16528H000123");
    baro_store_key(&d, "Geo", "Tracker", 1996, key, sizeof key);
    CHECK_EQ_STR(key, "2HJYK16528H000123");                 /* the VIN wins */

    static char text[4096], out[4096];
    size_t n = baro_store_set("", key, 98.04f, "2026-11-17", out, sizeof out);
    CHECK(n > 0);
    CHECK_EQ_STR(out, "car,kpa,date\n2HJYK16528H000123,98.0,2026-11-17\n");
    strcpy(text, out);
    baro_store_set(text, "Geo/Tracker/1996", 97.5f, "2026-11-15", out, sizeof out);
    strcpy(text, out);
    baro_store_set(text, key, 96.2f, "2026-11-18", out, sizeof out);   /* replaced, moved last */
    CHECK_EQ_STR(out, "car,kpa,date\nGeo/Tracker/1996,97.5,2026-11-15\n"
                      "2HJYK16528H000123,96.2,2026-11-18\n");
    float kpa = 0.0f;
    char date[16];
    CHECK(baro_store_get(out, key, &kpa, date, sizeof date));
    CHECK(kpa > 96.1f && kpa < 96.3f);
    CHECK_EQ_STR(date, "2026-11-18");
    CHECK(!baro_store_get(out, "nope", &kpa, NULL, 0));
    CHECK(!baro_store_get(out, "", &kpa, NULL, 0));

    /* Past BARO_STORE_MAX cars the oldest go. */
    text[0] = '\0';
    for (int i = 0; i < BARO_STORE_MAX + 3; i++) {
        char k[16];
        snprintf(k, sizeof k, "car%d", i);
        baro_store_set(text, k, 90.0f + i, "2026-11-17", out, sizeof out);
        strcpy(text, out);
    }
    CHECK(!baro_store_get(text, "car0", &kpa, NULL, 0));
    CHECK(baro_store_get(text, "car18", &kpa, NULL, 0) && kpa == 108.0f);
    int lines = 0;
    for (const char *p = text; *p; p++) {
        lines += *p == '\n';
    }
    CHECK_EQ_INT(lines, BARO_STORE_MAX + 1);                /* header + cars */
}

static void test_live_seed_baro(void)
{
    static obd_live_t lv;
    memset(&lv, 0, sizeof lv);
    lv.count = 1;
    lv.pid[0].pid = 0x0B;
    lv.pid[0].have = true;
    lv.pid[0].value = 30.0f;                                /* MAP at idle */
    float vac = 0.0f;
    CHECK(!obd_live_vacuum(&lv, &vac));                     /* no baro yet */
    obd_live_seed_baro(&lv, 40.0f);                         /* not a baro: ignored */
    CHECK(!lv.have_engine_off_map);
    obd_live_seed_baro(&lv, 98.0f);
    CHECK(lv.have_engine_off_map && lv.engine_off_map_saved);
    CHECK(obd_live_vacuum(&lv, &vac) && vac > 67.9f && vac < 68.1f);
    obd_live_seed_baro(&lv, 90.0f);                         /* already have one */
    CHECK(lv.engine_off_map_kpa == 98.0f);
}

void suite_baro_store(void)
{
    RUN_TEST(test_keyval_store);
    RUN_TEST(test_baro_store);
    RUN_TEST(test_live_seed_baro);
}
