/*
 * tests/test_sbup_roundtrip.c — host-backend round-trip coverage.
 *
 * Each test points the host PAL at a unique tmpdir under /tmp, so runs
 * are independent and the user's real ~/.lobby_bup is never touched.
 */

#define _DEFAULT_SOURCE  /* mkdtemp on glibc */

#include <saturn_test/test.h>
#include <saturn_bup.h>
#include <saturn_bup/host.h>
#include <saturn_bup/pal.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------------------------------------------------------------------------
 * Fresh tmpdir per test, returned as a static buffer so callers don't
 * have to manage lifetime. mkdtemp(3) creates the dir with mode 0700.
 * ------------------------------------------------------------------------- */

static char g_tmpdir[256];

static const char* fresh_tmpdir(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/sbup-test-XXXXXX");
    if (!mkdtemp(g_tmpdir)) {
        /* If mkdtemp fails the assertion in setup will catch it. */
        g_tmpdir[0] = '\0';
    }
    return g_tmpdir;
}

/* rm -rf the dir between tests so each scenario sees an empty cart. */
static void rm_rf_dir(const char* path) {
    DIR* d = opendir(path);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char p[1024];
        snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        unlink(p);
    }
    closedir(d);
    rmdir(path);
}

static void setup(void) {
    const char* dir = fresh_tmpdir();
    sbup_host_set_basedir(dir);
    sbup_register_host_pal();
}

static void teardown(void) {
    sbup_install_pal(NULL);
    if (g_tmpdir[0]) rm_rf_dir(g_tmpdir);
    sbup_host_set_basedir(NULL);
    g_tmpdir[0] = '\0';
}

SATURN_TEST_FIXTURE(setup, teardown);

/* ---------------------------------------------------------------------------
 * Filename padding (portable utility)
 * ------------------------------------------------------------------------- */

SATURN_TEST(pad_short_name_pads_with_spaces_and_terminates) {
    char buf[12];
    sbup_pad_filename(buf, "ABC");
    SATURN_ASSERT_STR_EQ(buf, "ABC        ");
    SATURN_ASSERT_EQ(buf[11], '\0');
}

SATURN_TEST(pad_exact_eleven_does_not_overflow) {
    char buf[12];
    sbup_pad_filename(buf, "12345678901");
    SATURN_ASSERT_STR_EQ(buf, "12345678901");
    SATURN_ASSERT_EQ(buf[11], '\0');
}

SATURN_TEST(pad_too_long_truncates_to_eleven) {
    char buf[12];
    sbup_pad_filename(buf, "ABCDEFGHIJKLMNOP");
    SATURN_ASSERT_EQ(buf[11], '\0');
    SATURN_ASSERT_STR_EQ(buf, "ABCDEFGHIJK");
}

/* ---------------------------------------------------------------------------
 * Lifecycle errors
 * ------------------------------------------------------------------------- */

SATURN_TEST(read_before_init_errors_not_ready) {
    sbup_handle_t h;
    memset(&h, 0, sizeof(h));   /* uninitialised */
    char buf[8];
    size_t n = 0;
    SATURN_ASSERT(!sbup_read(&h, "X", buf, sizeof(buf), &n));
    /* The handle was never initialised, so last_error stayed zero —
     * the dispatcher catches this via the initialised flag and reports
     * NOT_READY. */
    SATURN_ASSERT_STR_EQ(sbup_last_error(&h),
                         sbup_error_string(SBUP_NOT_READY));
}

/* ---------------------------------------------------------------------------
 * Round-trip
 * ------------------------------------------------------------------------- */

SATURN_TEST(write_then_read_returns_same_bytes) {
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));

    const char payload[] = "hello bup";
    SATURN_ASSERT(sbup_write(&h, "GREETING", payload, sizeof(payload)));

    char buf[64] = {0};
    size_t n = 0;
    SATURN_ASSERT(sbup_read(&h, "GREETING", buf, sizeof(buf), &n));
    SATURN_ASSERT_EQ((long)n, (long)sizeof(payload));
    SATURN_ASSERT_MEM_EQ(buf, payload, sizeof(payload));
}

SATURN_TEST(read_buffer_too_small_errors_broken) {
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));

    char big[200];
    memset(big, 0xA5, sizeof(big));
    SATURN_ASSERT(sbup_write(&h, "BIG", big, sizeof(big)));

    char small[16];
    size_t n = 0;
    SATURN_ASSERT(!sbup_read(&h, "BIG", small, sizeof(small), &n));
    SATURN_ASSERT_STR_EQ(sbup_last_error(&h), sbup_error_string(SBUP_BROKEN));
}

SATURN_TEST(missing_record_returns_not_found) {
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));

    char buf[8];
    size_t n = 0;
    SATURN_ASSERT(!sbup_read(&h, "GHOST", buf, sizeof(buf), &n));
    SATURN_ASSERT_STR_EQ(sbup_last_error(&h), sbup_error_string(SBUP_NOT_FOUND));
}

SATURN_TEST(overwrite_replaces_existing_record) {
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));

    const char first[] = "old payload bytes";
    const char second[] = "new!";
    SATURN_ASSERT(sbup_write(&h, "REC", first, sizeof(first)));
    SATURN_ASSERT(sbup_write(&h, "REC", second, sizeof(second)));

    char buf[64] = {0};
    size_t n = 0;
    SATURN_ASSERT(sbup_read(&h, "REC", buf, sizeof(buf), &n));
    SATURN_ASSERT_EQ((long)n, (long)sizeof(second));
    SATURN_ASSERT_MEM_EQ(buf, second, sizeof(second));
}

/* ---------------------------------------------------------------------------
 * Erase
 * ------------------------------------------------------------------------- */

SATURN_TEST(erase_removes_the_record) {
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));

    const char payload[] = "delete me";
    SATURN_ASSERT(sbup_write(&h, "DOOMED", payload, sizeof(payload)));
    SATURN_ASSERT(sbup_erase(&h, "DOOMED"));

    char buf[16];
    size_t n = 0;
    SATURN_ASSERT(!sbup_read(&h, "DOOMED", buf, sizeof(buf), &n));
    SATURN_ASSERT_STR_EQ(sbup_last_error(&h), sbup_error_string(SBUP_NOT_FOUND));
}

SATURN_TEST(erase_missing_record_succeeds) {
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));
    SATURN_ASSERT(sbup_erase(&h, "NEVER"));
    SATURN_ASSERT_STR_EQ(sbup_last_error(&h), sbup_error_string(SBUP_OK));
}

/* ---------------------------------------------------------------------------
 * Stat numbers move
 * ------------------------------------------------------------------------- */

SATURN_TEST(stat_reflects_writes_and_erases) {
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));

    sbup_device_info_t info0;
    SATURN_ASSERT(sbup_stat(&h, &info0));
    SATURN_ASSERT_EQ((long)info0.data_count, 0L);
    uint32_t free0 = info0.free_size;

    char small[32];
    memset(small, 'a', sizeof(small));
    SATURN_ASSERT(sbup_write(&h, "S1", small, sizeof(small)));

    char med[256];
    memset(med, 'b', sizeof(med));
    SATURN_ASSERT(sbup_write(&h, "M1", med, sizeof(med)));

    sbup_device_info_t info1;
    SATURN_ASSERT(sbup_stat(&h, &info1));
    SATURN_ASSERT_EQ((long)info1.data_count, 2L);
    SATURN_ASSERT_LT(info1.free_size, free0);

    /* Erasing one should bring the count back to 1 and free space up. */
    SATURN_ASSERT(sbup_erase(&h, "M1"));
    sbup_device_info_t info2;
    SATURN_ASSERT(sbup_stat(&h, &info2));
    SATURN_ASSERT_EQ((long)info2.data_count, 1L);
    SATURN_ASSERT_GT(info2.free_size, info1.free_size);
}

/* ---------------------------------------------------------------------------
 * Persistence: a second handle sees the first's writes.
 * ------------------------------------------------------------------------- */

SATURN_TEST(records_persist_across_handles) {
    sbup_handle_t h1;
    SATURN_ASSERT(sbup_init(&h1));

    const char payload[] = "stick around";
    SATURN_ASSERT(sbup_write(&h1, "STAY", payload, sizeof(payload)));

    sbup_handle_t h2;
    SATURN_ASSERT(sbup_init(&h2));

    char buf[32] = {0};
    size_t n = 0;
    SATURN_ASSERT(sbup_read(&h2, "STAY", buf, sizeof(buf), &n));
    SATURN_ASSERT_EQ((long)n, (long)sizeof(payload));
    SATURN_ASSERT_MEM_EQ(buf, payload, sizeof(payload));
}
