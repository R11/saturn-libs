/*
 * tests/test_local_lobby.c — unified lobby state machine.
 *
 * Drives sapp_run_one_frame() with synthetic inputs and asserts on the
 * exposed sapp_lobby_get() snapshot + framework state. A fresh tmpdir is
 * used per test so the user's real ~/.lobby_bup is never touched.
 *
 * Post-redesign: the lobby is a single screen with PLAYERS / GAMES
 * columns. Cursor focus is one of {PLAYERS, GAMES, ACTION}. The right
 * panel takes over only when an action requires it (NAME_PICK,
 * keyboard, connect, lobby-list, room-create, countdown).
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
extern void sapp_state_lobby_enter(void);

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

/* Trivial test game so the GAMES column is non-empty. */
static void tg_init(void* s, const lobby_game_config_t* c) { (void)s; (void)c; }
static void tg_tick(void* s, const lobby_input_t i[LOBBY_MAX_PLAYERS]) { (void)s; (void)i; }
static void tg_render(const void* s, lobby_scene_t* o) { (void)s; (void)o; }
static void tg_done(const void* s, lobby_game_result_t* o) {
    (void)s; if (o) o->outcome = LOBBY_OUTCOME_RUNNING;
}
static void tg_teardown(void* s) { (void)s; }
static const lobby_game_t tg_game = {
    "tg", "TestGame", 1, 1, 16,
    tg_init, tg_tick, tg_render, tg_done, tg_teardown, NULL
};

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
    sapp_register_game(&tg_game);
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

SATURN_TEST(lobby_bootstraps_with_slot0_seated_when_identity_present) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOBBY);

    const sapp_local_lobby_t* L = sapp_lobby_get();
    SATURN_ASSERT_NOT_NULL(L);
    SATURN_ASSERT_EQ(L->seated[0], 1);
    SATURN_ASSERT_STR_EQ(L->seated_name[0], "ALICE");
    for (uint8_t i = 1; i < SAPP_LOBBY_SLOTS; ++i) {
        SATURN_ASSERT_EQ(L->seated[i], 0);
    }
    SATURN_ASSERT_EQ(L->focus, SAPP_LOBBY_FOCUS_PLAYERS);
    SATURN_ASSERT_EQ(L->cursor_player, 0);
    SATURN_ASSERT_EQ(L->view, SAPP_LOBBY_VIEW_DEFAULT);
}

SATURN_TEST(lobby_cursor_down_through_players_falls_into_action_row) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();
    /* Press DOWN 7 times -> slot 7. One more -> ACTION row. */
    for (unsigned i = 0; i < SAPP_LOBBY_SLOTS - 1; ++i) tap_menu(BTN_DOWN);
    SATURN_ASSERT_EQ(sapp_lobby_get()->focus, SAPP_LOBBY_FOCUS_PLAYERS);
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor_player, SAPP_LOBBY_SLOTS - 1);
    tap_menu(BTN_DOWN);
    SATURN_ASSERT_EQ(sapp_lobby_get()->focus, SAPP_LOBBY_FOCUS_ACTION);
}

SATURN_TEST(lobby_cursor_right_moves_to_games_column) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();
    tap_menu(BTN_RIGHT);
    SATURN_ASSERT_EQ(sapp_lobby_get()->focus, SAPP_LOBBY_FOCUS_GAMES);
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor_game, 0);
}

SATURN_TEST(lobby_a_on_empty_slot_opens_name_pick) {
    seed_identity_with("ALICE", "BOB");
    sapp_bootstrap_identity();
    tap_menu(BTN_DOWN);   /* cursor_player = 1 */
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor_player, 1);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_NAME_PICK);
    SATURN_ASSERT_EQ(sapp_lobby_get()->kbd_owner_slot, 1);
}

SATURN_TEST(lobby_pad2_start_seats_slot_via_name_pick) {
    seed_identity_with("ALICE", "BOB");
    sapp_bootstrap_identity();

    /* Pad index 1 START -> opens NAME_PICK for an open slot (pad N
     * default to slot N if free). */
    lobby_input_t in[LOBBY_MAX_PLAYERS] = {0};
    in[1] = BTN_START; step(in);
    in[1] = 0;         step(in);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_NAME_PICK);
    SATURN_ASSERT_EQ(sapp_lobby_get()->kbd_owner_slot, 1);

    /* A confirms default option (BOB; ALICE excluded as it's seated). */
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_DEFAULT);
    SATURN_ASSERT_EQ(sapp_lobby_get()->seated[1], 1);
    SATURN_ASSERT_STR_EQ(sapp_lobby_get()->seated_name[1], "BOB");
}

SATURN_TEST(lobby_a_on_action_connect_opens_connecting_view) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();

    /* Down past slot 7 to reach action row. */
    for (unsigned i = 0; i < SAPP_LOBBY_SLOTS; ++i) tap_menu(BTN_DOWN);
    SATURN_ASSERT_EQ(sapp_lobby_get()->focus, SAPP_LOBBY_FOCUS_ACTION);
    /* cursor_action defaults to 0 (CONNECT in offline mode). */
    SATURN_ASSERT_EQ(sapp_lobby_get()->cursor_action, 0);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_CONNECTING);

    /* B returns to default view. */
    tap_menu(BTN_B);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_DEFAULT);
}

SATURN_TEST(lobby_first_run_advances_to_lobby_on_commit) {
    sapp_bootstrap_identity();
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_NAME_ENTRY_FIRST_RUN);
    /* Type 'A' and commit. */
    tap_menu(BTN_A);
    tap_menu(BTN_START);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOBBY);
    const sapp_local_lobby_t* L = sapp_lobby_get();
    SATURN_ASSERT_NOT_NULL(L);
    SATURN_ASSERT_STR_EQ(L->seated_name[0], "A");
    sapp_identity_t id;
    SATURN_ASSERT(sapp_identity_load(&id));
    SATURN_ASSERT_STR_EQ(id.current_name, "A");
}

SATURN_TEST(lobby_render_emits_players_and_games_columns) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();

    lobby_input_t in[LOBBY_MAX_PLAYERS] = {0};
    const lobby_scene_t* s = sapp_run_one_frame(in);
    SATURN_ASSERT_NOT_NULL(s);
    SATURN_ASSERT_GT(s->n_texts, 0);

    int found_players = 0, found_games = 0;
    for (uint16_t i = 0; i < s->n_texts; ++i) {
        if (strncmp(s->texts[i].str, "PLAYERS", 7) == 0) found_players = 1;
        if (strncmp(s->texts[i].str, "GAMES", 5) == 0)   found_games   = 1;
    }
    SATURN_ASSERT(found_players);
    SATURN_ASSERT(found_games);
}

SATURN_TEST(lobby_pad0_start_in_games_commits_vote) {
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();
    /* Move to GAMES column. */
    tap_menu(BTN_RIGHT);
    SATURN_ASSERT_EQ(sapp_lobby_get()->focus, SAPP_LOBBY_FOCUS_GAMES);
    /* START commits pad 0's vote on the focused game. */
    tap_menu(BTN_START);
    SATURN_ASSERT_EQ(sapp_lobby_get()->ready[0], 1);
    SATURN_ASSERT_EQ(sapp_lobby_get()->vote_game_id[0], 0);
    SATURN_ASSERT_EQ(sapp_lobby_ready_count(), 1);
    SATURN_ASSERT_EQ(sapp_lobby_vote_count_for_game(0), 1);
}

SATURN_TEST(lobby_pad0_a_in_games_also_commits_vote) {
    /* Bug 3 fix: A on a focused game commits the vote, just like START. */
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();
    tap_menu(BTN_RIGHT);
    SATURN_ASSERT_EQ(sapp_lobby_get()->focus, SAPP_LOBBY_FOCUS_GAMES);
    tap_menu(BTN_A);
    SATURN_ASSERT_EQ(sapp_lobby_get()->ready[0], 1);
    SATURN_ASSERT_EQ(sapp_lobby_get()->vote_game_id[0], 0);
}

SATURN_TEST(lobby_pad0_start_on_voted_game_unreadies) {
    /* Bug 2 fix: pressing START again on the same game you're voting
     * for toggles the ready bit off so the player can change their
     * mind / un-ready. Need 2 seated pads so we don't trip the
     * solo-skip-vote-timer path that jumps straight to COUNTDOWN. */
    seed_identity_with("ALICE", "BOB");
    sapp_bootstrap_identity();
    /* Seat pad 1 as BOB. */
    {
        lobby_input_t in[LOBBY_MAX_PLAYERS] = {0};
        in[1] = BTN_START; step(in);
        in[1] = 0;         step(in);
    }
    tap_menu(BTN_A); /* commit BOB in NAME_PICK */
    SATURN_ASSERT_EQ(sapp_lobby_get()->seated[1], 1);

    tap_menu(BTN_RIGHT);
    tap_menu(BTN_START);
    SATURN_ASSERT_EQ(sapp_lobby_get()->ready[0], 1);
    tap_menu(BTN_START);
    SATURN_ASSERT_EQ(sapp_lobby_get()->ready[0], 0);
    SATURN_ASSERT_EQ(sapp_lobby_get()->vote_game_id[0], SAPP_LOBBY_NO_OWNER);
}

SATURN_TEST(lobby_reentry_clears_round_state) {
    /* Bug 1 fix: GAME_OVER -> LOBBY (or RESULTS_ONLINE -> LOBBY) must
     * clear ready[] and vote_game_id[] so the next round doesn't auto-
     * start the same game on a stale majority. */
    seed_identity_with("ALICE", NULL);
    sapp_bootstrap_identity();
    tap_menu(BTN_RIGHT);
    tap_menu(BTN_START);
    SATURN_ASSERT_EQ(sapp_lobby_get()->ready[0], 1);

    /* Simulate going through a round and coming back. */
    sapp_force_state(LOBBY_STATE_GAME_OVER);
    sapp_state_lobby_enter();

    SATURN_ASSERT_EQ(sapp_lobby_get()->ready[0], 0);
    SATURN_ASSERT_EQ(sapp_lobby_get()->vote_game_id[0], SAPP_LOBBY_NO_OWNER);
    SATURN_ASSERT_EQ(sapp_lobby_ready_count(), 0);
    SATURN_ASSERT_EQ(sapp_lobby_vote_count_for_game(0), 0);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_DEFAULT);
}
