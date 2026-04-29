/*
 * tests/test_self_basics.c — self-tests for libs/test.
 *
 * These exercise every assertion macro on the success path. Failure paths
 * are tested separately (see tests/test_self_failure_via_subprocess.* —
 * future work). The goal here is to prove that:
 *   1. SATURN_TEST registers and runs a test.
 *   2. Every SATURN_ASSERT_* macro accepts a passing case without aborting.
 *   3. Multiple tests in one translation unit work.
 *   4. Empty test bodies are valid.
 *
 * If this file's tests all pass, the framework's core paths are sound.
 */

#include <saturn_test/test.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * SATURN_TEST registration smoke test
 *
 * The mere fact that this test runs and reports "ok" demonstrates that
 * __attribute__((constructor)) registration, the runner loop, and the test
 * dispatcher all work end-to-end. Empty body is intentional.
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_empty_body_is_valid) {
}

/* ---------------------------------------------------------------------------
 * Boolean assertion
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_assert_truthy_passes) {
    SATURN_ASSERT(1);
    SATURN_ASSERT(42);
    SATURN_ASSERT(-1);
    SATURN_ASSERT(2 + 2 == 4);
}

/* ---------------------------------------------------------------------------
 * Equality / inequality
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_assert_eq_passes_for_equal_ints) {
    SATURN_ASSERT_EQ(0, 0);
    SATURN_ASSERT_EQ(7, 7);
    SATURN_ASSERT_EQ(-3, -3);
    SATURN_ASSERT_EQ((long)0xDEADBEEF, (long)0xDEADBEEF);
}

SATURN_TEST(self_assert_ne_passes_for_distinct_ints) {
    SATURN_ASSERT_NE(0, 1);
    SATURN_ASSERT_NE(7, 8);
    SATURN_ASSERT_NE(-1, 1);
}

/* ---------------------------------------------------------------------------
 * Ordering
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_assert_ordering_passes) {
    SATURN_ASSERT_LT(1, 2);
    SATURN_ASSERT_LE(2, 2);
    SATURN_ASSERT_LE(1, 2);
    SATURN_ASSERT_GT(2, 1);
    SATURN_ASSERT_GE(2, 2);
    SATURN_ASSERT_GE(3, 2);
}

/* ---------------------------------------------------------------------------
 * Pointer assertions
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_assert_pointer_helpers_pass) {
    int local = 0;
    int* p = &local;
    void* nullp = NULL;

    SATURN_ASSERT_NOT_NULL(p);
    SATURN_ASSERT_NULL(nullp);
}

/* ---------------------------------------------------------------------------
 * String comparison
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_assert_str_eq_passes_for_equal_strings) {
    SATURN_ASSERT_STR_EQ("hello", "hello");
    SATURN_ASSERT_STR_EQ("", "");
    SATURN_ASSERT_STR_EQ("saturn", "saturn");
}

/* ---------------------------------------------------------------------------
 * Memory comparison
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_assert_mem_eq_passes_for_equal_buffers) {
    const uint8_t a[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t b[4] = {0x01, 0x02, 0x03, 0x04};

    SATURN_ASSERT_MEM_EQ(a, b, 4);
    /* Empty memory comparison is also valid. */
    SATURN_ASSERT_MEM_EQ(a, b, 0);
}

/* ---------------------------------------------------------------------------
 * Result-code shorthand
 * ------------------------------------------------------------------------- */

static int returns_ok(void) { return 0; }

SATURN_TEST(self_assert_ok_passes_for_zero_return) {
    SATURN_ASSERT_OK(returns_ok());
    SATURN_ASSERT_OK(0);
}

/* ---------------------------------------------------------------------------
 * Many assertions in one body — none should abort the test prematurely
 * because every one passes.
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_many_assertions_in_one_test) {
    int i;
    for (i = 0; i < 50; ++i) {
        SATURN_ASSERT_EQ(i, i);
        SATURN_ASSERT_LT(i, 100);
    }
    SATURN_ASSERT_EQ(i, 50);
}

/* ---------------------------------------------------------------------------
 * A second test in the same TU; separate name, separate registration.
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_two_tests_in_one_tu_both_run) {
    SATURN_ASSERT_EQ(1, 1);
}

/* ---------------------------------------------------------------------------
 * Long but legal test name — verifies the macro name-mangling holds for
 * larger identifiers.
 * ------------------------------------------------------------------------- */

SATURN_TEST(self_a_test_with_a_long_descriptive_name_that_explains_what_it_does) {
    SATURN_ASSERT_STR_EQ("ok", "ok");
}
