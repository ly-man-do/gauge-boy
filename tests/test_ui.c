/* Tests for source/ui: the HOME bottom screen layout and touch zones. */
#include <string.h>

#include "harness.h"
#include "ui/home_layout.h"
#include "ui/home_top.h"

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
    CHECK_EQ_INT(home_hit(3, 100), -1);              /* margin */
    CHECK_EQ_INT(home_hit(107, 100), -1);            /* gap */
    CHECK_EQ_INT(home_hit(160, 120), -1);            /* gap between the rows */
    CHECK_EQ_INT(home_hit(160, 225), HOME_CLOSE);    /* bar: closes the app */
    CHECK_EQ_INT(home_hit(0, HOME_SCREEN_H - 1), HOME_CLOSE);
    CHECK_EQ_INT(home_hit(5, HOME_BAR_Y - 1), -1);    /* margin above the bar */
    home_rect_t bar = home_item_touch(HOME_CLOSE);
    CHECK_EQ_INT(bar.h, 38);   /* System Settings' bar: owner's exception to the 40 px rule */
    CHECK_EQ_INT(bar.y + bar.h, HOME_SCREEN_H);
}

static void test_home_text(void)
{
    CHECK_EQ_STR(home_item_label(HOME_LIVE), "LIVE");
    CHECK(home_item_sub(HOME_LIVE) == NULL);
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
    CHECK_EQ_STR(home_diag_label(&v), "CEL ON");                /* the lamp moved to DIAG */
    v.cel_on = false;
    CHECK_EQ_STR(home_diag_label(&v), "DIAG");
    CHECK_EQ_STR(home_adapter_text(&v), "Connected");
}

static void test_home_top_rows_no_car(void)
{
    home_top_t t;
    memset(&t, 0, sizeof t);
    t.codes = -1;
    t.adapter_host = "192.168.0.10";
    home_row_t r[HOME_ROWS];
    home_top_rows(&t, r);
    CHECK_EQ_STR(r[0].tag, "Car");
    CHECK_EQ_STR(r[0].value, "No car yet");
    CHECK_EQ_INT(r[0].tone, HOME_TONE_DIM);
    CHECK_EQ_STR(r[1].value, "--");
    CHECK(!r[1].lamp);
    CHECK_EQ_STR(r[2].value, "--");
    CHECK_EQ_STR(r[3].tag, "Odometer");
    CHECK(r[3].lcd);
    CHECK_EQ_STR(r[3].value, "---,---");
    CHECK_EQ_STR(r[3].detail, "mi");
    CHECK_EQ_STR(r[4].value, "None yet");
    CHECK_EQ_STR(r[5].value, "--");
    CHECK_EQ_STR(r[6].tag, "Adapter");
    CHECK_EQ_STR(r[6].value, "Not connected");
    CHECK_EQ_STR(r[6].detail, "192.168.0.10");
    CHECK_EQ_INT(r[6].tone, HOME_TONE_RED);
}

static void test_home_top_rows_connected(void)
{
    home_top_t t;
    memset(&t, 0, sizeof t);
    t.connected = true;
    t.have_car = true;
    t.title = "2008 Honda Ridgeline";
    t.nickname = "Big Red";
    t.cel_known = true;
    t.codes = 0;
    t.code_list = "";
    t.protocol = 3;
    t.adapter_id = "ELM327 V2.3";
    t.have_odo = true;
    t.odo_km = 123456.5 * 1.609344;
    t.have_last = true;
    t.last_km = 19.956;   /* 12.4 mi */
    t.last_name = "drive-20261118-141344.csv";
    t.today = "20261118";
    t.have_economy = true;
    t.economy = 14.06;
    home_row_t r[HOME_ROWS];
    home_top_rows(&t, r);
    CHECK_EQ_STR(r[0].value, "Big Red");
    CHECK_EQ_STR(r[0].detail, "2008 Honda Ridgeline");
    CHECK_EQ_STR(r[1].value, "Off");
    CHECK(r[1].lamp);
    CHECK_EQ_INT(r[1].tone, HOME_TONE_NORMAL);
    CHECK_EQ_STR(r[2].value, "None stored");
    CHECK_EQ_STR(r[3].value, "123,456");
    CHECK_EQ_STR(r[4].value, "12.4 mi");
    CHECK_EQ_STR(r[4].detail, "today");
    CHECK_EQ_STR(r[5].value, "14.1 mpg");
    CHECK_EQ_STR(r[5].detail, "average");
    CHECK_EQ_INT(r[5].tone, HOME_TONE_ECONOMY);
    CHECK_EQ_STR(r[6].value, "ELM327 V2.3");
    CHECK_EQ_STR(r[6].detail, "ISO 9141-2");

    t.nickname = "";
    t.cel_on = true;
    t.have_mil_km = true;
    t.mil_km = 11;        /* 7 mi */
    t.codes = 2;
    t.code_list = "P0171, P0174";
    t.today = "20261119";
    t.last_name = "drive-20260105-090000.csv";
    t.last_km = 5000;
    t.adapter_id = "";
    t.protocol = 0;
    t.metric = true;
    home_top_rows(&t, r);
    CHECK_EQ_STR(r[0].value, "2008 Honda Ridgeline");
    CHECK_EQ_STR(r[0].detail, "");
    CHECK_EQ_STR(r[1].value, "ON");
    CHECK_EQ_INT(r[1].tone, HOME_TONE_AMBER);
    CHECK_EQ_STR(r[1].detail, "for 11 km");
    CHECK_EQ_STR(r[2].value, "2 stored");
    CHECK_EQ_STR(r[2].detail, "P0171, P0174");
    CHECK_EQ_STR(r[3].value, "198,683");
    CHECK_EQ_STR(r[3].detail, "km");
    CHECK_EQ_STR(r[4].value, "999.9 km");
    CHECK_EQ_STR(r[4].detail, "1/5");
    CHECK_EQ_STR(r[5].value, "14.1 L/100 km");
    CHECK_EQ_STR(r[6].value, "Connected");
    CHECK_EQ_STR(r[6].detail, "");
    t.metric = false;
    home_top_rows(&t, r);
    CHECK_EQ_STR(r[1].detail, "for 7 mi");
}

static void test_home_group_digits(void)
{
    char a[24];
    home_group_digits(123456, a, sizeof a);
    CHECK_EQ_STR(a, "123,456");
    home_group_digits(999, a, sizeof a);
    CHECK_EQ_STR(a, "999");
    home_group_digits(1000, a, sizeof a);
    CHECK_EQ_STR(a, "1,000");
    home_group_digits(0, a, sizeof a);
    CHECK_EQ_STR(a, "0");
    home_group_digits(1234567, a, 6);   /* too small: cut, still terminated */
    CHECK_EQ_INT((int)strlen(a), 5);
    CHECK_EQ_STR(home_protocol_name(6), "CAN 11/500");
    CHECK_EQ_STR(home_protocol_name(13), "");
}

void suite_ui(void)
{
    RUN_TEST(test_home_top_rows_no_car);
    RUN_TEST(test_home_top_rows_connected);
    RUN_TEST(test_home_group_digits);
    RUN_TEST(test_home_rects_fit_and_dont_overlap);
    RUN_TEST(test_home_hit);
    RUN_TEST(test_home_text);
}
