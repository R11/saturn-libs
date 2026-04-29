/*
 * tests/test_self_fixture.c — verifies SATURN_TEST_FIXTURE behavior.
 *
 * The fixture below increments g_setup_calls before every test in this
 * translation unit and increments g_teardown_calls after. Each test asserts
 * that the counters match the expected post-setup state. Because tests run
 * in registration order and the runner resets nothing between TUs, we
 * track call counts inside this TU only.
 *
 * Crucially: the fixture in this file MUST NOT affect tests in
 * test_self_basics.c (different TU, no fixture). That separation is part
 * of what we're verifying.
 */

#include <saturn_test/test.h>

static int g_setup_calls    = 0;
static int g_teardown_calls = 0;
static int g_in_test        = 0;

static void fixture_setup(void) {
    g_setup_calls++;
    g_in_test = 1;
}

static void fixture_teardown(void) {
    g_teardown_calls++;
    g_in_test = 0;
}

SATURN_TEST_FIXTURE(fixture_setup, fixture_teardown);

/* ---------------------------------------------------------------------------
 * After the first setup, g_setup_calls == 1 and g_teardown_calls == 0.
 * The teardown for THIS test runs after the test body returns, so checking
 * teardown count from inside the test always sees the previous run's
 * teardown count.
 * ------------------------------------------------------------------------- */

SATURN_TEST(fixture_first_test_sees_one_setup_zero_prior_teardowns) {
    SATURN_ASSERT_EQ(g_setup_calls, 1);
    SATURN_ASSERT_EQ(g_teardown_calls, 0);
    SATURN_ASSERT_EQ(g_in_test, 1);
}

SATURN_TEST(fixture_second_test_sees_two_setups_one_prior_teardown) {
    SATURN_ASSERT_EQ(g_setup_calls, 2);
    SATURN_ASSERT_EQ(g_teardown_calls, 1);
    SATURN_ASSERT_EQ(g_in_test, 1);
}

SATURN_TEST(fixture_third_test_sees_three_setups_two_prior_teardowns) {
    SATURN_ASSERT_EQ(g_setup_calls, 3);
    SATURN_ASSERT_EQ(g_teardown_calls, 2);
    SATURN_ASSERT_EQ(g_in_test, 1);
}
