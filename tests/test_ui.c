/* Tests for source/ui: the HOME bottom screen layout and touch zones. */
#include <string.h>

#include "harness.h"
#include "ui/home_layout.h"

static bool overlap(home_rect_t a, home_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_home_rects_fit_and_dont_overlap(void)
{
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        home_rect_t r = home_item_rect((home_item_t)i);
        home_rect_t t = home_item_touch((home_item_t)i);
        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= HOME_SCREEN_W);
        if (i != HOME_CLOSE) {
            CHECK(r.y + r.h + 2 <= HOME_BAR_Y);      /* shadow clears the bar */
        }
        if (i != HOME_CLOSE) {
            CHECK(t.w >= 40 && t.h >= 40);           /* touch rule: 40x40 px */
        }
        for (int j = i + 1; j < HOME_ITEM_COUNT; j++) {
            CHECK(!overlap(t, home_item_touch((home_item_t)j)));
        }
    }
    /* symmetric margins */
    home_rect_t a = home_item_rect(HOME_LIVE), c = home_item_rect(HOME_TRIM);
    CHECK_EQ_INT(a.x, HOME_SCREEN_W - (c.x + c.w));
    home_rect_t cel = home_item_rect(HOME_GARAGE), ad = home_item_rect(HOME_ADAPTER);
    CHECK_EQ_INT(cel.x, a.x);
    CHECK_EQ_INT(ad.x + ad.w, c.x + c.w);
}

static void test_home_hit(void)
{
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        home_rect_t r = home_item_rect((home_item_t)i);
        CHECK_EQ_INT(home_hit(r.x + r.w / 2, r.y + r.h / 2), i);
        CHECK_EQ_INT(home_hit(r.x, r.y), i);
        CHECK_EQ_INT(home_hit(r.x + r.w - 1, r.y + r.h - 1), i);
    }
    CHECK_EQ_INT(home_hit(20, 0), HOME_GARAGE);         /* strip above the status card */
    CHECK_EQ_INT(home_hit(300, 39), HOME_ADAPTER);
    CHECK_EQ_INT(home_hit(5, 100), -1);              /* margin */
    CHECK_EQ_INT(home_hit(110, 100), -1);            /* gap */
    CHECK_EQ_INT(home_hit(160, 225), HOME_CLOSE);    /* bar: closes the app */
    CHECK_EQ_INT(home_hit(0, HOME_SCREEN_H - 1), HOME_CLOSE);
    CHECK_EQ_INT(home_hit(5, HOME_BAR_Y - 1), -1);    /* margin above the bar */
    home_rect_t bar = home_item_touch(HOME_CLOSE);
    CHECK_EQ_INT(bar.h, 32);   /* owner's exception to the 40 px rule */
    CHECK_EQ_INT(bar.y + bar.h, HOME_SCREEN_H);
}

static void test_home_text(void)
{
    CHECK_EQ_STR(home_item_label(HOME_LIVE), "LIVE");
    CHECK_EQ_STR(home_item_sub(HOME_LIVE), "Dash gauges");
    CHECK_EQ_STR(home_item_label(HOME_SETUP), "SETUP");
    CHECK(home_item_sub(HOME_GARAGE) == NULL);
    home_view_t v;
    memset(&v, 0, sizeof v);
    CHECK_EQ_STR(home_cel_text(&v), "CEL not read");
    CHECK_EQ_STR(home_adapter_text(&v), "Not connected");
    v.cel_known = true;
    CHECK_EQ_STR(home_cel_text(&v), "CEL off");
    v.cel_on = true;
    v.connected = true;
    CHECK_EQ_STR(home_cel_text(&v), "CEL ON");
    CHECK_EQ_STR(home_diag_sub(&v), "CEL ON");                  /* the lamp moved to DIAG */
    v.cel_on = false;
    CHECK_EQ_STR(home_diag_sub(&v), "Codes & CEL");
    CHECK_EQ_STR(home_adapter_text(&v), "Connected");
}

void suite_ui(void)
{
    RUN_TEST(test_home_rects_fit_and_dont_overlap);
    RUN_TEST(test_home_hit);
    RUN_TEST(test_home_text);
}
