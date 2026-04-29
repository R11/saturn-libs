/*
 * tests/test_smpc_lifecycle.c — init/shutdown and PAL handshake.
 *
 * Crucial: this TU does NOT use the standard fixture, because it tests
 * the case where the PAL is *not* installed. Each test does its own
 * setup explicitly.
 */

#include <saturn_test/test.h>
#include <saturn_smpc.h>
#include <saturn_smpc/pal.h>

#include "mock_pal.h"

/* Init without a PAL must error out cleanly. */
SATURN_TEST(smpc_init_without_pal_errors_not_ready) {
    saturn_smpc_install_pal(NULL);
    SATURN_ASSERT_EQ(saturn_smpc_init(), SATURN_ERR_NOT_READY);
}

/* Once a PAL is installed, init succeeds and forwards to PAL.init. */
SATURN_TEST(smpc_init_with_pal_calls_pal_init_once) {
    mock_pal_install_and_init();
    SATURN_ASSERT_EQ(mock_init_calls, 1);
    mock_pal_uninstall();
}

/* Shutdown forwards to PAL.shutdown and is idempotent. */
SATURN_TEST(smpc_shutdown_is_idempotent) {
    mock_pal_install_and_init();
    saturn_smpc_shutdown();
    SATURN_ASSERT_EQ(mock_shutdown_calls, 1);

    /* second shutdown should be a no-op (no extra PAL call). */
    saturn_smpc_shutdown();
    SATURN_ASSERT_EQ(mock_shutdown_calls, 1);

    saturn_smpc_install_pal(NULL);
}

/* Polling before init (or after shutdown) returns NOT_READY. */
SATURN_TEST(smpc_poll_before_init_errors_not_ready) {
    saturn_smpc_install_pal(NULL);
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_ERR_NOT_READY);
}

/* After init, poll succeeds and forwards to PAL.read_pads. */
SATURN_TEST(smpc_poll_after_init_forwards_to_pal) {
    mock_pal_install_and_init();
    SATURN_ASSERT_EQ(mock_read_pads_calls, 0);
    SATURN_ASSERT_EQ(saturn_smpc_poll(), SATURN_OK);
    SATURN_ASSERT_EQ(mock_read_pads_calls, 1);
    mock_pal_uninstall();
}
