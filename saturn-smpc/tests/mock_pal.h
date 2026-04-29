/*
 * tests/mock_pal.h — shared mock SMPC PAL for host tests.
 *
 * Each test TU has its own SATURN_TEST_FIXTURE that delegates to
 * mock_pal_install_and_init() / mock_pal_uninstall(). Tests then poke
 * the exposed `mock_*` globals to drive specific scenarios.
 */

#ifndef SMPC_TEST_MOCK_PAL_H
#define SMPC_TEST_MOCK_PAL_H

#include <saturn_smpc.h>
#include <saturn_smpc/pal.h>
#include <saturn_base/result.h>

/* Mock-controlled state. Tests write these between setup() and the call
 * to saturn_smpc_poll() to inject specific peripheral conditions. */
extern saturn_smpc_pad_t mock_pads[SATURN_SMPC_MAX_PADS];
extern uint8_t           mock_n_pads;
extern saturn_smpc_rtc_t mock_rtc;
extern saturn_result_t   mock_rtc_result;

/* Call counters; useful for asserting lifecycle order. */
extern int               mock_init_calls;
extern int               mock_shutdown_calls;
extern int               mock_read_pads_calls;
extern int               mock_read_rtc_calls;

/* Install the mock PAL and call saturn_smpc_init() (also resets state). */
void mock_pal_install_and_init(void);

/* Reset state to defaults without touching the lib. Used in mid-test
 * scenarios where the test wants a fresh peripheral table. */
void mock_pal_reset(void);

/* saturn_smpc_shutdown() and uninstall the PAL. */
void mock_pal_uninstall(void);

#endif /* SMPC_TEST_MOCK_PAL_H */
