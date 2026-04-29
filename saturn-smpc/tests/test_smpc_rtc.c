/*
 * tests/test_smpc_rtc.c — RTC read + field validity.
 */

#include <saturn_test/test.h>
#include <saturn_smpc.h>

#include "mock_pal.h"

static void setup(void)    { mock_pal_install_and_init(); }
static void teardown(void) { mock_pal_uninstall(); }
SATURN_TEST_FIXTURE(setup, teardown);

SATURN_TEST(smpc_rtc_read_returns_pal_value) {
    mock_rtc.year     = 2026;
    mock_rtc.month    = 4;
    mock_rtc.day      = 27;
    mock_rtc.weekday  = 1;     /* Monday */
    mock_rtc.hour     = 14;
    mock_rtc.minute   = 30;
    mock_rtc.second   = 0;
    mock_rtc_result   = SATURN_OK;

    saturn_smpc_rtc_t out;
    SATURN_ASSERT_OK(saturn_smpc_rtc_read(&out));
    SATURN_ASSERT_EQ(out.year,    2026);
    SATURN_ASSERT_EQ(out.month,   4);
    SATURN_ASSERT_EQ(out.day,     27);
    SATURN_ASSERT_EQ(out.weekday, 1);
    SATURN_ASSERT_EQ(out.hour,    14);
    SATURN_ASSERT_EQ(out.minute,  30);
    SATURN_ASSERT_EQ(out.second,  0);
}

SATURN_TEST(smpc_rtc_read_returns_invalid_for_null_out) {
    SATURN_ASSERT_EQ(saturn_smpc_rtc_read(NULL), SATURN_ERR_INVALID);
}

/* PAL reports a malformed timestamp; the lib catches the bad month. */
SATURN_TEST(smpc_rtc_read_validates_month_range) {
    mock_rtc.year   = 2026; mock_rtc.month = 13; mock_rtc.day  = 1;
    mock_rtc.hour   = 0;    mock_rtc.minute = 0; mock_rtc.second = 0;
    mock_rtc_result = SATURN_OK;

    saturn_smpc_rtc_t out;
    SATURN_ASSERT_EQ(saturn_smpc_rtc_read(&out), SATURN_ERR_HARDWARE);
}

SATURN_TEST(smpc_rtc_read_validates_day_range) {
    mock_rtc.year   = 2026; mock_rtc.month = 1;  mock_rtc.day  = 32;
    mock_rtc.hour   = 0;    mock_rtc.minute = 0; mock_rtc.second = 0;

    saturn_smpc_rtc_t out;
    SATURN_ASSERT_EQ(saturn_smpc_rtc_read(&out), SATURN_ERR_HARDWARE);
}

SATURN_TEST(smpc_rtc_read_validates_hour_range) {
    mock_rtc.year   = 2026; mock_rtc.month = 1;  mock_rtc.day  = 1;
    mock_rtc.hour   = 24;   mock_rtc.minute = 0; mock_rtc.second = 0;

    saturn_smpc_rtc_t out;
    SATURN_ASSERT_EQ(saturn_smpc_rtc_read(&out), SATURN_ERR_HARDWARE);
}

SATURN_TEST(smpc_rtc_read_propagates_pal_error) {
    mock_rtc_result = SATURN_ERR_HARDWARE;
    saturn_smpc_rtc_t out;
    SATURN_ASSERT_EQ(saturn_smpc_rtc_read(&out), SATURN_ERR_HARDWARE);
}

SATURN_TEST(smpc_rtc_read_before_init_errors_not_ready) {
    mock_pal_uninstall();
    saturn_smpc_install_pal(NULL);
    saturn_smpc_rtc_t out;
    SATURN_ASSERT_EQ(saturn_smpc_rtc_read(&out), SATURN_ERR_NOT_READY);
}
