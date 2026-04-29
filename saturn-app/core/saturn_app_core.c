/*
 * libs/saturn-app/core — frame loop, scene compositor, game registry,
 * offline state machine.
 *
 * Single-static-block design (no malloc): one fixed game arena, one
 * fixed scene, one input rising-edge tracker.
 */

#include <saturn_app.h>

#include <string.h>

/* ---------------------------------------------------------------------------
 * Scene helpers
 * ------------------------------------------------------------------------- */

void lobby_scene_clear(lobby_scene_t* s, uint16_t bg_color)
{
    if (!s) return;
    s->bg_color = bg_color;
    s->n_quads  = 0;
    s->n_texts  = 0;
}

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
static int8_t               g_select_cursor;     /* index into g_games */

static int8_t               g_active_game;       /* -1 if none */
static uint8_t              g_game_state_bytes[SAPP_GAME_STATE_BYTES];
static lobby_game_result_t  g_game_result;

static lobby_input_t        g_prev_inputs[LOBBY_MAX_PLAYERS];

static lobby_scene_t        g_scene;

/* Framework-defined palette indices for vdp2 text. */
#define PAL_DEFAULT  0
#define PAL_HIGHLIGHT 1
#define PAL_DIM      2

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Match saturn-smpc's button bitmask. Mirroring rather than including
 * saturn-smpc avoids a circular dep — the framework cares only about the
 * bit pattern, not the lib that produced it. */
#define INP_RIGHT  0x0001
#define INP_LEFT   0x0002
#define INP_DOWN   0x0004
#define INP_UP     0x0008
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
 * Game lifecycle helpers
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
 * State-machine renderers (when not in PLAYING)
 * ------------------------------------------------------------------------- */

static void render_title(void)
{
    lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
    lobby_scene_text(&g_scene, 14, 8,  PAL_DEFAULT,   "S A T U R N");
    lobby_scene_text(&g_scene, 16, 10, PAL_HIGHLIGHT, "L O B B Y");
    lobby_scene_text(&g_scene, 12, 18, PAL_DEFAULT,   "PRESS START");
}

static void render_select(void)
{
    int i;
    char buf[LOBBY_TEXT_MAX_LEN];
    lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
    lobby_scene_text(&g_scene, 12, 2, PAL_HIGHLIGHT, "SELECT A GAME");

    if (g_n_games == 0) {
        lobby_scene_text(&g_scene, 10, 12, PAL_DIM, "NO GAMES INSTALLED");
        return;
    }
    for (i = 0; i < g_n_games; ++i) {
        const lobby_game_t* g = g_games[i];
        const char* arrow = (i == g_select_cursor) ? ">" : " ";
        size_t name_len = 0;
        if (g->display_name) {
            while (g->display_name[name_len] && name_len < LOBBY_TEXT_MAX_LEN - 4)
                name_len++;
        }
        buf[0] = arrow[0]; buf[1] = ' ';
        memcpy(&buf[2], g->display_name ? g->display_name : "(no name)",
               name_len);
        buf[2 + name_len] = '\0';
        lobby_scene_text(&g_scene, 10, (uint8_t)(6 + i * 2),
                         (i == g_select_cursor) ? PAL_HIGHLIGHT : PAL_DEFAULT,
                         buf);
    }

    lobby_scene_text(&g_scene, 4, 24, PAL_DIM,
                     "UP/DOWN: PICK   A: START   B: BACK");
}

static void render_game_over(void)
{
    char score_buf[LOBBY_TEXT_MAX_LEN];
    uint32_t s = g_game_result.score[0];

    lobby_scene_clear(&g_scene, rgb555(32, 0, 0));
    lobby_scene_text(&g_scene, 14, 8, PAL_HIGHLIGHT, "G A M E  O V E R");

    /* Tiny formatter — no printf in case core/ ever ships embedded. */
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

saturn_result_t sapp_init(uint16_t screen_w, uint16_t screen_h, uint32_t boot_seed)
{
    if (screen_w == 0 || screen_h == 0) return SATURN_ERR_INVALID;
    g_inited        = 1;
    g_sw            = screen_w;
    g_sh            = screen_h;
    g_boot_seed     = boot_seed;
    g_frame_count   = 0;
    g_n_games       = 0;
    g_state         = LOBBY_STATE_TITLE;
    g_select_cursor = 0;
    g_active_game   = -1;
    memset(g_prev_inputs, 0, sizeof(g_prev_inputs));
    return SATURN_OK;
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

const lobby_scene_t* sapp_run_one_frame(const lobby_input_t inputs[LOBBY_MAX_PLAYERS])
{
    lobby_input_t menu_now  = inputs ? inputs[0] : 0;
    lobby_input_t menu_prev = g_prev_inputs[0];
    uint8_t       i;

    if (!g_inited) return &g_scene;
    g_frame_count++;

    /* Phase 1: handle input → possibly transition state. */
    switch (g_state) {
    case LOBBY_STATE_TITLE:
        if (input_pressed(menu_now, menu_prev, INP_START)) {
            g_state = LOBBY_STATE_SELECT;
            g_select_cursor = 0;
        }
        break;
    case LOBBY_STATE_SELECT:
        if (g_n_games > 0) {
            if (input_pressed(menu_now, menu_prev, INP_UP)
             && g_select_cursor > 0) g_select_cursor--;
            if (input_pressed(menu_now, menu_prev, INP_DOWN)
             && g_select_cursor < (int8_t)(g_n_games - 1)) g_select_cursor++;
            if (input_pressed(menu_now, menu_prev, INP_A)
             || input_pressed(menu_now, menu_prev, INP_START)) {
                start_active_game(g_select_cursor);
                g_state = LOBBY_STATE_PLAYING;
            }
        }
        if (input_pressed(menu_now, menu_prev, INP_B)) {
            g_state = LOBBY_STATE_TITLE;
        }
        break;
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
            g_state = LOBBY_STATE_SELECT;
        }
        break;
    case LOBBY_STATE_GAME_OVER:
        if (input_pressed(menu_now, menu_prev, INP_START)
         || input_pressed(menu_now, menu_prev, INP_A)) {
            g_state = LOBBY_STATE_SELECT;
        } else if (input_pressed(menu_now, menu_prev, INP_B)) {
            g_state = LOBBY_STATE_TITLE;
        }
        break;
    }

    /* Phase 2: render based on the current (possibly just-transitioned)
     * state. Avoids one-frame lag when the user presses START on TITLE
     * and expects to immediately see the SELECT menu. */
    switch (g_state) {
    case LOBBY_STATE_TITLE:     render_title();     break;
    case LOBBY_STATE_SELECT:    render_select();    break;
    case LOBBY_STATE_GAME_OVER: render_game_over(); break;
    case LOBBY_STATE_PLAYING:
        if (g_active_game >= 0
         && g_games[g_active_game]->render_scene) {
            g_games[g_active_game]->render_scene(g_game_state_bytes, &g_scene);
        }
        break;
    }

    /* Snapshot inputs for next-frame edge detection. */
    for (i = 0; i < LOBBY_MAX_PLAYERS; ++i) {
        g_prev_inputs[i] = inputs ? inputs[i] : 0;
    }
    return &g_scene;
}
