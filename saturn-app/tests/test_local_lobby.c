/*
 * tests/test_local_lobby.c — local-lobby state machine.
 *
 * Drives sapp_run_one_frame() with synthetic inputs and asserts on the
 * exposed sapp_lobby_get() snapshot + framework state. A fresh tmpdir is
 * used per test so the user's real ~/.lobby_bup is never touched.
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

static void seed_identity_with(const char* name, const char* extra1) {
    sapp_identity_t id;
    sapp_identity_default(&id);
    size_t i;
    for (i = 0; i < SAPP_NAME_CAP - 1 && name[i]; ++i) id.current_name[i] = name[i];
    id.current_name[i] = '\0';
    sapp_identity_add_name(&id, name);
    if (extra1) sapp_identity_add_name(&id, extra1);
    SATURN_ASSERT(sapp_identity_save(&id));
}

static void setup(void) {
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/sapp-lobby-XXXXXX");
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

/* Press + release on the menu pad. */
static void tap_menu(uint16_t mask) {
    lobby_input_t in[LOBBY_MAX_PLAYERS] = {0};
    in[0] = mask; step(in);
    in[0] = 0;    step(in);
}

/* ------------------------------------------------------------------ */

SATURN_TEST(lobby_bootstraps_with_slot0_seated_when_identity_present) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);

    const sapp_local_lobby_t* L = sapp_lobby_get();
    SATURN_ASSERT_NOT_NULL(L);
    SATURN_ASSERT_EQ(L->seated[0], 1);
    SATURN_ASSERT_STR_EQ(L->seated_name[0], "ALICE");
    /* Slots 1..7 unseated. */
    for (uint8_t i = 1; i < SAPP_LOBBY_SLOTS; ++i) {
        SATURN_ASSERT_EQ(L->seated[i], 0);
    }
    SATURN_ASSERT_EQ(L->cursor, 0);
    /* cart_color planning placeholder is set per-slot. */
    SATURN_ASSERT_EQ(L->cart_color[0], 0);
    SATURN_ASSERT_EQ(L->cart_color[7], 7);
}

SATURN_TEST(lobby_cursor_wraps_top_and_bottom) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();

    /* Up from 0 wraps to last (PLAY_OFFLINE). */
    tap_menu(BTN_UP);
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor, SAPP_LOBBY_CURSOR_PLAY_OFFLINE);
    /* Down once wraps back to 0. */
    tap_menu(BTN_DOWN);
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor, 0);
}

SATURN_TEST(lobby_a_on_empty_slot_opens_name_pick) {
    seed_identity_with("ALICE", "BOB");
    sapp_bootstrap_identity();

    /* Move cursor to slot 1. */
    tap_menu(BTN_DOWN);
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor, 1);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_PICK);
    SATURN_ASSERT_EQ(sapp_lobby_get()->kbd_owner_slot, 1);
}

SATURN_TEST(lobby_pad2_start_seats_slot_1_via_name_pick) {
    seed_identity_with("ALICE", "BOB");
    sapp_bootstrap_identity();

    /* Pad index 1 presses START — this is "press start on pad 2". */
    lobby_input_t in[LOBBY_MAX_PLAYERS] = {0};
    in[1] = BTN_START; step(in);
    in[1] = 0;         step(in);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_PICK);
    SATURN_ASSERT_EQ(sapp_lobby_get()->kbd_owner_slot, 1);

    /* Confirm: A picks the default option (BOB, since ALICE is in slot 0). */
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
    SATURN_ASSERT_EQ(sapp_lobby_get()->seated[1], 1);
    SATURN_ASSERT_STR_EQ(sapp_lobby_get()->seated_name[1], "BOB");
}

SATURN_TEST(lobby_a_on_connect_transitions_to_connecting) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();

    /* Cursor 0 -> 1..7 -> CONNECT (=8). 8 down presses. */
    for (int i = 0; i < SAPP_LOBBY_CURSOR_CONNECT; ++i) tap_menu(BTN_DOWN);
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor, SAPP_LOBBY_CURSOR_CONNECT);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_CONNECTING);

    /* B returns to lobby. */
    tap_menu(BTN_B);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
}

SATURN_TEST(lobby_a_on_play_offline_drops_into_select) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();

    /* PLAY_OFFLINE = cursor 9. 9 down presses. */
    for (int i = 0; i < SAPP_LOBBY_CURSOR_PLAY_OFFLINE; ++i) tap_menu(BTN_DOWN);
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor, SAPP_LOBBY_CURSOR_PLAY_OFFLINE);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_SELECT);
}

SATURN_TEST(lobby_first_run_advances_to_local_lobby_on_commit) {
    /* No record yet — bootstrap should land on NAME_ENTRY_FIRST_RUN. */
    sapp_bootstrap_identity();
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_ENTRY_FIRST_RUN);

    /* Type one character and commit via START. The widget alphabet starts
     * with 'A' at (0,0); A button on (0,0) appends 'A'. */
    tap_menu(BTN_A);          /* type 'A' */
    tap_menu(BTN_START);      /* commit */

    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOCAL_LOBBY);
    const sapp_local_lobby_t* L = sapp_lobby_get();
    SATURN_ASSERT_NOT_NULL(L);
    SATURN_ASSERT_STR_EQ(L->seated_name[0], "A");
    /* Identity persisted to BUP. */
    sapp_identity_t id;
    SATURN_ASSERT(sapp_identity_load(&id));
    SATURN_ASSERT_STR_EQ(id.current_name, "A");
}

SATURN_TEST(lobby_render_emits_slot_lines_and_action_row) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();

    lobby_input_t in[LOBBY_MAX_PLAYERS] = {0};
    const lobby_scene_t* s = sapp_run_one_frame(in);
    SATURN_ASSERT_NOT_NULL(s);
    /* Header + 8 slots + divider + CONNECT + PLAY_OFFLINE >= 12. */
    SATURN_ASSERT_GT(s->n_texts, 11);

    /* Find the "LOCAL LOBBY" header. */
    int found = 0;
    for (uint16_t i = 0; i < s->n_texts; ++i) {
        if (strncmp(s->texts[i].str, "LOCAL LOBBY", 11) == 0) found = 1;
    }
    SATURN_ASSERT(found);
}
