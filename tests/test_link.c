/* Talking to the car: start-up, protocol search, supported-PID scan. */
#include <string.h>

#include "harness.h"
#include "fake_io.h"
#include "obd/obd_link.h"

static fake_io_t f;
static elm_t elm;
static obd_link_t link;

static void setup(void)
{
    fake_io_init(&f);
    elm_io_t io = fake_io_iface(&f);
    elm_bind(&elm, &io);
}

static void script_reset(void)
{
    fake_io_reply(&f, "ATZ", "ATZ\r\r\rELM327 v2.3\r\r>", 900, 0);
    fake_io_reply(&f, "ATE0", "ATE0\rOK\r\r>", 10, 0);
    fake_io_reply(&f, "ATL0", "OK\r\r>", 10, 0);
    fake_io_reply(&f, "ATSP0", "OK\r\r>", 10, 0);
}

static void test_link_ridgeline(void)
{
    /* The Ridgeline's real replies (ISO 9141-2, ~4.5 s search). */
    setup();
    script_reset();
    fake_io_reply(&f, "0100", "SEARCHING...\r41 00 BF BE A8 91\r\r>", 4500, 0);
    fake_io_reply(&f, "ATDPN", "A3\r\r>", 40, 0);
    fake_io_reply(&f, "0120", "41 20 80 00 00 00\r\r>", 60, 0);
    CHECK_EQ_INT(obd_link_open(&elm, true, &link), OBD_LINK_OK);
    CHECK_EQ_STR(link.adapter_id, "ELM327 V2.3");
    CHECK_EQ_INT(link.protocol, 3);
    CHECK(link.protocol_auto);
    CHECK_EQ_INT(link.ecus, 1);
    CHECK_EQ_INT(link.ranges_read, 2);
    CHECK(obd_link_pid_supported(&link, 0x01));
    CHECK(!obd_link_pid_supported(&link, 0x02));
    CHECK(obd_link_pid_supported(&link, 0x0C));   /* RPM */
    CHECK(obd_link_pid_supported(&link, 0x20));
    CHECK(obd_link_pid_supported(&link, 0x21));
    CHECK(!obd_link_pid_supported(&link, 0x22));
    CHECK(!obd_link_pid_supported(&link, 0x41));  /* range not read */
    CHECK(!obd_link_pid_supported(&link, 0x00));
    char list[200];
    CHECK_EQ_STR(obd_link_pid_list(&link, list, sizeof list),
                 "01 03 04 05 06 07 08 09 0B 0C 0D 0E 0F 11 13 15 19 1C 20 21");
    CHECK_EQ_INT(f.unexpected_writes, 0);
}

static void test_link_can_two_ecus(void)
{
    /* No reset (the adapter is already set up); CAN, engine + transmission. */
    setup();
    fake_io_reply(&f, "0100", "41 00 BE 3E B8 13\r41 00 80 00 00 00\r\r>", 300, 0);
    fake_io_reply(&f, "ATDPN", "A6\r\r>", 20, 0);
    fake_io_reply(&f, "0120", "41 20 80 01 80 01\r\r>", 30, 0);
    fake_io_reply(&f, "0140", "41 40 C0 00 00 00\r\r>", 30, 0);
    CHECK_EQ_INT(obd_link_open(&elm, false, &link), OBD_LINK_OK);
    CHECK_EQ_INT(link.protocol, 6);
    CHECK_EQ_INT(link.ecus, 2);
    CHECK_EQ_INT(link.ranges_read, 3);
    CHECK(obd_link_pid_supported(&link, 0x41) && obd_link_pid_supported(&link, 0x42));
    CHECK(!obd_link_pid_supported(&link, 0x43));
    CHECK_EQ_STR(link.adapter_id, "");
}

static void test_link_engine_off(void)
{
    setup();
    script_reset();
    fake_io_reply(&f, "0100", "SEARCHING...\rUNABLE TO CONNECT\r\r>", 5600, 0);
    CHECK_EQ_INT(obd_link_open(&elm, true, &link), OBD_LINK_NO_VEHICLE);
    CHECK_EQ_STR(link.failed_command, "0100");
    CHECK_EQ_INT(link.elm_status, ELM_ERR_UNABLE_TO_CONNECT);
    CHECK(strstr(obd_link_result_str(link.result), "engine") != NULL);
}

static void test_link_no_adapter(void)
{
    /* Nothing answers the reset. */
    setup();
    CHECK_EQ_INT(obd_link_open(&elm, true, &link), OBD_LINK_NO_ADAPTER);
    CHECK_EQ_STR(link.failed_command, "ATZ");

    /* ATDPN gives nonsense. */
    setup();
    fake_io_reply(&f, "0100", "41 00 BE 3E B8 11\r\r>", 100, 0);
    fake_io_reply(&f, "ATDPN", "?\r\r>", 10, 0);
    CHECK_EQ_INT(obd_link_open(&elm, false, &link), OBD_LINK_NO_ADAPTER);
    CHECK_EQ_STR(link.failed_command, "ATDPN");
    CHECK_EQ_INT(link.protocol, 0);
}

static void test_link_partial_ranges(void)
{
    /* 0100 claims 0120 exists, but the car then says NO DATA: keep 0100. */
    setup();
    fake_io_reply(&f, "0100", "41 00 BE 3E B8 11\r\r>", 100, 0);
    fake_io_reply(&f, "ATDPN", "A6\r\r>", 10, 0);
    fake_io_reply(&f, "0120", "NO DATA\r\r>", 50, 0);
    CHECK_EQ_INT(obd_link_open(&elm, false, &link), OBD_LINK_OK);
    CHECK_EQ_INT(link.ranges_read, 1);

    /* Connection drops while reading 0120. */
    setup();
    fake_io_reply(&f, "0100", "41 00 BE 3E B8 11\r\r>", 100, 0);
    fake_io_reply(&f, "ATDPN", "A6\r\r>", 10, 0);
    f.close_after_write = false;
    fake_io_reply(&f, "0120", "", 0, 0);
    f.closed = true;
    obd_link_result_t res = obd_link_open(&elm, false, &link);
    CHECK(res == OBD_LINK_LOST || res == OBD_LINK_NO_VEHICLE);
}

void suite_link(void)
{
    gb_suite("link");
    RUN_TEST(test_link_ridgeline);
    RUN_TEST(test_link_can_two_ecus);
    RUN_TEST(test_link_engine_off);
    RUN_TEST(test_link_no_adapter);
    RUN_TEST(test_link_partial_ranges);
}
