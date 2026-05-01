#include <saturn_test/test.h>
#include <saturn_app.h>
#include <string.h>

/* Tiny stub game used by these tests. State holds a counter; tick
 * increments it; is_done says winner after 5 ticks; render writes a
 * single quad. */
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
#define INP_LEFT   0x0002
#define INP_DOWN   0x0004
#define INP_UP     0x0008
#define INP_START  0x0010
#define INP_A      0x0020
#define INP_B      0x0040

static void zero_inputs(void) { memset(inp, 0, sizeof(inp)); }

static void setup(void) {
    sapp_init(320, 224, 0xC0FFEEu);
    sapp_register_game(&k_stub);
    zero_inputs();
}
static void teardown(void) { sapp_shutdown(); }
SATURN_TEST_FIXTURE(setup, teardown);

SATURN_TEST(app_starts_on_title) {
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_TITLE);
}

SATURN_TEST(app_title_to_select_on_start_press) {
    sapp_run_one_frame(inp);                /* prev=0 */
    inp[0] = INP_START;
    sapp_run_one_frame(inp);                /* edge: TITLE -> SELECT */
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_SELECT);
}

SATURN_TEST(app_select_a_starts_game) {
    sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);          /* enter SELECT */
    zero_inputs();      sapp_run_one_frame(inp);          /* clear edge */
    inp[0] = INP_A;     sapp_run_one_frame(inp);          /* SELECT -> PLAYING */
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING);
    SATURN_ASSERT_STR_EQ(sapp_active_game_id(), "stub");
}

SATURN_TEST(app_game_runs_and_finishes_to_game_over) {
    int i;
    sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    zero_inputs();      sapp_run_one_frame(inp);
    inp[0] = INP_A;     sapp_run_one_frame(inp);

    /* Stub game finishes after 5 ticks of PLAYING. */
    for (i = 0; i < 5; ++i) {
        zero_inputs();
        sapp_run_one_frame(inp);
    }
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_GAME_OVER);
}

SATURN_TEST(app_game_over_back_to_select_on_start) {
    int i;
    sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    zero_inputs();      sapp_run_one_frame(inp);
    inp[0] = INP_A;     sapp_run_one_frame(inp);
    for (i = 0; i < 5; ++i) { zero_inputs(); sapp_run_one_frame(inp); }
    /* now in GAME_OVER */
    zero_inputs();      sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_SELECT);
}

SATURN_TEST(app_renders_title_text_when_in_title) {
    const lobby_scene_t* s = sapp_run_one_frame(inp);
    SATURN_ASSERT_NOT_NULL(s);
    SATURN_ASSERT_GT(s->n_texts, 0);
}

SATURN_TEST(app_renders_game_scene_when_playing) {
    sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    zero_inputs();      sapp_run_one_frame(inp);
    inp[0] = INP_A;     sapp_run_one_frame(inp);
    /* Now PLAYING. Tick once more — the stub game's render writes a quad. */
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

SATURN_TEST(app_bg_image_ref_tracks_state) {
    /* TITLE -> g_title_bg (always present, exported by saturn_app). */
    const lobby_scene_t* s = sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(s->bg_image_ref, &g_title_bg);

    /* TITLE -> SELECT clears bg. */
    inp[0] = INP_START; sapp_run_one_frame(inp);
    zero_inputs();
    s = sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(s->bg_image_ref, (const lobby_bg_image_t*)0);

    /* SELECT -> PLAYING uses the active game's background. */
    inp[0] = INP_A; s = sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(s->bg_image_ref, &k_stub_bg);
}

SATURN_TEST(app_register_rejects_null) {
    SATURN_ASSERT_EQ(sapp_register_game(NULL), SATURN_ERR_INVALID);
}

SATURN_TEST(app_select_b_returns_to_title) {
    sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    zero_inputs();      sapp_run_one_frame(inp);
    inp[0] = INP_B;     sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_TITLE);
}
