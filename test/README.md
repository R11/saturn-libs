# libs/test

Host-only test harness for the saturn lobby project. Provides:

- **`SATURN_TEST(name) { ... }`** — declare a test; auto-registers via
  `__attribute__((constructor))`.
- **`SATURN_TEST_FIXTURE(setup, teardown)`** — install setup/teardown that
  wraps every test in the same translation unit.
- **Assertion macros** — `SATURN_ASSERT`, `SATURN_ASSERT_EQ`,
  `SATURN_ASSERT_NE`, `SATURN_ASSERT_LT`, `SATURN_ASSERT_LE`,
  `SATURN_ASSERT_NULL`, `SATURN_ASSERT_NOT_NULL`, `SATURN_ASSERT_STR_EQ`,
  `SATURN_ASSERT_MEM_EQ`, `SATURN_ASSERT_OK`.
- **Runner** — default `main` that runs every registered test, with
  `--filter`, `--list`, `--tap` flags and well-defined exit codes.

## Quick start

```c
#include <saturn_test/test.h>

SATURN_TEST(arithmetic_two_plus_two_is_four) {
    SATURN_ASSERT_EQ(2 + 2, 4);
}
```

Link against `libsaturn_test.a` plus `test_main.o` for the default runner,
or include `test_main.o` only if you want to provide your own `main`.

## Build

```
make            # build libsaturn_test.a
make test       # build + run self-tests
make test-tap   # same with TAP 14 output
make clean
```

## Why fresh, not coup's `cui_test_framework.h`

The shape (auto-registration via constructors, assertion macros, optional
fixtures) is borrowed in spirit. The naming, output format (TAP 14, not
custom), and runner flags are designed for the lobby project's needs. No
header is lifted verbatim.

## Status

This is the first scaffolded lib in the lobby project. Subsequent libs
(`saturn-smpc`, `saturn-vdp1`, ...) will write their tests against this
framework. See `games/lobby/STATUS.md` for the broader project state.
