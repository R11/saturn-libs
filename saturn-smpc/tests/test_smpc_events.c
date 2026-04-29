/*
 * tests/test_smpc_events.c — connect / disconnect edge events.
 *
 * Each event is a one-shot: returns 1 on the poll where the connected
 * flag first flipped, returns 0 every subsequent poll until the flag
 * flips again.
 */

#include <saturn_test/test.h>
#include <saturn_smpc.h>

#include "mock_pal.h"

static void setup(void)    { mock_pal_install_and_init(); }
static void teardown(void) { mock_pal_uninstall(); }
SATURN_TEST_FIXTURE(setup, teardown);

/* On the first poll, no peripheral has been seen before, so a newly
 * connected pad reports "just connected" exactly once. */
SATURN_TEST(smpc_pad_just_connected_fires_on_first_poll) {
    mock_pads[0].port      = 0;
    mock_pads[0].tap       = 0;
    mock_pads[0].connected = 1;
    mock_n_pads            = 1;

    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(saturn_smpc_pad_just_connected(0));
    /* Second poll, same state: edge no longer fires. */
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(!saturn_smpc_pad_just_connected(0));
}

SATURN_TEST(smpc_pad_just_connected_after_unplug_replug) {
    /* Frame 1: pad connected. */
    mock_pads[0].connected = 1;
    mock_n_pads            = 1;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(saturn_smpc_pad_just_connected(0));

    /* Frame 2: still connected; no new edge. */
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(!saturn_smpc_pad_just_connected(0));

    /* Frame 3: unplugged. */
    mock_pads[0].connected = 0;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(saturn_smpc_pad_just_disconnected(0));

    /* Frame 4: replugged → connected edge fires again. */
    mock_pads[0].connected = 1;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(saturn_smpc_pad_just_connected(0));
}

SATURN_TEST(smpc_pad_just_disconnected_fires_once) {
    /* Connect on frame 1. */
    mock_pads[0].connected = 1;
    mock_n_pads            = 1;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);

    /* Disconnect on frame 2. */
    mock_pads[0].connected = 0;
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(saturn_smpc_pad_just_disconnected(0));

    /* Frame 3: still disconnected; edge does not refire. */
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(!saturn_smpc_pad_just_disconnected(0));
}

SATURN_TEST(smpc_pad_events_for_unused_index_return_zero) {
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT(!saturn_smpc_pad_just_connected(7));
    SATURN_ASSERT(!saturn_smpc_pad_just_disconnected(7));
}

SATURN_TEST(smpc_pad_events_out_of_range_return_zero) {
    SATURN_ASSERT(!saturn_smpc_pad_just_connected(99));
    SATURN_ASSERT(!saturn_smpc_pad_just_disconnected(99));
}
