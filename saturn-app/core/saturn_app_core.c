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
    s->bg_color    = bg_color;
    s->n_quads     = 0;
    s->n_texts     = 0;
    /* Note: bg_image_ref is NOT cleared here. Game render functions
     * call lobby_scene_clear() at the top of every render — clearing
     * bg_image_ref would force a re-upload every frame. The lobby
     * state machine sets bg_image_ref AFTER the per-state render. */
}

/* ---------------------------------------------------------------------------
 * Title splash backdrop (procedural — easy to swap for a PNG-backed
 * lobby_bg_image_t once an asset header lands). The procedural fill is
 * a vertical dark-blue gradient with a slight horizontal warm tint.
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
        /* Top row: deep navy. Bottom row: medium violet. */
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
static int8_t               g_select_cursor;     /* index into g_games */

static int8_t               g_active_game;       /* -1 if none */
static uint8_t              g_game_state_bytes[SAPP_GAME_STATE_BYTES];
static lobby_game_result_t  g_game_result;

static lobby_input_t        g_prev_inputs[LOBBY_MAX_PLAYERS];

static lobby_scene_t        g_scene;

/* Identity (loaded from BUP via sapp_bootstrap_identity, or installed
 * directly via sapp_set_identity). g_identity_valid gates reads. */
static sapp_identity_t      g_identity;
static int                  g_identity_valid;

/* Forward decls of the state module hooks. Defined under state/. Kept
 * here rather than in a header because they're internal coordination
 * between core/ and state/. (state_internal.h consolidates the trio for
 * the state modules themselves.) */
void          sapp_state_name_entry_first_run_enter (void);
int           sapp_state_name_entry_first_run_input (lobby_input_t now,
                                                     lobby_input_t prev);
void          sapp_state_name_entry_first_run_render(lobby_scene_t* scene);

void          sapp_state_local_lobby_enter  (void);
lobby_state_t sapp_state_local_lobby_input  (
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev  [LOBBY_MAX_PLAYERS]);
void          sapp_state_local_lobby_render (lobby_scene_t* scene);

void          sapp_state_name_pick_enter (uint8_t slot);
lobby_state_t sapp_state_name_pick_input (lobby_input_t now,
                                          lobby_input_t prev);
void          sapp_state_name_pick_render(lobby_scene_t* scene);

void          sapp_state_name_entry_new_enter (void);
lobby_state_t sapp_state_name_entry_new_input (lobby_input_t now,
                                               lobby_input_t prev);
void          sapp_state_name_entry_new_render(lobby_scene_t* scene);

/* M4 online states. */
void          sapp_state_connecting_enter   (void);
lobby_state_t sapp_state_connecting_input   (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_connecting_render  (lobby_scene_t* s);

void          sapp_state_lobby_list_enter   (void);
lobby_state_t sapp_state_lobby_list_input   (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_lobby_list_render  (lobby_scene_t* s);

void          sapp_state_room_create_enter  (void);
lobby_state_t sapp_state_room_create_input  (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_room_create_render (lobby_scene_t* s);

void          sapp_state_in_room_enter      (void);
lobby_state_t sapp_state_in_room_input      (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_in_room_render     (lobby_scene_t* s);

void          sapp_state_game_select_online_enter  (void);
lobby_state_t sapp_state_game_select_online_input  (lobby_input_t now,
                                                    lobby_input_t prev);
void          sapp_state_game_select_online_render (lobby_scene_t* s);
void          sapp_state_vote_timer_enter   (void);
lobby_state_t sapp_state_vote_timer_input   (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_vote_timer_render  (lobby_scene_t* s);
void          sapp_state_countdown_enter    (void);
lobby_state_t sapp_state_countdown_input    (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_countdown_render   (lobby_scene_t* s);
void          sapp_state_playing_online_enter (void);
lobby_state_t sapp_state_playing_online_input (
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev[LOBBY_MAX_PLAYERS]);
void          sapp_state_playing_online_render(lobby_scene_t* s);
void          sapp_state_results_online_enter  (void);
lobby_state_t sapp_state_results_online_input  (lobby_input_t now,
                                                lobby_input_t prev);
void          sapp_state_results_online_render (lobby_scene_t* s);

/* Shared scene preamble used by the name-entry render. Lives here so the
 * state module doesn't need to know the framework's palette/colour
 * conventions. */
void sapp_scene_clear_for_name_entry(lobby_scene_t* s);

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

/* Reset hook from state/state_local_lobby.c — clears its module-static
 * lobby snapshot so a re-init test starts fresh. */
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
    g_state         = LOBBY_STATE_TITLE;
    g_select_cursor = 0;
    g_active_game   = -1;
    memset(g_prev_inputs, 0, sizeof(g_prev_inputs));
    g_scene.bg_image_ref = NULL;
    memset(&g_identity, 0, sizeof(g_identity));
    g_identity_valid = 0;
    sapp_state_local_lobby__reset();
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
        /* Don't save yet — wait until the user lands a name in NAME_ENTRY,
         * so a transient launch can't pollute the cart. */
    }
    g_identity       = id;
    g_identity_valid = 1;

    if (g_identity.current_name[0] == '\0') {
        g_state = LOBBY_STATE_NAME_ENTRY_FIRST_RUN;
        sapp_state_name_entry_first_run_enter();
    } else {
        g_state = LOBBY_STATE_LOCAL_LOBBY;
        sapp_state_local_lobby_enter();
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

const lobby_scene_t* sapp_run_one_frame(const lobby_input_t inputs[LOBBY_MAX_PLAYERS])
{
    lobby_input_t menu_now  = inputs ? inputs[0] : 0;
    lobby_input_t menu_prev = g_prev_inputs[0];
    uint8_t       i;

    if (!g_inited) return &g_scene;
    g_frame_count++;

    /* Phase 1: handle input → possibly transition state. */
    switch (g_state) {
    case LOBBY_STATE_NAME_ENTRY_FIRST_RUN:
        if (sapp_state_name_entry_first_run_input(menu_now, menu_prev)) {
            g_state = LOBBY_STATE_LOCAL_LOBBY;
            sapp_state_local_lobby_enter();
        }
        break;
    case LOBBY_STATE_LOCAL_LOBBY: {
        lobby_state_t next = sapp_state_local_lobby_input(inputs, g_prev_inputs);
        if (next != LOBBY_STATE_LOCAL_LOBBY) {
            g_state = next;
            if (next == LOBBY_STATE_SELECT)     g_select_cursor = 0;
            if (next == LOBBY_STATE_CONNECTING) sapp_state_connecting_enter();
        }
        break;
    }
    case LOBBY_STATE_NAME_PICK: {
        lobby_state_t next = sapp_state_name_pick_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_NAME_PICK) {
            g_state = next;
            if (next == LOBBY_STATE_LOCAL_LOBBY) sapp_state_local_lobby_enter();
            else if (next == LOBBY_STATE_NAME_ENTRY_NEW) {
                /* enter handled by name_pick before returning */
            }
        }
        break;
    }
    case LOBBY_STATE_NAME_ENTRY_NEW: {
        lobby_state_t next = sapp_state_name_entry_new_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_NAME_ENTRY_NEW) {
            g_state = next;
            if (next == LOBBY_STATE_LOCAL_LOBBY) sapp_state_local_lobby_enter();
        }
        break;
    }
    case LOBBY_STATE_CONNECTING: {
        lobby_state_t next = sapp_state_connecting_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_CONNECTING) {
            g_state = next;
            if (next == LOBBY_STATE_LOCAL_LOBBY) sapp_state_local_lobby_enter();
        }
        break;
    }
    case LOBBY_STATE_LOBBY_LIST: {
        lobby_state_t next = sapp_state_lobby_list_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_LOBBY_LIST) {
            g_state = next;
            if (next == LOBBY_STATE_LOCAL_LOBBY) sapp_state_local_lobby_enter();
        }
        break;
    }
    case LOBBY_STATE_ROOM_CREATE: {
        lobby_state_t next = sapp_state_room_create_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_ROOM_CREATE) {
            g_state = next;
            if (next == LOBBY_STATE_LOCAL_LOBBY) sapp_state_local_lobby_enter();
        }
        break;
    }
    case LOBBY_STATE_IN_ROOM: {
        lobby_state_t next = sapp_state_in_room_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_IN_ROOM) {
            g_state = next;
            if (next == LOBBY_STATE_LOCAL_LOBBY) sapp_state_local_lobby_enter();
        }
        break;
    }
    case LOBBY_STATE_GAME_SELECT_ONLINE: {
        lobby_state_t next = sapp_state_game_select_online_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_GAME_SELECT_ONLINE) {
            g_state = next;
            if (next == LOBBY_STATE_VOTE_TIMER)   sapp_state_vote_timer_enter();
            else if (next == LOBBY_STATE_COUNTDOWN) sapp_state_countdown_enter();
            else if (next == LOBBY_STATE_PLAYING_ONLINE) sapp_state_playing_online_enter();
            else if (next == LOBBY_STATE_IN_ROOM) sapp_state_in_room_enter();
        }
        break;
    }
    case LOBBY_STATE_VOTE_TIMER: {
        lobby_state_t next = sapp_state_vote_timer_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_VOTE_TIMER) {
            g_state = next;
            if (next == LOBBY_STATE_COUNTDOWN) sapp_state_countdown_enter();
            else if (next == LOBBY_STATE_PLAYING_ONLINE) sapp_state_playing_online_enter();
            else if (next == LOBBY_STATE_IN_ROOM) sapp_state_in_room_enter();
        }
        break;
    }
    case LOBBY_STATE_COUNTDOWN: {
        lobby_state_t next = sapp_state_countdown_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_COUNTDOWN) {
            g_state = next;
            if (next == LOBBY_STATE_PLAYING_ONLINE) sapp_state_playing_online_enter();
            else if (next == LOBBY_STATE_IN_ROOM) sapp_state_in_room_enter();
        }
        break;
    }
    case LOBBY_STATE_PLAYING_ONLINE: {
        lobby_state_t next = sapp_state_playing_online_input(inputs, g_prev_inputs);
        if (next != LOBBY_STATE_PLAYING_ONLINE) {
            g_state = next;
            if (next == LOBBY_STATE_RESULTS_ONLINE) sapp_state_results_online_enter();
            else if (next == LOBBY_STATE_IN_ROOM) sapp_state_in_room_enter();
        }
        break;
    }
    case LOBBY_STATE_RESULTS_ONLINE: {
        lobby_state_t next = sapp_state_results_online_input(menu_now, menu_prev);
        if (next != LOBBY_STATE_RESULTS_ONLINE) {
            g_state = next;
            if (next == LOBBY_STATE_IN_ROOM) sapp_state_in_room_enter();
        }
        break;
    }
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
    case LOBBY_STATE_NAME_ENTRY_FIRST_RUN:
        sapp_state_name_entry_first_run_render(&g_scene);
        break;
    case LOBBY_STATE_LOCAL_LOBBY:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_local_lobby_render(&g_scene);
        break;
    case LOBBY_STATE_NAME_PICK:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_name_pick_render(&g_scene);
        break;
    case LOBBY_STATE_NAME_ENTRY_NEW:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_name_entry_new_render(&g_scene);
        break;
    case LOBBY_STATE_CONNECTING:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_connecting_render(&g_scene);
        break;
    case LOBBY_STATE_LOBBY_LIST:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_lobby_list_render(&g_scene);
        break;
    case LOBBY_STATE_ROOM_CREATE:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_room_create_render(&g_scene);
        break;
    case LOBBY_STATE_IN_ROOM:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_in_room_render(&g_scene);
        break;
    case LOBBY_STATE_GAME_SELECT_ONLINE:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_game_select_online_render(&g_scene);
        break;
    case LOBBY_STATE_VOTE_TIMER:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_vote_timer_render(&g_scene);
        break;
    case LOBBY_STATE_COUNTDOWN:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_countdown_render(&g_scene);
        break;
    case LOBBY_STATE_PLAYING_ONLINE:
        sapp_state_playing_online_render(&g_scene);
        break;
    case LOBBY_STATE_RESULTS_ONLINE:
        lobby_scene_clear(&g_scene, rgb555(8, 8, 32));
        sapp_state_results_online_render(&g_scene);
        break;
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

    /* Resolve NBG1 backdrop request from current state. Pointer-
     * identity drives upload skipping in the platform shell, so we set
     * the same const pointer every frame as long as the state hasn't
     * changed. */
    switch (g_state) {
    case LOBBY_STATE_TITLE:
        g_scene.bg_image_ref = &g_title_bg;
        break;
    case LOBBY_STATE_PLAYING:
        if (g_active_game >= 0) {
            g_scene.bg_image_ref = g_games[g_active_game]->background_image;
        } else {
            g_scene.bg_image_ref = NULL;
        }
        break;
    case LOBBY_STATE_SELECT:
    case LOBBY_STATE_GAME_OVER:
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
