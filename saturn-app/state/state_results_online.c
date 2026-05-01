/*
 * state/state_results_online.c — brief results screen, then back to IN_ROOM.
 *
 * Per Decision #20: after RESULTS we always return to IN_ROOM so the room
 * can pick another game. START / A advances; the screen also auto-
 * advances after a fixed number of frames so the host stays kinetic.
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"

#include <stdio.h>

#define INP_START 0x0010
#define INP_A     0x0020
#define INP_B     0x0040

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

static uint16_t s_frames;

static int pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask) {
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

void sapp_state_results_online_enter(void)
{
    s_frames = 0;
    sapp_online_set_status("RESULTS");
}

lobby_state_t sapp_state_results_online_input(lobby_input_t now, lobby_input_t prev)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    sapp_net_poll();
    s_frames++;
    if (pressed(now, prev, INP_START) || pressed(now, prev, INP_A)
        || pressed(now, prev, INP_B) || s_frames > 180) {
        ctx->round_ended = false;
        return LOBBY_STATE_LOBBY;
    }
    return LOBBY_STATE_RESULTS_ONLINE;
}

void sapp_state_results_online_render(lobby_scene_t* scene)
{
    if (!scene) return;
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    char buf[40];

    lobby_scene_text(scene, 0, 0, PAL_HIGHLIGHT, "ROUND OVER");
    lobby_scene_text(scene, 0, 1, PAL_DIM, "-------------------");
    snprintf(buf, sizeof(buf), "OUTCOME=%u WINNER=SLOT %u",
             (unsigned)ctx->round_outcome, (unsigned)ctx->round_winner);
    lobby_scene_text(scene, 0, 6, PAL_DEFAULT, buf);
    lobby_scene_text(scene, 0, 24, PAL_DIM, "PRESS START");
}
