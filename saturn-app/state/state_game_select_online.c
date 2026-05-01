/*
 * libs/saturn-app/state/state_game_select_online.c — per-player game pick.
 *
 * Per Decision #16: each player picks a game with A (cursor through the
 * registered games), then START commits the pick as a READY+vote. While
 * not yet locked, START toggles ready (re-sending the READY frame each
 * time so the server tracks the latest state).
 *
 * Server-driven transitions:
 *   - VOTE_TIMER 5      -> STATE_VOTE_TIMER
 *   - GAME_PICK         -> STATE_COUNTDOWN
 *   - GAME_START        -> STATE_PLAYING_ONLINE (skip-ahead path if we
 *                          missed the timer/pick frames somehow).
 *   - B                 -> STATE_IN_ROOM (cancel — does not LEAVE)
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

static uint8_t s_cursor;     /* index into registered games */
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

void sapp_state_game_select_online_enter(void)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    s_cursor      = 0;
    s_ready       = false;
    s_voted_game  = 0;
    ctx->vote_timer_secs    = 0xFF;
    ctx->countdown_secs     = 0xFF;
    ctx->game_pick_pending  = false;
    ctx->game_start_pending = false;
    ctx->round_ended        = false;
    sapp_online_set_status("PICK A GAME");
}

lobby_state_t sapp_state_game_select_online_input(lobby_input_t now,
                                                  lobby_input_t prev)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    uint8_t            n_games = sapp_count_games();

    sapp_net_poll();

    /* Server-driven transitions take precedence. */
    if (ctx->game_start_pending) {
        return LOBBY_STATE_PLAYING_ONLINE;
    }
    if (ctx->game_pick_pending) {
        return LOBBY_STATE_COUNTDOWN;
    }
    if (ctx->vote_timer_secs != 0xFF) {
        return LOBBY_STATE_VOTE_TIMER;
    }

    if (pressed(now, prev, INP_B)) {
        if (s_ready) send_ready(0, s_voted_game); /* be polite */
        return LOBBY_STATE_IN_ROOM;
    }

    if (n_games > 0) {
        if (pressed(now, prev, INP_UP)   && s_cursor > 0) s_cursor--;
        if (pressed(now, prev, INP_DOWN) && s_cursor + 1 < n_games) s_cursor++;
        if (pressed(now, prev, INP_A)) {
            /* A also un-readies (lets you change mind without START). */
            if (s_ready) {
                s_ready = false;
                send_ready(0, s_voted_game);
                sapp_online_set_status("UN-READY");
            }
        }
        if (pressed(now, prev, INP_START)) {
            s_voted_game = s_cursor;
            s_ready      = !s_ready;
            send_ready(s_ready ? 1 : 0, s_voted_game);
            sapp_online_set_status(s_ready ? "READY" : "UN-READY");
        }
    }
    return LOBBY_STATE_GAME_SELECT_ONLINE;
}

void sapp_state_game_select_online_render(lobby_scene_t* scene)
{
    uint8_t n_games = sapp_count_games();
    if (!scene) return;

    lobby_scene_text(scene, 0, 0, PAL_HIGHLIGHT, "PICK A GAME");
    lobby_scene_text(scene, 0, 1, PAL_DIM, "-------------------");

    if (n_games == 0) {
        lobby_scene_text(scene, 0, 4, PAL_DIM, "(no games registered)");
    }
    for (uint8_t i = 0; i < n_games && i < 12; ++i) {
        const lobby_game_t* g = sapp_get_game(i);
        char buf[40];
        const char* arrow = (i == s_cursor) ? ">" : " ";
        const char* tag   = (s_ready && i == s_voted_game) ? " [VOTE]" : "";
        snprintf(buf, sizeof(buf), "%s %s%s",
                 arrow,
                 g && g->display_name ? g->display_name : "?",
                 tag);
        lobby_scene_text(scene, 0, (uint8_t)(3 + i),
                         (i == s_cursor) ? PAL_HIGHLIGHT : PAL_DEFAULT, buf);
    }

    lobby_scene_text(scene, 0, 22, PAL_DIM,
                     s_ready ? "START:UN-READY" : "START:READY+VOTE");
    lobby_scene_text(scene, 0, 23, PAL_DIM, "B:BACK");
    {
        const char* st = sapp_online_status();
        if (st && st[0]) lobby_scene_text(scene, 0, 25, PAL_DIM, st);
    }
}
