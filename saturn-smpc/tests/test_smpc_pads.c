/*
 * tests/test_smpc_pads.c — pad table queries (count, get, by_addr).
 */

#include <saturn_test/test.h>
#include <saturn_smpc.h>

#include "mock_pal.h"

static void setup(void)    { mock_pal_install_and_init(); }
static void teardown(void) { mock_pal_uninstall(); }
SATURN_TEST_FIXTURE(setup, teardown);

/* No connected pads: count is zero, every get is a real pad slot but
 * `connected` is 0. */
SATURN_TEST(smpc_pad_count_zero_when_no_pads) {
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT_EQ(saturn_smpc_pad_count(), 0);
}

SATURN_TEST(smpc_pad_get_returns_null_for_out_of_range_index) {
    SATURN_ASSERT_NULL(saturn_smpc_pad_get(SATURN_SMPC_MAX_PADS));
    SATURN_ASSERT_NULL(saturn_smpc_pad_get(99));
}

/* Single connected pad on port 0 / tap 0. */
SATURN_TEST(smpc_one_connected_pad_visible_at_index_zero) {
    mock_pads[0].port      = 0;
    mock_pads[0].tap       = 0;
    mock_pads[0].connected = 1;
    mock_pads[0].kind      = SATURN_SMPC_KIND_DIGITAL;
    mock_pads[0].buttons   = SATURN_SMPC_BUTTON_A;
    mock_n_pads            = 1;

    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT_EQ(saturn_smpc_pad_count(), 1);

    const saturn_smpc_pad_t* p = saturn_smpc_pad_get(0);
    SATURN_ASSERT_NOT_NULL(p);
    SATURN_ASSERT_EQ(p->port, 0);
    SATURN_ASSERT_EQ(p->tap, 0);
    SATURN_ASSERT_EQ(p->connected, 1);
    SATURN_ASSERT_EQ(p->kind, SATURN_SMPC_KIND_DIGITAL);
    SATURN_ASSERT_EQ(p->buttons, SATURN_SMPC_BUTTON_A);
}

/* pad_by_addr should round-trip. */
SATURN_TEST(smpc_pad_by_addr_finds_known_pad) {
    mock_pads[0].port      = 1;
    mock_pads[0].tap       = 3;
    mock_pads[0].connected = 1;
    mock_pads[0].kind      = SATURN_SMPC_KIND_DIGITAL;
    mock_n_pads            = 1;

    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    const saturn_smpc_pad_t* p = saturn_smpc_pad_by_addr(1, 3);
    SATURN_ASSERT_NOT_NULL(p);
    SATURN_ASSERT_EQ(p->port, 1);
    SATURN_ASSERT_EQ(p->tap, 3);
}

SATURN_TEST(smpc_pad_by_addr_returns_null_for_unknown) {
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT_NULL(saturn_smpc_pad_by_addr(0, 0));
    SATURN_ASSERT_NULL(saturn_smpc_pad_by_addr(99, 99));
}

/* Multiple polls: most recent state wins. */
SATURN_TEST(smpc_poll_overwrites_pad_state) {
    mock_pads[0].connected = 1;
    mock_pads[0].buttons   = SATURN_SMPC_BUTTON_A;
    mock_n_pads            = 1;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);

    mock_pads[0].buttons = SATURN_SMPC_BUTTON_B;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);

    const saturn_smpc_pad_t* p = saturn_smpc_pad_get(0);
    SATURN_ASSERT_NOT_NULL(p);
    SATURN_ASSERT_EQ(p->buttons, SATURN_SMPC_BUTTON_B);
}

/* PAL reporting more than 12 pads is clamped — never overflow. */
SATURN_TEST(smpc_pad_count_clamps_to_max) {
    mock_n_pads = 99;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT_LE(saturn_smpc_pad_count(), SATURN_SMPC_MAX_PADS);
}
