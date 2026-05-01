/*
 * libs/saturn-app/core — frame loop, scene compositor, game registry,
 * top-level state machine.
 *
 * After the M-redesign there are 6 top-level states:
 *   NAME_ENTRY_FIRST_RUN  — first-run keyboard
 *   LOBBY                 — unified persistent lobby screen
 *   PLAYING               — offline single-game runner
 *   GAME_OVER             — offline post-game summary
 *   PLAYING_ONLINE        — lockstep online game
 *   RESULTS_ONLINE        — brief online round-over screen
 *
 * Single-static-block design (no malloc): one fixed game arena, one
 * fixed scene, one input rising-edge tracker.
 */

#include <saturn_app.h>
#include "../state/state_internal.h"
#include "../state/state_online.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Scene helpers
 * ------------------------------------------------------------------------- */

void lobby_scene_clear(lobby_scene_t* s, uint16_t bg_color)
{
    if (!s) return;
    s->bg_color    = bg_color;
    s->n_quads     = 0;
    s->n_texts     = 0;
    /* bg_image_ref is NOT cleared here. */
}

/* ---------------------------------------------------------------------------
 * Title splash backdrop (preserved for backwards-compat with shells that
 * upload &g_title_bg unconditionally; the unified lobby does not use it).
 * ------------------------------------------------------------------------- */

static uint16_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = (uint16_t)(r & 0x1F);
    uint16_t g5 = (uint16_t)(g & 0x1F);
    uint16_t b5 = (uint16_t)(b & 0x1F);
    return (uint16_t)(0x8000u | (b5 << 10) | (g5 << 5) | r5);
}

static void title_paint_procedural(uint16_t* dst, uint16_t w, uint16_t h)
{
    uint16_t y, x;
    if (!dst) return;
    for (y = 0; y < h; ++y) {
        uint8_t r = (uint8_t)(2 + (uint32_t)y * 8 / (h ? h : 1));
        uint8_t g = (uint8_t)(2 + (uint32_t)y * 4 / (h ? h : 1));
        uint8_t b = (uint8_t)(10 + (uint32_t)y * 16 / (h ? h : 1));
        uint16_t row_color = pack_rgb(r, g, b);
        for (x = 0; x < w; ++x) dst[(uint32_t)y * w + x] = row_color;
    }
}

const lobby_bg_image_t g_title_bg = {
    320, 224, 0, title_paint_procedural
};

int lobby_scene_quad(lobby_scene_t* s,
                     int16_t x, int16_t y, uint16_t w, uint16_t h,
                     uint16_t color)
{
    lobby_quad_t* q;
    if (!s || s->n_quads >= LOBBY_SCENE_MAX_QUADS) return 0;
    q = &s->quads[s->n_quads++];
    q->x = x; q->y = y; q->w = w; q->h = h; q->color = color;
    return 1;
}

int lobby_scene_text(lobby_scene_t* s,
                     uint8_t col, uint8_t row, uint8_t palette,
                     const char* str)
{
    lobby_text_t* t;
    size_t len = 0;
    if (!s || !str || s->n_texts >= LOBBY_SCENE_MAX_TEXTS) return 0;
    while (str[len] && len < LOBBY_TEXT_MAX_LEN - 1) len++;
    t = &s->texts[s->n_texts++];
    t->col = col; t->row = row; t->palette = palette;
    t->len = (uint8_t)len;
    memcpy(t->str, str, len);
    t->str[len] = '\0';
    return 1;
}

/* ---------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */

static int                  g_inited;
static uint16_t             g_sw, g_sh;
static uint32_t             g_boot_seed;
static uint32_t             g_frame_count;

static const lobby_game_t*  g_games[SAPP_MAX_GAMES];
static uint8_t              g_n_games;

static lobby_state_t        g_state;

static int8_t               g_active_game;
static uint8_t              g_game_state_bytes[SAPP_GAME_STATE_BYTES];
static lobby_game_result_t  g_game_result;

static lobby_input_t        g_prev_inputs[LOBBY_MAX_PLAYERS];

static lobby_scene_t        g_scene;

static sapp_identity_t      g_identity;
static int                  g_identity_valid;

/* Forward decls of state-module hooks. */
void          sapp_state_playing_online_enter (void);
lobby_state_t sapp_state_playing_online_input (
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev[LOBBY_MAX_PLAYERS]);
void          sapp_state_playing_online_render(lobby_scene_t* s);
void          sapp_state_results_online_enter  (void);
lobby_state_t sapp_state_results_online_input  (lobby_input_t now,
                                                lobby_input_t prev);
void          sapp_state_results_online_render (lobby_scene_t* s);

void sapp_scene_clear_for_name_entry(lobby_scene_t* s);

#define PAL_DEFAULT  0
#define PAL_HIGHLIGHT 1
#define PAL_DIM      2

#define INP_START  0x0010
#define INP_A      0x0020
#define INP_B      0x0040

static int input_pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask)
{
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

static uint16_t rgb555(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t r5 = (uint16_t)((r >> 3) & 0x1F);
    uint16_t g5 = (uint16_t)((g >> 3) & 0x1F);
    uint16_t b5 = (uint16_t)((b >> 3) & 0x1F);
    return (uint16_t)(0x8000u | (b5 << 10) | (g5 << 5) | r5);
}

/* ---------------------------------------------------------------------------
 * Game lifecycle helpers (offline path)
 * ------------------------------------------------------------------------- */

static void start_active_game(int8_t idx)
{
    const lobby_game_t* g;
    lobby_game_config_t cfg;

    if (idx < 0 || idx >= g_n_games) return;
    g = g_games[idx];

    cfg.seed       = g_boot_seed ^ g_frame_count;
    cfg.n_players  = 1;
    cfg.difficulty = 0;

    memset(g_game_state_bytes, 0, sizeof(g_game_state_bytes));
    if (g->init) g->init(g_game_state_bytes, &cfg);

    g_active_game            = idx;
    g_game_result.outcome    = LOBBY_OUTCOME_RUNNING;
    g_game_result.winner_slot = 0;
    memset(g_game_result.score, 0, sizeof(g_game_result.score));
}

static void teardown_active_game(void)
{
    if (g_active_game < 0) return;
    if (g_games[g_active_game]->teardown) {
        g_games[g_active_game]->teardown(g_game_state_bytes);
    }
    g_active_game = -1;
}

/* ---------------------------------------------------------------------------
 * Misc renderers (GAME_OVER offline)
 * ------------------------------------------------------------------------- */

static void render_game_over(void)
{
    char score_buf[LOBBY_TEXT_MAX_LEN];
    uint32_t s = g_game_result.score[0];

    lobby_scene_clear(&g_scene, rgb555(32, 0, 0));
    lobby_scene_text(&g_scene, 14, 8, PAL_HIGHLIGHT, "G A M E  O V E R");

    {
        char tmp[16];
        int  i = 0, j;
        if (s == 0) {
            tmp[i++] = '0';
        } else {
            while (s > 0 && i < 15) { tmp[i++] = (char)('0' + (s % 10)); s /= 10; }
        }
        memcpy(score_buf, "SCORE: ", 7);
        for (j = 0; j < i; ++j) score_buf[7 + j] = tmp[i - 1 - j];
        score_buf[7 + i] = '\0';
    }
    lobby_scene_text(&g_scene, 14, 12, PAL_DEFAULT, score_buf);
    lobby_scene_text(&g_scene, 12, 18, PAL_DEFAULT, "PRESS START");
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

extern void sapp_state_local_lobby__reset(void);

saturn_result_t sapp_init(uint16_t screen_w, uint16_t screen_h, uint32_t boot_seed)
{
    if (screen_w == 0 || screen_h == 0) return SATURN_ERR_INVALID;
    g_inited        = 1;
    g_sw            = screen_w;
    g_sh            = screen_h;
    g_boot_seed     = boot_seed;
    g_frame_count   = 0;
    g_n_games       = 0;
    g_state         = LOBBY_STATE_NAME_ENTRY_FIRST_RUN;
    g_active_game   = -1;
    memset(g_prev_inputs, 0, sizeof(g_prev_inputs));
    g_scene.bg_image_ref = NULL;
    memset(&g_identity, 0, sizeof(g_identity));
    g_identity_valid = 0;
    sapp_state_local_lobby__reset();
    sapp_online_ctx_reset();
    return SATURN_OK;
}

void sapp_scene_clear_for_name_entry(lobby_scene_t* s)
{
    lobby_scene_clear(s, rgb555(8, 8, 32));
}

const sapp_identity_t* sapp_bootstrap_identity(void)
{
    sapp_identity_t id;
    if (!sapp_identity_load(&id)) {
        sapp_identity_default(&id);
    }
    g_identity       = id;
    g_identity_valid = 1;

    if (g_identity.current_name[0] == '\0') {
        g_state = LOBBY_STATE_NAME_ENTRY_FIRST_RUN;
        sapp_state_name_entry_first_run_enter();
    } else {
        g_state = LOBBY_STATE_LOBBY;
        sapp_state_lobby_enter();
    }
    return &g_identity;
}

const sapp_identity_t* sapp_get_identity(void)
{
    return g_identity_valid ? &g_identity : NULL;
}

void sapp_set_identity(const sapp_identity_t* id)
{
    if (!id) { g_identity_valid = 0; return; }
    g_identity       = *id;
    g_identity_valid = 1;
}

void sapp_shutdown(void)
{
    if (g_active_game >= 0) teardown_active_game();
    g_inited = 0;
}

saturn_result_t sapp_register_game(const lobby_game_t* g)
{
    if (!g)                          return SATURN_ERR_INVALID;
    if (g_n_games >= SAPP_MAX_GAMES) return SATURN_ERR_NO_SPACE;
    if (g->state_size > SAPP_GAME_STATE_BYTES) return SATURN_ERR_NO_SPACE;
    g_games[g_n_games++] = g;
    return SATURN_OK;
}

uint8_t sapp_game_count(void) { return g_n_games; }

const lobby_game_t* sapp_get_game(uint8_t game_id)
{
    if (game_id >= g_n_games) return NULL;
    return g_games[game_id];
}

uint8_t sapp_count_games(void) { return g_n_games; }

lobby_state_t sapp_state(void) { return g_state; }

const char* sapp_active_game_id(void)
{
    if (g_active_game < 0) return NULL;
    return g_games[g_active_game]->id;
}

uint32_t sapp_frame_count(void) { return g_frame_count; }

void sapp_force_state(lobby_state_t s)
{
    if (g_state == LOBBY_STATE_PLAYING && s != LOBBY_STATE_PLAYING) {
        teardown_active_game();
    }
    g_state = s;
}

/* The unified lobby returns either LOBBY_STATE_LOBBY (stay), or
 * LOBBY_STATE_PLAYING (offline launch), or LOBBY_STATE_PLAYING_ONLINE
 * (online launch). When transitioning to PLAYING (offline) we need to
 * spin up the picked game; the lobby snapshot tells us which. */
static uint8_t pick_offline_game_from_lobby(void)
{
    /* Walk the lobby's vote_game_id[]; pick the most-voted-for. Tie
     * breaks on lower index. The unified lobby's own offline_pick_winner
     * already chose for us, but we need the result here. We re-derive
     * by finding the seated/ready slot's vote with the highest count. */
    uint8_t n_games = sapp_count_games();
    uint8_t best = 0, best_n = 0;
    const sapp_local_lobby_t* L = sapp_lobby_get();
    if (!L || n_games == 0) return 0;
    for (uint8_t g = 0; g < n_games; ++g) {
        uint8_t c = sapp_lobby_vote_count_for_game(g);
        if (c > best_n) { best_n = c; best = g; }
    }
    return best;
}

const lobby_scene_t* sapp_run_one_frame(const lobby_input_t inputs[LOBBY_MAX_PLAYERS])
{
    lobby_input_t menu_now  = inputs ? inputs[0] : 0;
    lobby_input_t menu_prev = g_prev_inputs[0];
    uint8_t       i;

    if (!g_inited) return &g_scene;
    g_frame_count++;

    /* Phase 1: input + transitions. */
    switch (g_state) {
    case LOBBY_STATE_NAME_ENTRY_FIRST_RUN:
        if (sapp_state_name_entry_first_run_input(menu_now, menu_prev)) {
            g_state = LOBBY_STATE_LOBBY;
            sapp_state_lobby_enter();
        }
        break;
    case LOBBY_STATE_LOBBY: {
        lobby_state_t next = sapp_state_lobby_input(inputs, g_prev_inputs);
        if (next != LOBBY_STATE_LOBBY) {
            if (next == LOBBY_STATE_PLAYING) {
                start_active_game((int8_t)pick_offline_game_from_lobby());
                g_state = LOBBY_STATE_PLAYING;
            } else if (next == LOBBY_STATE_PLAYING_ONLINE) {
                sapp_state_playing_online_enter();
                g_state = LOBBY_STATE_PLAYING_ONLINE;
            } else {
                g_state = next;
            }
        }
        break;
    }
    case LOBBY_STATE_PLAYING:
        if (g_active_game >= 0) {
            const lobby_game_t* g = g_games[g_active_game];
            if (g->tick) g->tick(g_game_state_bytes, inputs);
            if (g->is_done) {
                g->is_done(g_game_state_bytes, &g_game_result);
                if (g_game_result.outcome != LOBBY_OUTCOME_RUNNING) {
                    teardown_active_game();
                    g_state = LOBBY_STATE_GAME_OVER;
                }
            }
        } else {
            g_state = LOBBY_STATE_LOBBY;
            sapp_state_lobby_enter();
        }
        break;
    case LOBBY_STATE_GAME_OVER:
        if (input_pressed(menu_now, menu_prev, INP_START)
         || input_pressed(menu_now, menu_prev, INP_A)
         || input_pressed(menu_now, menu_prev, INP_B)) {
            g_state = LOBBY_STATE_LOBBY;
            sapp_state_lobby_enter();
        }
        break;
    case LOBBY_STATE_PLAYING_ONLINE: {
        lobby_state_t next = sapp_state_playing_online_input(inputs, g_prev_inputs);
        if (next != LOBBY_STATE_PLAYING_ONLINE) {
            g_state = next;
            if (next == LOBBY_STATE_RESULTS_ONLINE) sapp_state_results_online_enter();
            else if (next == LOBBY_STATE_LOBBY) sapp_state_lobby_enter();
        }
        break;
    }
    case LOBBY_STATE_RESULTS_ONLINE: {
        lobby_state_t next = sapp_state_results_online_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_RESULTS_ONLINE) {
            g_state = next;
            if (next == LOBBY_STATE_LOBBY) sapp_state_lobby_enter();
        }
        break;
    }
    }

    /* Phase 2: render based on the (possibly-just-transitioned) state. */
    switch (g_state) {
    case LOBBY_STATE_NAME_ENTRY_FIRST_RUN:
        sapp_state_name_entry_first_run_render(&g_scene);
        break;
    case LOBBY_STATE_LOBBY:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_lobby_render(&g_scene);
        break;
    case LOBBY_STATE_PLAYING:
        if (g_active_game >= 0
         && g_games[g_active_game]->render_scene) {
            g_games[g_active_game]->render_scene(g_game_state_bytes, &g_scene);
        }
        break;
    case LOBBY_STATE_GAME_OVER:
        render_game_over();
        break;
    case LOBBY_STATE_PLAYING_ONLINE:
        sapp_state_playing_online_render(&g_scene);
        break;
    case LOBBY_STATE_RESULTS_ONLINE:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_results_online_render(&g_scene);
        break;
    }

    /* Phase 3: backdrop. */
    switch (g_state) {
    case LOBBY_STATE_PLAYING:
        if (g_active_game >= 0) {
            g_scene.bg_image_ref = g_games[g_active_game]->background_image;
        } else {
            g_scene.bg_image_ref = NULL;
        }
        break;
    case LOBBY_STATE_NAME_ENTRY_FIRST_RUN:
    case LOBBY_STATE_LOBBY:
    case LOBBY_STATE_GAME_OVER:
    case LOBBY_STATE_PLAYING_ONLINE:
    case LOBBY_STATE_RESULTS_ONLINE:
    default:
        g_scene.bg_image_ref = NULL;
        break;
    }

    /* Snapshot inputs for next-frame edge detection. */
    for (i = 0; i < LOBBY_MAX_PLAYERS; ++i) {
        g_prev_inputs[i] = inputs ? inputs[i] : 0;
    }
    return &g_scene;
}
