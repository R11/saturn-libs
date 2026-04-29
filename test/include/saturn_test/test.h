/*
 * libs/test — public API for the saturn lobby project's test harness.
 *
 * Drop a SATURN_TEST(...) into any .c file linked against libsaturn_test.a;
 * the test auto-registers at program start and runs by default. Assertions
 * mark the current test failed and early-return from the test function (no
 * exceptions on Saturn, so flow control is via early return).
 *
 * Naming: every public name is prefixed `saturn_test_*` or `SATURN_*`.
 * No platform headers are pulled in here — only stdint and stddef.
 */

#ifndef SATURN_TEST_H
#define SATURN_TEST_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Public types
 * ------------------------------------------------------------------------- */

typedef void (*saturn_test_fn)(void);
typedef void (*saturn_test_fixture_fn)(void);

/* Result of saturn_test_main(). */
enum {
    SATURN_TEST_EXIT_OK     = 0, /* every test passed */
    SATURN_TEST_EXIT_FAIL   = 1, /* one or more tests failed */
    SATURN_TEST_EXIT_RUNNER = 2  /* runner-level error (bad flag, IO failure) */
};

/* ---------------------------------------------------------------------------
 * Test declaration
 *
 * Inside the body of SATURN_TEST(name) { ... } you write whatever you want.
 * Assertions early-return from this body on failure.
 * ------------------------------------------------------------------------- */

#define SATURN_TEST(name)                                                    \
    static void saturn_test_body_##name(void);                               \
    __attribute__((constructor))                                             \
    static void saturn_test_register_##name(void) {                          \
        saturn_test_register(#name, __FILE__,                                \
                             saturn_test_body_##name);                       \
    }                                                                        \
    static void saturn_test_body_##name(void)

/* SATURN_TEST_FIXTURE attaches setup/teardown to every SATURN_TEST in the
 * same translation unit. Either side may be NULL. Only one fixture per TU;
 * declaring a second one in the same file is a registration error caught
 * at runner start. */
#define SATURN_TEST_FIXTURE(setup_fn, teardown_fn)                           \
    __attribute__((constructor))                                             \
    static void saturn_test_fixture_register_(void) {                        \
        saturn_test_register_fixture(__FILE__,                               \
                                     (saturn_test_fixture_fn)(setup_fn),    \
                                     (saturn_test_fixture_fn)(teardown_fn));\
    }                                                                        \
    /* Trailing forward-decl absorbs the user's `;` so the macro can be    \
     * written `SATURN_TEST_FIXTURE(setup, teardown);` without tripping    \
     * -Wpedantic's "extra semicolon at file scope" diagnostic. */          \
    extern int saturn_test_fixture_eat_semi

/* ---------------------------------------------------------------------------
 * Assertion macros
 *
 * On failure: record the failure in the runner's tally, print a diagnostic
 * to stderr, and `return;` out of the enclosing SATURN_TEST body. A test
 * function may have multiple assertions but only the first failure is
 * reported per test (subsequent ones are unreachable via early-return).
 * ------------------------------------------------------------------------- */

#define SATURN_ASSERT(expr)                                                  \
    do {                                                                     \
        if (!saturn_test_assert((expr) != 0, #expr,                          \
                                __FILE__, __LINE__)) return;                 \
    } while (0)

#define SATURN_ASSERT_EQ(a, b)                                               \
    do {                                                                     \
        if (!saturn_test_assert_eq((long)(a), (long)(b),                     \
                                   #a " == " #b,                             \
                                   __FILE__, __LINE__)) return;              \
    } while (0)

#define SATURN_ASSERT_NE(a, b)                                               \
    do {                                                                     \
        if (!saturn_test_assert_ne((long)(a), (long)(b),                     \
                                   #a " != " #b,                             \
                                   __FILE__, __LINE__)) return;              \
    } while (0)

#define SATURN_ASSERT_LT(a, b)                                               \
    do {                                                                     \
        if (!saturn_test_assert_cmp((long)(a) < (long)(b),                   \
                                    #a " < " #b,                             \
                                    __FILE__, __LINE__)) return;             \
    } while (0)

#define SATURN_ASSERT_LE(a, b)                                               \
    do {                                                                     \
        if (!saturn_test_assert_cmp((long)(a) <= (long)(b),                  \
                                    #a " <= " #b,                            \
                                    __FILE__, __LINE__)) return;             \
    } while (0)

#define SATURN_ASSERT_GT(a, b)                                               \
    do {                                                                     \
        if (!saturn_test_assert_cmp((long)(a) > (long)(b),                   \
                                    #a " > " #b,                             \
                                    __FILE__, __LINE__)) return;             \
    } while (0)

#define SATURN_ASSERT_GE(a, b)                                               \
    do {                                                                     \
        if (!saturn_test_assert_cmp((long)(a) >= (long)(b),                  \
                                    #a " >= " #b,                            \
                                    __FILE__, __LINE__)) return;             \
    } while (0)

#define SATURN_ASSERT_NULL(p)                                                \
    do {                                                                     \
        if (!saturn_test_assert((p) == NULL, #p " == NULL",                  \
                                __FILE__, __LINE__)) return;                 \
    } while (0)

#define SATURN_ASSERT_NOT_NULL(p)                                            \
    do {                                                                     \
        if (!saturn_test_assert((p) != NULL, #p " != NULL",                  \
                                __FILE__, __LINE__)) return;                 \
    } while (0)

#define SATURN_ASSERT_STR_EQ(a, b)                                           \
    do {                                                                     \
        if (!saturn_test_assert_streq((a), (b),                              \
                                      #a " == " #b,                          \
                                      __FILE__, __LINE__)) return;           \
    } while (0)

#define SATURN_ASSERT_MEM_EQ(a, b, n)                                        \
    do {                                                                     \
        if (!saturn_test_assert_memeq((a), (b), (size_t)(n),                 \
                                      #a " == " #b " over " #n " bytes",    \
                                      __FILE__, __LINE__)) return;           \
    } while (0)

/* SATURN_ASSERT_OK: shorthand for asserting a saturn_result_t equals 0.
 * `SATURN_OK` lives in saturn-base/result.h once that lib exists; until
 * then this macro hardcodes the 0 expected value. */
#define SATURN_ASSERT_OK(r)            SATURN_ASSERT_EQ((long)(r), 0L)

/* ---------------------------------------------------------------------------
 * Runner-facing API
 *
 * Most consumers won't need these directly — the default main in
 * src/test_main.c calls saturn_test_main(argc, argv). Custom runners can
 * orchestrate registration and execution themselves.
 * ------------------------------------------------------------------------- */

/* Run all registered tests with command-line flags.
 * Flags:
 *   --filter <pattern>   Run only tests whose name matches the glob.
 *                        Glob supports '*' (any sequence) and '?' (one char).
 *   --list               Print every registered test name; do not run.
 *   --tap                Emit TAP 14 output instead of human-readable.
 *   --help               Print usage and exit 0.
 *
 * Returns one of SATURN_TEST_EXIT_*.
 */
int saturn_test_main(int argc, char** argv);

/* Reset the runner's state. Useful only in unusual cases (e.g. an
 * embedding harness that wants to run the suite multiple times in one
 * process). Default main() callers do not need this. */
void saturn_test_runner_reset(void);

/* ---------------------------------------------------------------------------
 * Internal: registration + assertion machinery.
 *
 * These are public only because the macros expand into calls to them; they
 * are not part of the user-facing surface. Test authors should use the
 * macros above, not these functions directly.
 * ------------------------------------------------------------------------- */

void saturn_test_register(const char* name,
                          const char* file,
                          saturn_test_fn fn);

void saturn_test_register_fixture(const char* file,
                                  saturn_test_fixture_fn setup,
                                  saturn_test_fixture_fn teardown);

/* Assertion primitives. Return 1 if the condition holds, 0 otherwise.
 * On 0 they emit a diagnostic via the runner; the calling macro then
 * returns from the test body. */
int saturn_test_assert(int cond,
                       const char* expr,
                       const char* file, int line);

int saturn_test_assert_eq(long a, long b,
                          const char* expr,
                          const char* file, int line);

int saturn_test_assert_ne(long a, long b,
                          const char* expr,
                          const char* file, int line);

int saturn_test_assert_cmp(int cond,
                           const char* expr,
                           const char* file, int line);

int saturn_test_assert_streq(const char* a, const char* b,
                             const char* expr,
                             const char* file, int line);

int saturn_test_assert_memeq(const void* a, const void* b, size_t n,
                             const char* expr,
                             const char* file, int line);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURN_TEST_H */
