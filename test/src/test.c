/*
 * libs/test — runner, registration, and assertion primitives.
 *
 * Internal layout:
 *   - g_tests[]    : flat array of registered tests, populated by
 *                    __attribute__((constructor)) call sites before main().
 *   - g_fixtures[] : per-TU fixtures, indexed by source file path. A test
 *                    looks up its fixture by string-comparing __FILE__.
 *   - g_current_*  : per-test runtime state (failed flag, recorded location
 *                    of first failure for TAP YAML diagnostics).
 *
 * No malloc anywhere. Storage is fixed-size arrays sized for ~1k tests
 * across the project. Bumping the cap is one constant.
 *
 * Assertion failures: each primitive returns 1 on pass, 0 on fail. The
 * caller (the macro in test.h) wraps with `if (!...) return;` so the
 * enclosing SATURN_TEST body exits at the first failure. The runner tally
 * picks up the failure via the g_current_failed flag.
 */

#include <saturn_test/test.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Capacity. Bump if needed.
 * ------------------------------------------------------------------------- */

#define SATURN_TEST_MAX_TESTS     1024
#define SATURN_TEST_MAX_FIXTURES   256

/* ---------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

typedef struct {
    const char*    name;
    const char*    file;
    saturn_test_fn fn;
} reg_test_t;

typedef struct {
    const char*            file;
    saturn_test_fixture_fn setup;
    saturn_test_fixture_fn teardown;
} reg_fixture_t;

static reg_test_t    g_tests[SATURN_TEST_MAX_TESTS];
static int           g_n_tests;

static reg_fixture_t g_fixtures[SATURN_TEST_MAX_FIXTURES];
static int           g_n_fixtures;

static int           g_current_failed;       /* set by assertion primitives */
static const char*   g_first_fail_expr;
static const char*   g_first_fail_file;
static int           g_first_fail_line;
static char          g_first_fail_detail[160]; /* "(got X vs Y)" or similar */

static int           g_use_tap;
static int           g_overflow_test_count;
static int           g_overflow_fixture_count;
static int           g_dup_fixture_seen;
static const char*   g_dup_fixture_file;

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

void saturn_test_register(const char* name,
                          const char* file,
                          saturn_test_fn fn)
{
    if (g_n_tests >= SATURN_TEST_MAX_TESTS) {
        g_overflow_test_count++;
        return;
    }
    g_tests[g_n_tests].name = name;
    g_tests[g_n_tests].file = file;
    g_tests[g_n_tests].fn   = fn;
    g_n_tests++;
}

void saturn_test_register_fixture(const char* file,
                                  saturn_test_fixture_fn setup,
                                  saturn_test_fixture_fn teardown)
{
    int i;

    if (g_n_fixtures >= SATURN_TEST_MAX_FIXTURES) {
        g_overflow_fixture_count++;
        return;
    }
    /* One fixture per TU. A duplicate at the same path is a usage error
     * surfaced when the runner starts. */
    for (i = 0; i < g_n_fixtures; ++i) {
        if (strcmp(g_fixtures[i].file, file) == 0) {
            if (!g_dup_fixture_seen) {
                g_dup_fixture_seen = 1;
                g_dup_fixture_file = file;
            }
            return;
        }
    }
    g_fixtures[g_n_fixtures].file     = file;
    g_fixtures[g_n_fixtures].setup    = setup;
    g_fixtures[g_n_fixtures].teardown = teardown;
    g_n_fixtures++;
}

static const reg_fixture_t* find_fixture(const char* file)
{
    int i;
    for (i = 0; i < g_n_fixtures; ++i) {
        if (strcmp(g_fixtures[i].file, file) == 0) {
            return &g_fixtures[i];
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Assertion primitives
 *
 * On failure the primitive: (a) records a diagnostic in g_first_fail_*,
 * but only the first time per test (so the message reflects what actually
 * tripped), (b) sets g_current_failed, (c) returns 0. The macro caller
 * does `return;` from the test body.
 * ------------------------------------------------------------------------- */

static void record_first_fail(const char* expr,
                              const char* file, int line,
                              const char* detail)
{
    if (g_current_failed) return; /* already recorded */
    g_current_failed       = 1;
    g_first_fail_expr      = expr;
    g_first_fail_file      = file;
    g_first_fail_line      = line;
    if (detail && detail[0]) {
        size_t i = 0;
        while (i < sizeof(g_first_fail_detail) - 1 && detail[i]) {
            g_first_fail_detail[i] = detail[i];
            i++;
        }
        g_first_fail_detail[i] = '\0';
    } else {
        g_first_fail_detail[0] = '\0';
    }
}

int saturn_test_assert(int cond,
                       const char* expr,
                       const char* file, int line)
{
    if (cond) return 1;
    record_first_fail(expr, file, line, NULL);
    return 0;
}

int saturn_test_assert_eq(long a, long b,
                          const char* expr,
                          const char* file, int line)
{
    char buf[64];
    if (a == b) return 1;
    snprintf(buf, sizeof(buf), "(got %ld vs %ld)", a, b);
    record_first_fail(expr, file, line, buf);
    return 0;
}

int saturn_test_assert_ne(long a, long b,
                          const char* expr,
                          const char* file, int line)
{
    char buf[64];
    if (a != b) return 1;
    snprintf(buf, sizeof(buf), "(both equal %ld)", a);
    record_first_fail(expr, file, line, buf);
    return 0;
}

int saturn_test_assert_cmp(int cond,
                           const char* expr,
                           const char* file, int line)
{
    if (cond) return 1;
    record_first_fail(expr, file, line, NULL);
    return 0;
}

int saturn_test_assert_streq(const char* a, const char* b,
                             const char* expr,
                             const char* file, int line)
{
    char buf[160];
    if (a == b) return 1;             /* same pointer, including both NULL */
    if (a == NULL || b == NULL) {
        snprintf(buf, sizeof(buf), "(one operand is NULL)");
        record_first_fail(expr, file, line, buf);
        return 0;
    }
    if (strcmp(a, b) == 0) return 1;
    snprintf(buf, sizeof(buf), "(got \"%s\" vs \"%s\")", a, b);
    record_first_fail(expr, file, line, buf);
    return 0;
}

int saturn_test_assert_memeq(const void* a, const void* b, size_t n,
                             const char* expr,
                             const char* file, int line)
{
    char buf[64];
    if (n == 0) return 1;
    if (a == NULL || b == NULL) {
        snprintf(buf, sizeof(buf), "(one operand is NULL)");
        record_first_fail(expr, file, line, buf);
        return 0;
    }
    if (memcmp(a, b, n) == 0) return 1;
    snprintf(buf, sizeof(buf), "(differ within %zu bytes)", n);
    record_first_fail(expr, file, line, buf);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Glob matching for --filter. Tiny recursive impl: '*' = any sequence,
 * '?' = exactly one char. Anything else is a literal match.
 * ------------------------------------------------------------------------- */

static int glob_match(const char* pat, const char* str)
{
    if (*pat == '\0') return *str == '\0';
    if (*pat == '*') {
        if (glob_match(pat + 1, str)) return 1;
        if (*str && glob_match(pat, str + 1)) return 1;
        return 0;
    }
    if (*pat == '?') {
        if (*str == '\0') return 0;
        return glob_match(pat + 1, str + 1);
    }
    if (*pat != *str) return 0;
    return glob_match(pat + 1, str + 1);
}

/* ---------------------------------------------------------------------------
 * Reset (mostly for embedding harnesses; default main() never calls this).
 * ------------------------------------------------------------------------- */

void saturn_test_runner_reset(void)
{
    g_n_tests                = 0;
    g_n_fixtures             = 0;
    g_current_failed         = 0;
    g_first_fail_expr        = NULL;
    g_first_fail_file        = NULL;
    g_first_fail_line        = 0;
    g_first_fail_detail[0]   = '\0';
    g_use_tap                = 0;
    g_overflow_test_count    = 0;
    g_overflow_fixture_count = 0;
    g_dup_fixture_seen       = 0;
    g_dup_fixture_file       = NULL;
}

/* ---------------------------------------------------------------------------
 * Runner main
 * ------------------------------------------------------------------------- */

static void usage(const char* prog)
{
    printf("Usage: %s [--filter PATTERN] [--list] [--tap] [--help]\n", prog);
    printf("\n");
    printf("  --filter PATTERN   Run only tests whose name matches the\n");
    printf("                     glob (supports '*' and '?').\n");
    printf("  --list             Print every registered test name and exit.\n");
    printf("  --tap              Emit TAP 14 output instead of human-readable.\n");
    printf("  --help             Show this message.\n");
}

int saturn_test_main(int argc, char** argv)
{
    const char* filter   = NULL;
    int         list_only = 0;
    int         i;
    int         matching[SATURN_TEST_MAX_TESTS];
    int         n_matching = 0;
    int         n_pass     = 0;
    int         n_fail     = 0;

    /* Argv parsing */
    for (i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (strcmp(a, "--filter") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "saturn-test: --filter requires an argument\n");
                return SATURN_TEST_EXIT_RUNNER;
            }
            filter = argv[++i];
        } else if (strcmp(a, "--list") == 0) {
            list_only = 1;
        } else if (strcmp(a, "--tap") == 0) {
            g_use_tap = 1;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]);
            return SATURN_TEST_EXIT_OK;
        } else {
            fprintf(stderr, "saturn-test: unknown flag '%s'\n", a);
            return SATURN_TEST_EXIT_RUNNER;
        }
    }

    /* Capacity diagnostics (fail-loud rather than silently truncate) */
    if (g_overflow_test_count > 0) {
        fprintf(stderr, "saturn-test: %d test(s) dropped (cap %d)\n",
                g_overflow_test_count, SATURN_TEST_MAX_TESTS);
        return SATURN_TEST_EXIT_RUNNER;
    }
    if (g_overflow_fixture_count > 0) {
        fprintf(stderr, "saturn-test: %d fixture(s) dropped (cap %d)\n",
                g_overflow_fixture_count, SATURN_TEST_MAX_FIXTURES);
        return SATURN_TEST_EXIT_RUNNER;
    }
    if (g_dup_fixture_seen) {
        fprintf(stderr,
                "saturn-test: duplicate SATURN_TEST_FIXTURE in %s "
                "(only one fixture per TU is allowed)\n",
                g_dup_fixture_file ? g_dup_fixture_file : "?");
        return SATURN_TEST_EXIT_RUNNER;
    }

    /* Build filtered set, preserving registration order */
    for (i = 0; i < g_n_tests; ++i) {
        if (filter == NULL || glob_match(filter, g_tests[i].name)) {
            matching[n_matching++] = i;
        }
    }

    if (list_only) {
        for (i = 0; i < n_matching; ++i) {
            printf("%s\n", g_tests[matching[i]].name);
        }
        return SATURN_TEST_EXIT_OK;
    }

    /* TAP plan line — must come before any test result line. */
    if (g_use_tap) {
        printf("TAP version 14\n");
        printf("1..%d\n", n_matching);
    }

    for (i = 0; i < n_matching; ++i) {
        const reg_test_t*    t  = &g_tests[matching[i]];
        const reg_fixture_t* fx = find_fixture(t->file);

        /* Reset per-test state. */
        g_current_failed       = 0;
        g_first_fail_expr      = NULL;
        g_first_fail_file      = NULL;
        g_first_fail_line      = 0;
        g_first_fail_detail[0] = '\0';

        if (fx && fx->setup)    fx->setup();
        t->fn();
        if (fx && fx->teardown) fx->teardown();

        if (g_current_failed) {
            n_fail++;
            if (g_use_tap) {
                printf("not ok %d - %s\n", i + 1, t->name);
                printf("  ---\n");
                printf("  message: %s%s%s\n",
                       g_first_fail_expr ? g_first_fail_expr : "(no expr)",
                       g_first_fail_detail[0] ? " " : "",
                       g_first_fail_detail);
                printf("  at: %s:%d\n",
                       g_first_fail_file ? g_first_fail_file : "?",
                       g_first_fail_line);
                printf("  ...\n");
            } else {
                printf("[FAIL] %s\n", t->name);
                printf("       %s%s%s\n       at %s:%d\n",
                       g_first_fail_expr ? g_first_fail_expr : "(no expr)",
                       g_first_fail_detail[0] ? " " : "",
                       g_first_fail_detail,
                       g_first_fail_file ? g_first_fail_file : "?",
                       g_first_fail_line);
            }
        } else {
            n_pass++;
            if (g_use_tap) printf("ok %d - %s\n",  i + 1, t->name);
            else           printf("[PASS] %s\n",   t->name);
        }
    }

    if (!g_use_tap) {
        printf("\n%d passed, %d failed (of %d total)\n",
               n_pass, n_fail, n_matching);
    }

    return (n_fail == 0) ? SATURN_TEST_EXIT_OK : SATURN_TEST_EXIT_FAIL;
}
