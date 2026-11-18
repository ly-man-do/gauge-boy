/* The garage card (source/data/garage): cars, titles, nicknames, logged
 * distance and its cache. VINs here are made up. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "data/garage.h"

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

void suite_garage(void)
{
    RUN_TEST(test_garage_cars);
    RUN_TEST(test_garage_distance);
}
