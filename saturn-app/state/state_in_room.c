/*
 * libs/saturn-app/state/state_in_room.c — flat member list for the joined room.
 *
 * Per Decision #15, the list is one row per online player, color-coded by
 * cart_id. The action row is LEAVE (replacing CONNECT, per Decision #20).
 *
 * Bindings:
 *   B           send ROOM_LEAVE; transition back to LOBBY_LIST
 *   START       send ROOM_LEAVE (matches LEAVE button)
 *
 * On every input we pump the network so live ROOM_STATE updates land
 * (e.g. another cart joining or leaving).
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <stdio.h>
#include <string.h>

#define INP_START 0x0010
#define INP_A     0x0020
#define INP_B     0x0040

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

static int pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask) {
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

void sapp_state_in_room_enter(void)
{
    sapp_online_set_status("IN ROOM");
}

static void send_leave(void)
{
    uint8_t buf[2];
    size_t  n = sapp_proto_encode_room_leave(buf, sizeof(buf));
    if (n) (void)sapp_net_send_frame(buf, n);
}

lobby_state_t sapp_state_in_room_input(lobby_input_t now, lobby_input_t prev)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    sapp_net_poll();

    if (pressed(now, prev, INP_B)) {
        send_leave();
        memset(&ctx->room, 0, sizeof(ctx->room));
        sapp_online_set_status("LEFT ROOM");
        sapp_state_lobby_list_enter();
        return LOBBY_STATE_LOBBY_LIST;
    }
    if (pressed(now, prev, INP_A) || pressed(now, prev, INP_START)) {
        sapp_state_game_select_online_enter();
        return LOBBY_STATE_GAME_SELECT_ONLINE;
    }
    return LOBBY_STATE_IN_ROOM;
}

void sapp_state_in_room_render(lobby_scene_t* scene)
{
    const sapp_room_state_t* rs = sapp_online_room_state();
    if (!scene) return;

    lobby_scene_text(scene, 0, 0, PAL_HIGHLIGHT, "IN ROOM");
    lobby_scene_text(scene, 0, 1, PAL_DIM, "-------------------");

    if (!rs || !rs->valid) {
        lobby_scene_text(scene, 0, 4, PAL_DIM, "(waiting for state)");
    } else {
        char rid[SAPP_ROOM_ID_LEN + 1];
        for (uint8_t i = 0; i < SAPP_ROOM_ID_LEN; ++i) {
            uint8_t c = rs->room_id[i];
            rid[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        rid[SAPP_ROOM_ID_LEN] = '\0';
        {
            char buf[40];
            snprintf(buf, sizeof(buf), "ROOM %s  %u/%u",
                     rid,
                     (unsigned)rs->n_members,
                     (unsigned)rs->max_slots);
            lobby_scene_text(scene, 0, 2, PAL_DEFAULT, buf);
        }

        for (uint8_t i = 0; i < rs->n_members && i < 8; ++i) {
            const sapp_room_member_t* m = &rs->members[i];
            char buf[40];
            uint8_t pal = PAL_DEFAULT;
            /* Color-code by cart: even carts default palette, odd carts dim
             * for now — the renderer just needs distinct labels in M4. */
            if (m->cart_id & 1) pal = PAL_DIM;
            snprintf(buf, sizeof(buf), "  %u. %.10s  [c%u]",
                     (unsigned)(m->slot + 1),
                     m->name[0] ? m->name : "?",
                     (unsigned)m->cart_id);
            lobby_scene_text(scene, 0, (uint8_t)(4 + i), pal, buf);
        }
    }

    lobby_scene_text(scene, 0, 13, PAL_DIM, "-------------------");
    lobby_scene_text(scene, 0, 14, PAL_HIGHLIGHT, "> LEAVE");
    lobby_scene_text(scene, 0, 24, PAL_DIM, "B:LEAVE");
}
