/*
 * tests/test_smpc_buttons.c — button helpers (held / pressed / released).
 *
 * The lib's edge detection relies on prev_buttons being set from the
 * previous poll. Tests drive two consecutive polls with controlled
 * button states and assert the helpers report correct edges.
 */

#include <saturn_test/test.h>
#include <saturn_smpc.h>

#include "mock_pal.h"

static void setup(void)    { mock_pal_install_and_init(); }
static void teardown(void) { mock_pal_uninstall(); }
SATURN_TEST_FIXTURE(setup, teardown);

/* held: any-bit-of-mask currently set. */
SATURN_TEST(smpc_button_held_true_when_any_mask_bit_set) {
    saturn_smpc_pad_t p = {0};
    p.buttons = SATURN_SMPC_BUTTON_A;

    SATURN_ASSERT(saturn_smpc_button_held(&p, SATURN_SMPC_BUTTON_A));
    SATURN_ASSERT(!saturn_smpc_button_held(&p, SATURN_SMPC_BUTTON_B));
    /* Multi-bit mask: held if ANY bit matches. */
    SATURN_ASSERT(saturn_smpc_button_held(&p,
                  SATURN_SMPC_BUTTON_A | SATURN_SMPC_BUTTON_B));
}

SATURN_TEST(smpc_button_held_false_for_null_pad) {
    SATURN_ASSERT(!saturn_smpc_button_held(NULL, SATURN_SMPC_BUTTON_A));
}

/* pressed: now-bit-set, prev-bit-clear (rising edge). */
SATURN_TEST(smpc_button_pressed_detects_rising_edge) {
    saturn_smpc_pad_t p = {0};
    p.prev_buttons = 0;
    p.buttons      = SATURN_SMPC_BUTTON_A;
    SATURN_ASSERT(saturn_smpc_button_pressed(&p, SATURN_SMPC_BUTTON_A));
    SATURN_ASSERT(!saturn_smpc_button_pressed(&p, SATURN_SMPC_BUTTON_B));
}

SATURN_TEST(smpc_button_pressed_false_when_already_held) {
    saturn_smpc_pad_t p = {0};
    p.prev_buttons = SATURN_SMPC_BUTTON_A;
    p.buttons      = SATURN_SMPC_BUTTON_A;
    SATURN_ASSERT(!saturn_smpc_button_pressed(&p, SATURN_SMPC_BUTTON_A));
}

/* released: prev-bit-set, now-bit-clear (falling edge). */
SATURN_TEST(smpc_button_released_detects_falling_edge) {
    saturn_smpc_pad_t p = {0};
    p.prev_buttons = SATURN_SMPC_BUTTON_A;
    p.buttons      = 0;
    SATURN_ASSERT(saturn_smpc_button_released(&p, SATURN_SMPC_BUTTON_A));
    SATURN_ASSERT(!saturn_smpc_button_released(&p, SATURN_SMPC_BUTTON_B));
}

SATURN_TEST(smpc_button_released_false_when_still_held) {
    saturn_smpc_pad_t p = {0};
    p.prev_buttons = SATURN_SMPC_BUTTON_A;
    p.buttons      = SATURN_SMPC_BUTTON_A;
    SATURN_ASSERT(!saturn_smpc_button_released(&p, SATURN_SMPC_BUTTON_A));
}

/* prev_buttons is updated by saturn_smpc_poll, not by the helpers
 * themselves. Verify the lib does the bookkeeping by polling twice. */
SATURN_TEST(smpc_poll_updates_prev_buttons_from_buttons) {
    mock_pads[0].connected = 1;
    mock_pads[0].buttons   = SATURN_SMPC_BUTTON_A;
    mock_n_pads            = 1;

    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    /* First poll: buttons=A, prev=0 (nothing before us). */
    const saturn_smpc_pad_t* p0 = saturn_smpc_pad_get(0);
    SATURN_ASSERT_NOT_NULL(p0);
    SATURN_ASSERT_EQ(p0->buttons,      SATURN_SMPC_BUTTON_A);
    SATURN_ASSERT_EQ(p0->prev_buttons, 0);

    mock_pads[0].buttons = SATURN_SMPC_BUTTON_B;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);

    /* Second poll: buttons=B, prev=A (carried from first poll). */
    const saturn_smpc_pad_t* p1 = saturn_smpc_pad_get(0);
    SATURN_ASSERT_NOT_NULL(p1);
    SATURN_ASSERT_EQ(p1->buttons,      SATURN_SMPC_BUTTON_B);
    SATURN_ASSERT_EQ(p1->prev_buttons, SATURN_SMPC_BUTTON_A);

    /* Edge helpers see this as "B pressed, A released this frame". */
    SATURN_ASSERT(saturn_smpc_button_pressed (p1, SATURN_SMPC_BUTTON_B));
    SATURN_ASSERT(saturn_smpc_button_released(p1, SATURN_SMPC_BUTTON_A));
}
