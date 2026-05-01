/*
 * libs/saturn-app/state/state_connecting.c — handshake state.
 *
 * On enter:
 *   - Reset the online ctx.
 *   - Install the framework's recv callback into sapp_net (so any backend
 *     installed by the shell or a test will route inbound frames to
 *     sapp_online_handle_frame).
 *   - Ask the backend to connect (no-op if already connected; tests with
 *     synthetic backends pre-connect).
 *   - Send the extended HELLO with the cart's identity + seated locals.
 *
 * On every poll: pump the net layer once. The recv callback will set
 * ctx->handshaken = true when HELLO_ACK arrives; the next input frame
 * then transitions to LOBBY_LIST.
 *
 * B at any time -> back to LOCAL_LOBBY (with the net session torn down).
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <stdio.h>
#include <string.h>

#define INP_B          0x0040
#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

static int s_connect_attempted;

static int pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask) {
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

static void recv_router(const uint8_t* body, size_t len, void* user) {
    (void)user;
    sapp_online_handle_frame(body, len);
}

void sapp_state_connecting_enter(void)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    memset(ctx, 0, sizeof(*ctx));
    ctx->vote_timer_secs = 0xFF;
    ctx->countdown_secs  = 0xFF;
    ctx->picked_game_id  = 0xFF;
    ctx->last_drop_reason = 0xFF;
    sapp_online_set_status("CONNECTING...");

    sapp_net_set_recv(recv_router, NULL);

    s_connect_attempted = 0;
}

lobby_state_t sapp_state_connecting_input(lobby_input_t now,
                                          lobby_input_t prev)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();

    if (pressed(now, prev, INP_B)) {
        sapp_net_disconnect();
        return LOBBY_STATE_LOCAL_LOBBY;
    }

    /* If no backend is installed (e.g. unit test that hasn't installed
     * anything), just sit in CONNECTING. */
    if (!sapp_net_active()) {
        sapp_online_set_status("NO NETWORK BACKEND");
        return LOBBY_STATE_CONNECTING;
    }

    /* One connect attempt. Subsequent frames just poll. */
    if (!s_connect_attempted) {
        s_connect_attempted = 1;
        if (!sapp_net_connect()) {
            sapp_online_set_status("CONNECT FAILED — B BACK");
            return LOBBY_STATE_CONNECTING;
        }
        if (!sapp_online_send_hello()) {
            sapp_online_set_status("HELLO SEND FAILED — B BACK");
            return LOBBY_STATE_CONNECTING;
        }
        sapp_online_set_status("WAITING HELLO_ACK...");
    }

    sapp_net_poll();

    if (ctx->handshaken) {
        sapp_state_lobby_list_enter();
        return LOBBY_STATE_LOBBY_LIST;
    }
    return LOBBY_STATE_CONNECTING;
}

void sapp_state_connecting_render(lobby_scene_t* scene)
{
    if (!scene) return;
    lobby_scene_text(scene, 12, 10, PAL_HIGHLIGHT, "CONNECTING");
    {
        const char* st = sapp_online_status();
        if (st && st[0]) lobby_scene_text(scene, 4, 14, PAL_DEFAULT, st);
    }
    lobby_scene_text(scene, 4, 24, PAL_DIM, "B: BACK");
}
