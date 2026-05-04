/*
 * tests/test_identity.c — sapp_identity_* coverage against the host
 * BUP backend. Each test gets a fresh tmpdir so the user's real
 * ~/.lobby_bup is never touched.
 */

#define _DEFAULT_SOURCE  /* mkdtemp on glibc */

#include <saturn_test/test.h>
#include <saturn_app/identity.h>
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

/* Internal hook into core/identity.c — the singleton handle has to be
 * reset between tests because each test installs a fresh PAL pointing
 * at a fresh tmpdir. */
extern void sapp_identity__reset_for_tests(void);

static char g_tmpdir[256];

static const char* fresh_tmpdir(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/sapp-id-test-XXXXXX");
    if (!mkdtemp(g_tmpdir)) {
        g_tmpdir[0] = '\0';
    }
    return g_tmpdir;
}

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
    sapp_identity__reset_for_tests();
}

static void teardown(void) {
    sapp_identity__reset_for_tests();
    sbup_install_pal(NULL);
    if (g_tmpdir[0]) rm_rf_dir(g_tmpdir);
    sbup_host_set_basedir(NULL);
    g_tmpdir[0] = '\0';
}

SATURN_TEST_FIXTURE(setup, teardown);

/* ---------------------------------------------------------------------------
 * default + uuid
 * ------------------------------------------------------------------------- */

SATURN_TEST(default_sets_magic_version_and_nonzero_uuid) {
    sapp_identity_t id;
    sapp_identity_default(&id);
    SATURN_ASSERT_EQ((long)id.magic,   (long)SAPP_IDENTITY_MAGIC);
    SATURN_ASSERT_EQ((long)id.version, (long)SAPP_IDENTITY_VERSION);
    SATURN_ASSERT_EQ((long)id.name_count, 0L);
    SATURN_ASSERT_EQ(id.current_name[0], '\0');

    /* session_uuid must be at least one non-zero byte. */
    int any = 0;
    for (size_t i = 0; i < sizeof(id.session_uuid); ++i) {
        if (id.session_uuid[i]) { any = 1; break; }
    }
    SATURN_ASSERT(any);
}

/* ---------------------------------------------------------------------------
 * round trip
 * ------------------------------------------------------------------------- */

SATURN_TEST(default_save_then_load_round_trips_all_fields) {
    sapp_identity_t a;
    sapp_identity_default(&a);
    /* Populate every field so the round-trip really exercises them. */
    memcpy(a.current_name, "ALICE", 6);
    sapp_identity_add_name(&a, "ALICE");
    sapp_identity_add_name(&a, "BOB");

    SATURN_ASSERT(sapp_identity_save(&a));

    sapp_identity_t b;
    memset(&b, 0xCC, sizeof(b));
    SATURN_ASSERT(sapp_identity_load(&b));

    SATURN_ASSERT_EQ((long)b.magic,      (long)a.magic);
    SATURN_ASSERT_EQ((long)b.version,    (long)a.version);
    SATURN_ASSERT_EQ((long)b.name_count, (long)a.name_count);
    SATURN_ASSERT_MEM_EQ(b.session_uuid, a.session_uuid, sizeof(a.session_uuid));
    SATURN_ASSERT_STR_EQ(b.current_name, a.current_name);
    SATURN_ASSERT_STR_EQ(b.names[0],     a.names[0]);
    SATURN_ASSERT_STR_EQ(b.names[1],     a.names[1]);
}

/* ---------------------------------------------------------------------------
 * add_name dedupe
 * ------------------------------------------------------------------------- */

SATURN_TEST(add_name_is_dedupe) {
    sapp_identity_t id;
    sapp_identity_default(&id);
    sapp_identity_add_name(&id, "ALICE");
    sapp_identity_add_name(&id, "ALICE");
    SATURN_ASSERT_EQ((long)id.name_count, 1L);
    SATURN_ASSERT_STR_EQ(id.names[0], "ALICE");
}

/* ---------------------------------------------------------------------------
 * add_name FIFO eviction at the 9th unique
 * ------------------------------------------------------------------------- */

SATURN_TEST(add_name_evicts_oldest_when_full) {
    sapp_identity_t id;
    sapp_identity_default(&id);
    sapp_identity_add_name(&id, "N1");
    sapp_identity_add_name(&id, "N2");
    sapp_identity_add_name(&id, "N3");
    sapp_identity_add_name(&id, "N4");
    sapp_identity_add_name(&id, "N5");
    sapp_identity_add_name(&id, "N6");
    sapp_identity_add_name(&id, "N7");
    sapp_identity_add_name(&id, "N8");
    SATURN_ASSERT_EQ((long)id.name_count, 8L);
    SATURN_ASSERT_STR_EQ(id.names[0], "N1");
    SATURN_ASSERT_STR_EQ(id.names[7], "N8");

    /* 9th unique evicts N1 and lands at slot 7. */
    sapp_identity_add_name(&id, "N9");
    SATURN_ASSERT_EQ((long)id.name_count, 8L);
    SATURN_ASSERT_STR_EQ(id.names[0], "N2");
    SATURN_ASSERT_STR_EQ(id.names[6], "N8");
    SATURN_ASSERT_STR_EQ(id.names[7], "N9");
}

/* ---------------------------------------------------------------------------
 * load failures
 * ------------------------------------------------------------------------- */

SATURN_TEST(load_with_no_record_returns_false) {
    sapp_identity_t id;
    memset(&id, 0xAB, sizeof(id));
    SATURN_ASSERT(!sapp_identity_load(&id));
    /* Per contract: out is left untouched on failure. */
    SATURN_ASSERT_EQ(id.magic, 0xABABABABu);
}

SATURN_TEST(load_with_corrupted_magic_returns_false) {
    sapp_identity_t a;
    sapp_identity_default(&a);
    SATURN_ASSERT(sapp_identity_save(&a));

    /* Corrupt the magic in the on-disk record by reaching around the
     * identity API straight to sbup. The host PAL stores raw bytes. */
    sbup_handle_t h;
    SATURN_ASSERT(sbup_init(&h));
    sapp_identity_t bad;
    size_t n = 0;
    SATURN_ASSERT(sbup_read(&h, SAPP_IDENTITY_RECORD,
                            &bad, sizeof(bad), &n));
    bad.magic = 0xDEADBEEFu;
    SATURN_ASSERT(sbup_write(&h, SAPP_IDENTITY_RECORD,
                             &bad, sizeof(bad)));

    sapp_identity_t out;
    memset(&out, 0xCD, sizeof(out));
    SATURN_ASSERT(!sapp_identity_load(&out));
    SATURN_ASSERT_EQ(out.magic, 0xCDCDCDCDu);
}
