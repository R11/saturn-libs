/*
 * tests/test_name_pick.c — name-pick + new-name keyboard transitions.
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include <saturn_bup.h>
#include <saturn_bup/host.h>
#include <saturn_bup/pal.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern void sapp_identity__reset_for_tests(void);

#define BTN_RIGHT  0x0001
#define BTN_LEFT   0x0002
#define BTN_DOWN   0x0004
#define BTN_UP     0x0008
#define BTN_START  0x0010
#define BTN_A      0x0020
#define BTN_B      0x0040

static char g_tmpdir[256];

static void rm_rf_dir(const char* path) {
    DIR* d = opendir(path);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char p[1024]; snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        unlink(p);
    }
    closedir(d); rmdir(path);
}

static void seed_identity(const char* current,
                          const char** names, size_t n_names)
{
    sapp_identity_t id;
    sapp_identity_default(&id);
    if (current) {
        size_t i;
        for (i = 0; i < SAPP_NAME_CAP - 1 && current[i]; ++i)
            id.current_name[i] = current[i];
        id.current_name[i] = '\0';
        sapp_identity_add_name(&id, current);
    }
    for (size_t i = 0; i < n_names; ++i) sapp_identity_add_name(&id, names[i]);
    SATURN_ASSERT(sapp_identity_save(&id));
}

static void setup(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/sapp-pick-XXXXXX");
    SATURN_ASSERT(mkdtemp(g_tmpdir) != NULL);
    sbup_host_set_basedir(g_tmpdir);
    sbup_register_host_pal();
    sapp_identity__reset_for_tests();
    sapp_init(320, 224, 0xC0FFEEu);
}

static void teardown(void) {
    sapp_shutdown();
    sapp_identity__reset_for_tests();
    sbup_install_pal(NULL);
    if (g_tmpdir[0]) rm_rf_dir(g_tmpdir);
    sbup_host_set_basedir(NULL);
    g_tmpdir[0] = '\0';
}

SATURN_TEST_FIXTURE(setup, teardown);

static void step(lobby_input_t in[LOBBY_MAX_PLAYERS]) {
    sapp_run_one_frame(in);
}

static void tap_menu(uint16_t mask) {
    lobby_input_t in[LOBBY_MAX_PLAYERS] = {0};
    in[0] = mask; step(in);
    in[0] = 0;    step(in);
}

/* ------------------------------------------------------------------ */

SATURN_TEST(pick_default_skips_names_seated_in_other_slots) {
    /* Roster ALICE,BOB. ALICE seated in slot 0. Open NAME_PICK for slot 1
     * — default should be BOB (next free). */
    const char* extra[] = {"BOB"};
    seed_identity("ALICE", extra, 1);
    sapp_bootstrap_identity();

    /* Cursor down to slot 1, A. */
    tap_menu(BTN_DOWN);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_PICK);

    /* Confirm immediately; should seat BOB. */
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
    SATURN_ASSERT_STR_EQ(sapp_lobby_get()->seated_name[1], "BOB");
}

SATURN_TEST(pick_left_right_cycles_available_names) {
    const char* extra[] = {"BOB", "CAROL"};
    seed_identity("ALICE", extra, 2);
    sapp_bootstrap_identity();

    tap_menu(BTN_DOWN);   /* cursor->slot 1 */
    tap_menu(BTN_A);      /* enter NAME_PICK */
    /* Default options: BOB,CAROL (ALICE excluded — seated in slot 0).
     * RIGHT moves cursor to CAROL. */
    tap_menu(BTN_RIGHT);
    tap_menu(BTN_A);
    SATURN_ASSERT_STR_EQ(sapp_lobby_get()->seated_name[1], "CAROL");
}

SATURN_TEST(pick_b_on_guest_slot_unseats) {
    const char* extra[] = {"BOB"};
    seed_identity("ALICE", extra, 1);
    sapp_bootstrap_identity();

    tap_menu(BTN_DOWN); tap_menu(BTN_A);     /* enter NAME_PICK slot 1 */
    tap_menu(BTN_B);                          /* cancel */
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
    /* Guest slot 1 was never seated to begin with — still unseated. */
    SATURN_ASSERT_EQ(sapp_lobby_get()->seated[1], 0);
}

SATURN_TEST(pick_b_on_seated_guest_unseats) {
    const char* extra[] = {"BOB"};
    seed_identity("ALICE", extra, 1);
    sapp_bootstrap_identity();

    /* Seat slot 1 first. */
    tap_menu(BTN_DOWN); tap_menu(BTN_A); tap_menu(BTN_A);   /* seat BOB */
    SATURN_ASSERT_EQ(sapp_lobby_get()->seated[1], 1);

    /* Re-open and cancel — should un-seat. */
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_PICK);
    tap_menu(BTN_B);
    SATURN_ASSERT_EQ(sapp_lobby_get()->seated[1], 0);
}

SATURN_TEST(pick_b_on_slot0_does_not_unseat) {
    const char* extra[] = {"BOB"};
    seed_identity("ALICE", extra, 1);
    sapp_bootstrap_identity();

    /* Cursor on slot 0 already. A -> NAME_PICK -> B. */
    tap_menu(BTN_A);
    tap_menu(BTN_B);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
    /* Slot 0 stays seated as ALICE (re-seated on lobby_enter). */
    SATURN_ASSERT_EQ(sapp_lobby_get()->seated[0], 1);
    SATURN_ASSERT_STR_EQ(sapp_lobby_get()->seated_name[0], "ALICE");
    /* Identity unchanged. */
    sapp_identity_t id;
    SATURN_ASSERT(sapp_identity_load(&id));
    SATURN_ASSERT_STR_EQ(id.current_name, "ALICE");
}

SATURN_TEST(pick_a_on_slot0_swaps_current_name_and_persists) {
    const char* extra[] = {"BOB"};
    seed_identity("ALICE", extra, 1);
    sapp_bootstrap_identity();

    /* Cursor on slot 0 — open picker. Default cursor lands on the
     * currently-seated name (ALICE) for slot 0. RIGHT -> BOB. A. */
    tap_menu(BTN_A);
    tap_menu(BTN_RIGHT);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
    SATURN_ASSERT_STR_EQ(sapp_lobby_get()->seated_name[0], "BOB");

    sapp_identity_t id;
    SATURN_ASSERT(sapp_identity_load(&id));
    SATURN_ASSERT_STR_EQ(id.current_name, "BOB");
}

SATURN_TEST(pick_new_name_via_keyboard_adds_to_roster_and_returns) {
    seed_identity("ALICE", NULL, 0);   /* roster=[ALICE] only */
    sapp_bootstrap_identity();

    tap_menu(BTN_DOWN); tap_menu(BTN_A);      /* NAME_PICK slot 1 */
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_PICK);

    /* Press START (=Y) to open keyboard. */
    tap_menu(BTN_START);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_ENTRY_NEW);

    /* Type 'A' (cell 0,0) and START to commit. */
    tap_menu(BTN_A);
    tap_menu(BTN_START);

    /* Back in NAME_PICK with the new name preselected. */
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_PICK);

    /* Confirm: A -> seats slot 1 with "A". */
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
    SATURN_ASSERT_STR_EQ(sapp_lobby_get()->seated_name[1], "A");

    /* Roster includes the new name. */
    sapp_identity_t id;
    SATURN_ASSERT(sapp_identity_load(&id));
    int found_alice = 0, found_a = 0;
    for (uint8_t i = 0; i < id.name_count; ++i) {
        if (strcmp(id.names[i], "ALICE") == 0) found_alice = 1;
        if (strcmp(id.names[i], "A")     == 0) found_a     = 1;
    }
    SATURN_ASSERT(found_alice);
    SATURN_ASSERT(found_a);
}
