/*
 * tests/test_app_state.c — top-level lifecycle for the post-redesign
 * state machine.
 *
 * The 6 top-level states are:
 *   NAME_ENTRY_FIRST_RUN, LOBBY, PLAYING, GAME_OVER,
 *   PLAYING_ONLINE, RESULTS_ONLINE.
 *
 * Without identity bootstrap, sapp_init() lands on NAME_ENTRY_FIRST_RUN.
 * After typing a name and committing, the app advances to LOBBY (the
 * unified screen). Pressing START on a game launches PLAYING (offline
 * single-game runner). Game completion -> GAME_OVER.
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include <string.h>

typedef struct stub_state { uint32_t ticks; } stub_state_t;

static void stub_init(void* s, const lobby_game_config_t* cfg) {
    (void)cfg; ((stub_state_t*)s)->ticks = 0;
}
static void stub_tick(void* s, const lobby_input_t in[LOBBY_MAX_PLAYERS]) {
    (void)in; ((stub_state_t*)s)->ticks++;
}
static void stub_render(const void* s, lobby_scene_t* out) {
    (void)s;
    lobby_scene_clear(out, 0x801F);
    lobby_scene_quad(out, 10, 20, 30, 40, 0xFFFF);
}
static void stub_is_done(const void* s, lobby_game_result_t* out) {
    const stub_state_t* st = (const stub_state_t*)s;
    if (st->ticks >= 5) {
        out->outcome = LOBBY_OUTCOME_WINNER;
        out->winner_slot = 0;
        out->score[0] = st->ticks;
    } else {
        out->outcome = LOBBY_OUTCOME_RUNNING;
    }
}
static void stub_teardown(void* s) { (void)s; }

static void stub_paint(uint16_t* dst, uint16_t w, uint16_t h) {
    (void)dst; (void)w; (void)h;
}
static const lobby_bg_image_t k_stub_bg = { 320, 224, 0, stub_paint };

static const lobby_game_t k_stub = {
    "stub", "Stub Game", 1, 1, sizeof(stub_state_t),
    stub_init, stub_tick, stub_render, stub_is_done, stub_teardown,
    &k_stub_bg
};

static lobby_input_t inp[LOBBY_MAX_PLAYERS];

#define INP_RIGHT  0x0001
#define INP_DOWN   0x0004
#define INP_UP     0x0008
#define INP_START  0x0010
#define INP_A      0x0020
#define INP_B      0x0040

static void zero_inputs(void) { memset(inp, 0, sizeof(inp)); }

static void setup(void) {
    sapp_init(320, 224, 0xC0FFEEu);
    sapp_register_game(&k_stub);
    /* Install a synthetic identity so we skip first-run name entry and
     * land on the unified LOBBY directly. */
    sapp_identity_t id;
    sapp_identity_default(&id);
    memcpy(id.current_name, "P1", 3);
    sapp_identity_add_name(&id, "P1");
    sapp_set_identity(&id);
    /* Force into LOBBY view. */
    sapp_force_state(LOBBY_STATE_LOBBY);
    extern void sapp_state_lobby_enter(void);
    sapp_state_lobby_enter();
    zero_inputs();
}
static void teardown(void) { sapp_shutdown(); }
SATURN_TEST_FIXTURE(setup, teardown);

SATURN_TEST(app_starts_on_lobby_after_force) {
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOBBY);
    SATURN_ASSERT_NOT_NULL(sapp_lobby_get());
}

SATURN_TEST(app_lobby_to_playing_via_vote) {
    /* Cursor starts on PLAYERS slot 0. RIGHT moves to GAMES column. */
    inp[0] = INP_RIGHT; sapp_run_one_frame(inp);
    zero_inputs();      sapp_run_one_frame(inp);
    /* Pad 0 START commits a vote. With one seated pad (slot 0), one
     * vote = 100% ready (>50%). */
    inp[0] = INP_START; sapp_run_one_frame(inp);
    zero_inputs();      sapp_run_one_frame(inp);
    /* Now the offline vote-timer (5s = 300 frames) and countdown
     * (3s = 180 frames) need to run out. */
    for (int i = 0; i < 600; ++i) {
        zero_inputs(); sapp_run_one_frame(inp);
        if (sapp_state() == LOBBY_STATE_PLAYING) break;
    }
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING);
    SATURN_ASSERT_STR_EQ(sapp_active_game_id(), "stub");
}

SATURN_TEST(app_game_runs_and_finishes_to_game_over) {
    /* Drive into PLAYING via vote. */
    inp[0] = INP_RIGHT; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    for (int i = 0; i < 600; ++i) {
        zero_inputs(); sapp_run_one_frame(inp);
        if (sapp_state() == LOBBY_STATE_PLAYING) break;
    }
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING);
    /* Stub finishes after 5 ticks. */
    for (int i = 0; i < 6; ++i) { zero_inputs(); sapp_run_one_frame(inp); }
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_GAME_OVER);
}

SATURN_TEST(app_game_over_back_to_lobby_on_start) {
    /* Drive PLAYING -> GAME_OVER. */
    inp[0] = INP_RIGHT; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    for (int i = 0; i < 600; ++i) {
        zero_inputs(); sapp_run_one_frame(inp);
        if (sapp_state() == LOBBY_STATE_PLAYING) break;
    }
    for (int i = 0; i < 6; ++i) { zero_inputs(); sapp_run_one_frame(inp); }
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_GAME_OVER);
    /* START -> back to LOBBY. */
    zero_inputs();      sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOBBY);
}

SATURN_TEST(app_renders_lobby_when_in_lobby) {
    const lobby_scene_t* s = sapp_run_one_frame(inp);
    SATURN_ASSERT_NOT_NULL(s);
    SATURN_ASSERT_GT(s->n_texts, 0);
    /* Find the PLAYERS header. */
    int found = 0;
    for (uint16_t i = 0; i < s->n_texts; ++i) {
        if (strncmp(s->texts[i].str, "PLAYERS", 7) == 0) found = 1;
    }
    SATURN_ASSERT(found);
}

SATURN_TEST(app_renders_game_scene_when_playing) {
    /* Drive into PLAYING. */
    inp[0] = INP_RIGHT; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    for (int i = 0; i < 600; ++i) {
        zero_inputs(); sapp_run_one_frame(inp);
        if (sapp_state() == LOBBY_STATE_PLAYING) break;
    }
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING);
    zero_inputs();
    const lobby_scene_t* s = sapp_run_one_frame(inp);
    SATURN_ASSERT_NOT_NULL(s);
    SATURN_ASSERT_EQ(s->n_quads, 1);
    SATURN_ASSERT_EQ(s->quads[0].x, 10);
    SATURN_ASSERT_EQ(s->quads[0].w, 30);
}

SATURN_TEST(app_register_rejects_oversize_state) {
    static const lobby_game_t huge = {
        "huge", "Huge", 1, 1, SAPP_GAME_STATE_BYTES + 1,
        NULL, NULL, NULL, NULL, NULL,
        NULL
    };
    SATURN_ASSERT_EQ(sapp_register_game(&huge), SATURN_ERR_NO_SPACE);
}

SATURN_TEST(app_register_rejects_null) {
    SATURN_ASSERT_EQ(sapp_register_game(NULL), SATURN_ERR_INVALID);
}

SATURN_TEST(app_bg_image_ref_tracks_state) {
    /* Lobby view: no backdrop. */
    const lobby_scene_t* s = sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(s->bg_image_ref, (const lobby_bg_image_t*)0);
    /* Drive into PLAYING. */
    inp[0] = INP_RIGHT; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp); zero_inputs(); sapp_run_one_frame(inp);
    for (int i = 0; i < 600; ++i) {
        zero_inputs(); sapp_run_one_frame(inp);
        if (sapp_state() == LOBBY_STATE_PLAYING) break;
    }
    s = sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(s->bg_image_ref, &k_stub_bg);
}
