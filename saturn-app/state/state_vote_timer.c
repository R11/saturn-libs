/*
 * state/state_vote_timer.c — 5s extension while >50% players are ready.
 *
 * Players can still un-ready/change pick during this window. We delegate
 * cursor + ready-toggle behaviour to the same helpers used by
 * STATE_GAME_SELECT_ONLINE for parity (Decision #17).
 *
 * Server-driven exit:
 *   - GAME_PICK arrives -> STATE_COUNTDOWN
 *   - GAME_START (skip-ahead) -> STATE_PLAYING_ONLINE
 *   - VOTE_TIMER cleared (server cancels it because <=50% ready) ->
 *     back to STATE_GAME_SELECT_ONLINE
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <stdio.h>
#include <string.h>

#define INP_DOWN  0x0004
#define INP_UP    0x0008
#define INP_START 0x0010
#define INP_A     0x0020
#define INP_B     0x0040

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

static uint8_t s_cursor;
static bool    s_ready;
static uint8_t s_voted_game;

static int pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask) {
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

static void send_ready(uint8_t ready, uint8_t game_id)
{
    uint8_t buf[4];
    size_t n = sapp_proto_encode_ready(buf, sizeof(buf), ready, game_id);
    if (n) (void)sapp_net_send_frame(buf, n);
}

void sapp_state_vote_timer_enter(void)
{
    /* Preserve cursor/ready/vote across entry — feels nicer to players. */
    sapp_online_set_status("VOTE TIMER");
}

lobby_state_t sapp_state_vote_timer_input(lobby_input_t now, lobby_input_t prev)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    uint8_t            n_games = sapp_count_games();

    sapp_net_poll();

    if (ctx->game_start_pending) return LOBBY_STATE_PLAYING_ONLINE;
    if (ctx->game_pick_pending)  return LOBBY_STATE_COUNTDOWN;
    if (ctx->vote_timer_secs == 0xFF) {
        /* Timer cancelled — back to game select. */
        return LOBBY_STATE_GAME_SELECT_ONLINE;
    }

    if (pressed(now, prev, INP_B)) {
        if (s_ready) send_ready(0, s_voted_game);
        return LOBBY_STATE_IN_ROOM;
    }
    if (n_games > 0) {
        if (pressed(now, prev, INP_UP)   && s_cursor > 0) s_cursor--;
        if (pressed(now, prev, INP_DOWN) && s_cursor + 1 < n_games) s_cursor++;
        if (pressed(now, prev, INP_START)) {
            s_voted_game = s_cursor;
            s_ready = !s_ready;
            send_ready(s_ready ? 1 : 0, s_voted_game);
        }
    }
    return LOBBY_STATE_VOTE_TIMER;
}

void sapp_state_vote_timer_render(lobby_scene_t* scene)
{
    if (!scene) return;
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    char buf[40];
    snprintf(buf, sizeof(buf), "VOTE TIMER: %u",
             (unsigned)(ctx->vote_timer_secs & 0xFF));
    lobby_scene_text(scene, 0, 0, PAL_HIGHLIGHT, buf);
    lobby_scene_text(scene, 0, 1, PAL_DIM, "-------------------");

    uint8_t n_games = sapp_count_games();
    for (uint8_t i = 0; i < n_games && i < 12; ++i) {
        const lobby_game_t* g = sapp_get_game(i);
        char line[40];
        const char* arrow = (i == s_cursor) ? ">" : " ";
        const char* tag   = (s_ready && i == s_voted_game) ? " [VOTE]" : "";
        snprintf(line, sizeof(line), "%s %s%s",
                 arrow, g && g->display_name ? g->display_name : "?", tag);
        lobby_scene_text(scene, 0, (uint8_t)(3 + i),
                         (i == s_cursor) ? PAL_HIGHLIGHT : PAL_DEFAULT, line);
    }

    lobby_scene_text(scene, 0, 23, PAL_DIM,
                     s_ready ? "START:UN-READY" : "START:READY+VOTE");
}

/* Internal helper for game_select_online to keep cursor between modules.
 * (Currently only used to suppress -Wunused warnings on s_cursor/s_ready
 * in unit-test builds.) */
void sapp_state_vote_timer__test_state(uint8_t* cur, uint8_t* ready, uint8_t* vote)
{
    if (cur)   *cur   = s_cursor;
    if (ready) *ready = s_ready ? 1 : 0;
    if (vote)  *vote  = s_voted_game;
}
