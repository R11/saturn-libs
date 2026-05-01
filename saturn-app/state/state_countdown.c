/*
 * state/state_countdown.c — 3s countdown after GAME_PICK.
 *
 * Server is the timing authority. We just display the most recent
 * countdown_secs and transition to PLAYING_ONLINE when GAME_START
 * arrives. (If COUNTDOWN reaches 0 without a GAME_START following
 * promptly we still wait — the start frame is the source of truth.)
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"

#include <stdio.h>

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

void sapp_state_countdown_enter(void)
{
    sapp_online_set_status("GET READY");
}

lobby_state_t sapp_state_countdown_input(lobby_input_t now, lobby_input_t prev)
{
    (void)now; (void)prev;
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    sapp_net_poll();
    if (ctx->game_start_pending) return LOBBY_STATE_PLAYING_ONLINE;
    return LOBBY_STATE_COUNTDOWN;
}

void sapp_state_countdown_render(lobby_scene_t* scene)
{
    if (!scene) return;
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    const lobby_game_t* g = sapp_get_game(ctx->picked_game_id);
    char buf[40];

    snprintf(buf, sizeof(buf), "GAME: %s",
             (g && g->display_name) ? g->display_name : "?");
    lobby_scene_text(scene, 0, 8, PAL_HIGHLIGHT, buf);

    if (ctx->countdown_secs == 0xFF) {
        lobby_scene_text(scene, 0, 12, PAL_DIM, "WAITING...");
    } else {
        snprintf(buf, sizeof(buf), "STARTING IN %u",
                 (unsigned)(ctx->countdown_secs & 0xFF));
        lobby_scene_text(scene, 0, 12, PAL_DEFAULT, buf);
    }
}
