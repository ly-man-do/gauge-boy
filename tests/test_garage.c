/* The garage card (source/data/garage): cars, titles, nicknames, logged
 * distance and its cache. VINs here are made up. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "data/garage.h"
#include "data/mileage.h"
#include "data/baro_store.h"

static garage_t g;

static void test_garage_cars(void)
{
    char title[48];
    garage_title("2HJYK16528H000123", 2026, title, sizeof title);
    CHECK_EQ_STR(title, "2008 Honda Ridgeline");
    garage_title("Geo/Tracker/1996", 2026, title, sizeof title);
    CHECK_EQ_STR(title, "1996 Geo Tracker");
    garage_title("Geo//0", 2026, title, sizeof title);
    CHECK_EQ_STR(title, "Geo");

    memset(&g, 0, sizeof g);
    CHECK_EQ_INT(garage_add(&g, "2HJYK16528H000123", 2026), 0);
    garage_add_from_csv(&g, "car,run,seconds,extra,date\n"
                            "2HJYK16528H000123,0-60,8.89,,2026-11-18\n"
                            "Geo/Tracker/1996,revsync,3300.00,,2026-11-18\n", 2026);
    garage_add_from_csv(&g, "car,kpa,date\nGeo/Tracker/1996,97.5,2026-11-15\n", 2026);
    CHECK_EQ_INT(g.count, 2);                             /* no repeats */
    CHECK_EQ_STR(g.car[0].vin, "2HJYK16528H000123");
    CHECK_EQ_STR(g.car[1].vin, "");
    CHECK_EQ_INT(garage_add(&g, "", 2026), -1);

    static char a[1024], b[1024];
    size_t n = garage_set_nickname("", "Geo/Tracker/1996", "Little, Teal", a, sizeof a);
    CHECK(n > 0);
    CHECK_EQ_STR(a, "car,nickname\nGeo/Tracker/1996,Little Teal\n");   /* comma dropped */
    garage_set_nickname(a, "2HJYK16528H000123", "Big Red", b, sizeof b);
    garage_read_nicknames(&g, b);
    CHECK_EQ_STR(g.car[0].nickname, "Big Red");
    CHECK_EQ_STR(g.car[1].nickname, "Little Teal");
    garage_set_nickname(b, "Geo/Tracker/1996", "", a, sizeof a);       /* removed */
    CHECK_EQ_STR(a, "car,nickname\n2HJYK16528H000123,Big Red\n");
    CHECK_EQ_INT(garage_set_nickname(b, "x", "y", a, 8), 0);           /* too small */

    CHECK_EQ_INT(garage_find_drive(&g, "2HJYK16528H000123", "anything"), 0);
    CHECK_EQ_INT(garage_find_drive(&g, "", "1996 Geo Tracker (set in SETUP)"), 1);
    CHECK_EQ_INT(garage_find_drive(&g, "", "2023 Subaru Ascent"), -1);
}

static void test_garage_distance(void)
{
    static char text[64 * 1024];
    size_t used = 0;
    used += (size_t)snprintf(text + used, sizeof text - used,
                             "# Gauge Boy drive log\n# version,0.3.0\n# car,1996 Geo Tracker\n"
                             "time_s,RPM (rpm),Speed (mph),Battery (V)\n");
    /* 60 mph for 60 s (1 mile), a 10 s gap (not counted), 30 mph for 60 s. */
    for (int k = 0; k <= 300; k++) {
        used += (size_t)snprintf(text + used, sizeof text - used, "%.3f,2000,60,13.9\n", k * 0.2);
    }
    for (int k = 0; k <= 300; k++) {
        used += (size_t)snprintf(text + used, sizeof text - used, "%.3f,1500,30,,\n", 70.0 + k * 0.2);
    }
    used += (size_t)snprintf(text + used, sizeof text - used, "130.2,15");   /* cut off */
    drive_dist_t dd;
    CHECK(drive_dist_begin(&dd, text, used));
    CHECK_EQ_INT(dd.col, 1);
    for (char *line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        drive_dist_line(&dd, line);
    }
    CHECK(fabs(dd.km - 1.5 * 1.609344) < 0.01);

    CHECK(!drive_dist_begin(&dd, "# Gauge Boy drive log\ntime_s,RPM (rpm)\n", 37));

    static char cache[1024];
    size_t c = 0;
    CHECK(garage_cache_add(cache, sizeof cache, &c, "drive-1.csv", 1234, 2.414, "",
                           "1996 Geo Tracker, (set in SETUP)"));
    CHECK(garage_cache_add(cache, sizeof cache, &c, "drive-2.csv", 99, 0.5, "2HJYK16528H000123",
                           "2008 Honda Ridgeline"));
    double km = 0;
    char vin[18], car[64];
    CHECK(garage_cache_get(cache, "drive-1.csv", 1234, &km, vin, sizeof vin, car, sizeof car));
    CHECK(fabs(km - 2.414) < 1e-6);
    CHECK_EQ_STR(vin, "");
    CHECK_EQ_STR(car, "1996 Geo Tracker (set in SETUP)");
    CHECK(!garage_cache_get(cache, "drive-1.csv", 1300, &km, NULL, 0, NULL, 0));   /* grew */
    CHECK(garage_cache_get(cache, "drive-2.csv", 99, &km, vin, sizeof vin, NULL, 0));
    CHECK_EQ_STR(vin, "2HJYK16528H000123");
    size_t full = c;
    CHECK(!garage_cache_add(cache, full + 10, &c, "drive-3.csv", 1, 1.0, "", ""));
    CHECK_EQ_INT((long long)c, (long long)full);
}

static void test_mileage_log(void)
{
    static char a[2048], b[2048];
    /* $3.459 a gallon is 0.9138 a litre. */
    mileage_entry_t e = { "2026-11-18", 77140.2, 77502.6, 30.28, 3.459 / MILEAGE_L_GAL };
    size_t n = mileage_add("", "Geo/Tracker/1996", &e, a, sizeof a);
    CHECK(n > 0);
    CHECK_EQ_STR(a, "car,date,start_km,end_km,fuel_l,price_l\n"
                    "Geo/Tracker/1996,2026-11-18,77140.2,77502.6,30.280,0.9138\n");
    mileage_entry_t e2 = { "2026-11-20", 77502.6, 77900.0, 33.10, 0.0 };   /* no price */
    mileage_add(a, "Geo/Tracker/1996", &e2, b, sizeof b);
    mileage_entry_t other = { "2026-11-19", 1000.0, 1400.0, 40.0, 0.0 };
    mileage_add(b, "2HJYK16528H000123", &other, a, sizeof a);

    static mileage_list_t list;
    CHECK_EQ_INT(mileage_read(a, "Geo/Tracker/1996", &list), 2);
    CHECK(fabs(list.km - 759.8) < 0.01 && fabs(list.fuel_l - 63.38) < 0.001);
    CHECK_EQ_STR(list.e[1].date, "2026-11-20");
    CHECK(fabs(list.cost - 27.67) < 0.01);                /* 8.0 gal at $3.459; the second has none */
    CHECK(fabs(list.cost_km - 362.4) < 0.01);
    CHECK(fabs(mileage_cost(&list.e[0]) - 27.67) < 0.01 && mileage_cost(&list.e[1]) == 0.0);

    /* A line from before the price column reads with no price. */
    CHECK_EQ_INT(mileage_read("car,date,start_km,end_km,fuel_l\nX,2026-01-01,10,110,9.5\n", "X",
                              &list), 1);
    CHECK(list.e[0].price_l == 0.0 && list.cost == 0.0 && fabs(list.fuel_l - 9.5) < 1e-9);
    CHECK_EQ_INT(mileage_read(a, "Geo/Tracker/1996", &list), 2);
    double mpg = 0, l100 = 0;
    CHECK(mileage_mpg(list.e[0].end_km - list.e[0].start_km, list.e[0].fuel_l, &mpg));
    CHECK(fabs(mpg - 28.15) < 0.02);                      /* 225.2 mi on 8.0 gal */
    CHECK(mileage_l100(400.0, 40.0, &l100) && fabs(l100 - 10.0) < 1e-9);
    CHECK(!mileage_mpg(0.0, 10.0, &mpg) && !mileage_l100(100.0, 0.0, &l100));

    bool removed = false;
    n = mileage_delete_last(a, "Geo/Tracker/1996", &removed, b, sizeof b);
    CHECK(removed && n > 0);
    CHECK_EQ_INT(mileage_read(b, "Geo/Tracker/1996", &list), 1);
    CHECK_EQ_INT(mileage_read(b, "2HJYK16528H000123", &list), 1);   /* other cars untouched */
    mileage_delete_last(b, "nobody", &removed, a, sizeof a);
    CHECK(!removed);
    CHECK_EQ_STR(a, b);

    /* Edit or delete any entry by its place among the car's entries. */
    mileage_entry_t x = { "2026-11-21", 100.0, 200.0, 10.0, 0.0 }, y = { "2026-11-22", 200.0, 300.0, 9.0, 0.0 };
    mileage_add("", "A", &x, a, sizeof a);
    mileage_add(a, "B", &other, b, sizeof b);
    mileage_add(b, "A", &y, a, sizeof a);                    /* A: x, y; B between them */
    mileage_entry_t fixed = { "2026-11-22", 200.0, 310.0, 9.5, 1.0 };
    bool found = false;
    n = mileage_rewrite(a, "A", 1, &fixed, &found, b, sizeof b);   /* A's second: y */
    CHECK(found && n > 0);
    CHECK_EQ_INT(mileage_read(b, "A", &list), 2);
    CHECK(list.e[1].end_km == 310.0 && list.e[1].price_l == 1.0 && list.e[0].end_km == 200.0);
    CHECK_EQ_INT(mileage_read(b, "B", &list), 1);           /* B untouched */
    n = mileage_rewrite(b, "A", 0, NULL, &found, a, sizeof a);     /* delete A's first */
    CHECK(found && n > 0);
    CHECK_EQ_INT(mileage_read(a, "A", &list), 1);
    CHECK(list.e[0].end_km == 310.0 && list.first == 0);
    mileage_rewrite(a, "A", 5, NULL, &found, b, sizeof b);
    CHECK(!found);
    CHECK_EQ_STR(b, a);                                      /* nothing changed */

    double v = 0;
    CHECK(mileage_parse("29,958", &v) && v == 29958.0);
    CHECK(mileage_parse(" 14.25 ", &v) && fabs(v - 14.25) < 1e-9);
    CHECK(!mileage_parse("12a", &v) && !mileage_parse("", &v) && !mileage_parse("-5", &v));
    CHECK(!mileage_parse("1.2.3", &v));
    CHECK(mileage_check(100, 200, 10) == NULL);
    CHECK(mileage_check(200, 100, 10) != NULL);
    CHECK(mileage_check(100, 200, 0) != NULL);
    CHECK(mileage_check(100, 100 + 9000 * MILEAGE_KM_MI, 10) != NULL);

    /* The trip meter's file: a reading per car. */
    n = keyval_store_set("", TRIP_HEADER, "Geo/Tracker/1996", 77502.6f, "2026-11-20", a, sizeof a);
    float km = 0;
    CHECK(n > 0 && baro_store_get(a, "Geo/Tracker/1996", &km, NULL, 0));
    CHECK(fabsf(km - 77502.6f) < 0.01f);
}

void suite_garage(void)
{
    RUN_TEST(test_mileage_log);
    RUN_TEST(test_garage_cars);
    RUN_TEST(test_garage_distance);
}
