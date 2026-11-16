/* Tests for source/data/trim_view: bars, zones and hints. */
#include <string.h>

#include "harness.h"
#include "data/trim_view.h"

static char bar[TRIM_BAR_WIDTH + 1];

static void test_trim_zone(void)
{
    CHECK_EQ_INT(trim_zone(0.0f), TRIM_ZONE_GREEN);
    CHECK_EQ_INT(trim_zone(10.0f), TRIM_ZONE_GREEN);
    CHECK_EQ_INT(trim_zone(-10.0f), TRIM_ZONE_GREEN);
    CHECK_EQ_INT(trim_zone(10.5f), TRIM_ZONE_AMBER);
    CHECK_EQ_INT(trim_zone(-20.0f), TRIM_ZONE_AMBER);
    CHECK_EQ_INT(trim_zone(20.5f), TRIM_ZONE_RED);
    CHECK_EQ_INT(trim_zone(-100.0f), TRIM_ZONE_RED);
}

static void test_trim_bar(void)
{
    trim_bar(0.0f, TRIM_BAR_WIDTH, bar);
    CHECK_EQ_STR(bar, "............|............");
    trim_bar(0.8f, TRIM_BAR_WIDTH, bar);         /* under half a cell: nothing */
    CHECK_EQ_STR(bar, "............|............");
    trim_bar(4.2f, TRIM_BAR_WIDTH, bar);         /* 2 cells of 2 % */
    CHECK_EQ_STR(bar, "............|##..........");
    trim_bar(-6.0f, TRIM_BAR_WIDTH, bar);
    CHECK_EQ_STR(bar, ".........###|............");
    trim_bar(24.0f, TRIM_BAR_WIDTH, bar);        /* full, still in range */
    CHECK_EQ_STR(bar, "............|############");
    trim_bar(40.0f, TRIM_BAR_WIDTH, bar);        /* over: the end cell says so */
    CHECK_EQ_STR(bar, "............|###########>");
    trim_bar(-99.0f, TRIM_BAR_WIDTH, bar);
    CHECK_EQ_STR(bar, "<###########|............");
    CHECK_EQ_INT((int)strlen(bar), TRIM_BAR_WIDTH);
}

static trim_state_t two_banks(float st1, float lt1, float st2, float lt2, float rpm)
{
    trim_state_t s;
    memset(&s, 0, sizeof s);
    s.banks = 2;
    s.bank[0] = (trim_bank_t){ true, true, st1, lt1 };
    s.bank[1] = (trim_bank_t){ true, true, st2, lt2 };
    s.have_rpm = true;
    s.rpm = rpm;
    s.have_speed = true;
    return s;
}

static bool has(const char **h, int n, const char *start)
{
    for (int i = 0; i < n; i++) {
        if (strncmp(h[i], start, strlen(start)) == 0) {
            return true;
        }
    }
    return false;
}

static void test_trim_hints(void)
{
    const char *h[TRIM_HINTS_MAX];
    float t = 0.0f;
    trim_bank_t only_short = { true, false, 3.0f, 0.0f };
    CHECK(trim_total(&only_short, &t) && t == 3.0f);

    /* Normal Ridgeline idle. */
    trim_state_t s = two_banks(1.6f, 2.3f, -0.8f, 3.9f, 700.0f);
    CHECK(trim_at_idle(&s));
    int n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Trims within 10 %: normal."));

    /* Both lean at idle: vacuum leak; the same while driving: fuel delivery. */
    s = two_banks(9.0f, 8.0f, 10.0f, 6.0f, 750.0f);
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Both banks lean at idle"));
    CHECK(!has(h, n, "Trims within"));
    s = two_banks(9.0f, 8.0f, 10.0f, 6.0f, 2500.0f);
    s.speed_kmh = 80.0f;
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Both banks lean: possible fuel delivery"));

    /* One bank only. */
    s = two_banks(1.0f, 1.0f, 8.0f, 9.0f, 700.0f);
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Bank 2 only lean"));
    s = two_banks(-14.0f, -2.0f, 1.0f, 0.0f, 700.0f);
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Bank 1 only rich"));
    s = two_banks(-9.0f, -8.0f, -12.0f, -1.0f, 700.0f);
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Both banks rich"));

    /* One bank (Tracker): lean at idle. */
    memset(&s, 0, sizeof s);
    s.banks = 1;
    s.bank[0] = (trim_bank_t){ true, false, 15.0f, 0.0f };
    s.have_rpm = true;
    s.rpm = 800.0f;
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Lean at idle"));
    CHECK(has(h, n, "Long-term trims learn"));   /* long-term not read yet */

    /* Engine off, nothing yet, no trims at all. */
    s = two_banks(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK_EQ_INT(n, 1);
    CHECK(has(h, n, "Start the engine"));
    memset(&s, 0, sizeof s);
    s.banks = 2;
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "Waiting for the first readings"));
    s.banks = 0;
    n = trim_hints(&s, h, TRIM_HINTS_MAX);
    CHECK(has(h, n, "This car doesn't report fuel trims"));
    CHECK_EQ_INT(trim_hints(&s, h, 0), 0);
}

void suite_trim(void)
{
    RUN_TEST(test_trim_zone);
    RUN_TEST(test_trim_bar);
    RUN_TEST(test_trim_hints);
}
