/* Tests for source/ui: the HOME bottom screen layout and touch zones. */
#include <string.h>

#include "harness.h"
#include "ui/home_layout.h"
#include "ui/home_top.h"
#include "ui/live_ui.h"

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
    CHECK_EQ_STR(r[4].tag, "Adapter");
    CHECK_EQ_STR(r[4].value, "Not connected");
    CHECK_EQ_STR(r[4].detail, "192.168.0.10");
    CHECK_EQ_INT(r[4].tone, HOME_TONE_RED);
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
    home_row_t r[HOME_ROWS];
    home_top_rows(&t, r);
    CHECK_EQ_STR(r[0].value, "Big Red");
    CHECK_EQ_STR(r[0].detail, "2008 Honda Ridgeline");
    CHECK_EQ_STR(r[1].value, "Off");
    CHECK(r[1].lamp);
    CHECK_EQ_INT(r[1].tone, HOME_TONE_NORMAL);
    CHECK_EQ_STR(r[2].value, "None stored");
    CHECK_EQ_STR(r[3].value, "123,456");
    CHECK_EQ_STR(r[4].value, "ELM327 V2.3");
    CHECK_EQ_STR(r[4].detail, "ISO 9141-2");

    t.nickname = "";
    t.cel_on = true;
    t.have_mil_km = true;
    t.mil_km = 11;        /* 7 mi */
    t.codes = 2;
    t.code_list = "P0171, P0174";
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
    CHECK_EQ_STR(r[4].value, "Connected");
    CHECK_EQ_STR(r[4].detail, "");
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

static void test_live_list(void)
{
    live_rect_t r = live_list_row(0, 0);
    CHECK_EQ_INT(r.x + r.w / 2, LIVE_SCREEN_W / 2);   /* centred */
    CHECK(r.h >= 36);
    CHECK_EQ_INT(live_list_max_scroll(4), 0);          /* 4 rows fit */
    int max = live_list_max_scroll(9);
    CHECK_EQ_INT(max, 6 + 9 * 40 - LIVE_LIST_H);
    CHECK_EQ_INT(live_list_clamp(-5, 9), 0);
    CHECK_EQ_INT(live_list_clamp(999, 9), max);
    CHECK_EQ_INT(live_list_hit(160, 20, 0, 9), 0);
    CHECK_EQ_INT(live_list_hit(160, 44, 0, 9), -1);    /* the gap between rows */
    CHECK_EQ_INT(live_list_hit(160, 50, 0, 9), 1);
    CHECK_EQ_INT(live_list_hit(160, 20, 40, 9), 1);    /* scrolled one row */
    CHECK_EQ_INT(live_list_hit(10, 20, 0, 9), -1);     /* left of the rows */
    CHECK_EQ_INT(live_list_hit(160, 210, 0, 9), -1);   /* the Back tab's strip */
    live_rect_t t = live_list_track();
    CHECK_EQ_INT(live_list_grip_y(0, 9), t.y);
    CHECK_EQ_INT(live_list_grip_y(max, 9) + LIVE_GRIP_H, t.y + t.h);
    CHECK(live_list_grip_y(max / 2, 9) > t.y);
    CHECK(t.x > r.x + r.w);                            /* the bar is right of the rows */
}

static void test_live_view_hits(void)
{
    CHECK_EQ_INT(live_view_hit(30, 220, false), LIVE_HIT_BACK);
    CHECK(live_back_hit(30, 220));
    CHECK(!live_back_hit(300, 220));
    CHECK_EQ_INT(live_view_hit(10, 100, false), LIVE_HIT_PREV);
    CHECK_EQ_INT(live_view_hit(310, 100, false), LIVE_HIT_NEXT);
    CHECK_EQ_INT(live_view_hit(100, 120, false), LIVE_HIT_STOP);
    CHECK_EQ_INT(live_view_hit(220, 120, false), LIVE_HIT_RECORD);
    CHECK_EQ_INT(live_view_hit(160, 170, false), LIVE_HIT_NONE);
    CHECK_EQ_INT(live_view_hit(160, 170, true), LIVE_HIT_EDIT);
    CHECK_EQ_INT(live_view_hit(160, 20, true), LIVE_HIT_NONE);
    for (int a = LIVE_HIT_BACK; a <= LIVE_HIT_EDIT; a++) {   /* touch rule: 40x40 */
        live_rect_t r = live_view_rect((live_hit_t)a);
        CHECK(r.w >= 40 && (r.h >= 38));
        CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= LIVE_SCREEN_W && r.y + r.h <= LIVE_SCREEN_H);
    }
}

static void test_live_dash_math(void)
{
    CHECK_EQ_INT(live_tach_lit(0.0f), 0);
    CHECK_EQ_INT(live_tach_lit(742.0f), 4);
    CHECK_EQ_INT(live_tach_lit(7000.0f), LIVE_TACH_RED);
    CHECK_EQ_INT(live_tach_lit(9500.0f), LIVE_TACH_SEGMENTS);
    CHECK_EQ_INT(live_tach_lit(-5.0f), 0);
    CHECK_EQ_INT(live_coolant_lit(20.0f), 0);
    CHECK_EQ_INT(live_coolant_lit(88.0f), 6);
    CHECK_EQ_INT(live_coolant_lit(140.0f), LIVE_MINI_SEGMENTS);
    CHECK_EQ_INT(live_volts_lit(14.2f), 8);
    CHECK_EQ_INT(live_volts_lit(9.0f), 0);
    CHECK(live_rec_pulse(0) > 0.99f);
    CHECK(live_rec_pulse(1200) < 0.21f);
    CHECK(live_rec_pulse(2400) > 0.99f);
}

static void test_live_list_grip(void)
{
    int n = 9, max = live_list_max_scroll(n);
    CHECK(max > 0);
    /* The strip right of the rows is the bar; the rows and the Back tab aren't. */
    CHECK(live_list_bar_hit(300, 50));
    CHECK(live_list_bar_hit(290, 0));
    CHECK(!live_list_bar_hit(200, 50));
    CHECK(!live_list_bar_hit(300, 210));
    /* On the grip the grab keeps the finger's place; off it the grip centres on it. */
    int top = live_list_grip_y(0, n);
    CHECK_EQ_INT(live_list_grip_grab(top + 5, 0, n), 5);
    CHECK_EQ_INT(live_list_grip_grab(150, 0, n), LIVE_GRIP_H / 2);
    /* Grip position and scroll map both ways, clamped at the ends. */
    CHECK_EQ_INT(live_list_scroll_for_grip(live_list_grip_y(0, n), n), 0);
    CHECK_EQ_INT(live_list_scroll_for_grip(live_list_grip_y(max, n), n), max);
    CHECK_EQ_INT(live_list_scroll_for_grip(-50, n), 0);
    CHECK_EQ_INT(live_list_scroll_for_grip(500, n), max);
    int mid = live_list_scroll_for_grip(live_list_grip_y(max / 2, n), n);
    CHECK(mid >= max / 2 - 2 && mid <= max / 2 + 2);
}

static void test_live_dash_values(void)
{
    live_dash_t d;
    live_dash_values(NULL, OBD_UNITS_US, false, 0, &d);
    CHECK(!d.have_rpm && !d.have_speed && !d.have_coolant && !d.have_volts);
    CHECK_EQ_STR(d.speed, "---");
    CHECK_EQ_STR(d.speed_unit, "mph");
    CHECK_EQ_STR(d.coolant, "--- F");
    CHECK_EQ_STR(d.battery, "--.- V");
    CHECK_EQ_STR(d.odo, "---,---");
    CHECK_EQ_STR(d.odo_unit, "mi");

    /* The garage's odometer until the car reports its own. */
    live_dash_values(NULL, OBD_UNITS_US, true, 198683.0, &d);
    CHECK_EQ_STR(d.odo, "123,456");

    static obd_live_t lv;
    memset(&lv, 0, sizeof lv);
    const uint8_t pids[] = { 0x0C, 0x0D, 0x05, 0xA6 };
    const float vals[] = { 742.0f, 0.0f, 88.0f, 1000.0f };
    for (int i = 0; i < 4; i++) {
        lv.pid[i].pid = pids[i];
        lv.pid[i].have = true;
        lv.pid[i].value = vals[i];
    }
    lv.count = 4;
    lv.have_volts = true;
    lv.volts = 14.18f;
    live_dash_values(&lv, OBD_UNITS_US, true, 198683.0, &d);
    CHECK(d.have_rpm && d.rpm == 742.0f);
    CHECK_EQ_STR(d.speed, "  0");
    CHECK_EQ_STR(d.coolant, "190 F");
    CHECK_EQ_STR(d.battery, "14.2 V");
    CHECK_EQ_STR(d.odo, "621");   /* the car's own: 1000 km */

    lv.pid[1].value = 100.0f;
    live_dash_values(&lv, OBD_UNITS_METRIC, false, 0, &d);
    CHECK_EQ_STR(d.speed, "100");
    CHECK_EQ_STR(d.speed_unit, "km/h");
    CHECK_EQ_STR(d.coolant, "88 C");
    CHECK_EQ_STR(d.odo, "1,000");
    CHECK_EQ_STR(d.odo_unit, "km");
}

void suite_ui(void)
{
    RUN_TEST(test_live_dash_values);
    RUN_TEST(test_live_list_grip);
    RUN_TEST(test_live_list);
    RUN_TEST(test_live_view_hits);
    RUN_TEST(test_live_dash_math);
    RUN_TEST(test_home_top_rows_no_car);
    RUN_TEST(test_home_top_rows_connected);
    RUN_TEST(test_home_group_digits);
    RUN_TEST(test_home_rects_fit_and_dont_overlap);
    RUN_TEST(test_home_hit);
    RUN_TEST(test_home_text);
}
