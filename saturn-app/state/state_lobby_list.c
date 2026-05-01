/*
 * libs/saturn-app/state/state_lobby_list.c — server-driven public room list.
 *
 * Sends a LOBBY_LIST_REQ on enter (and whenever the user presses START
 * to refresh). The recv router updates ctx->list. UP/DOWN selects a row;
 * A sends ROOM_JOIN; Y (=START) opens ROOM_CREATE; B disconnects + back
 * to LOCAL_LOBBY.
 *
 * After ROOM_JOIN, the server replies with ROOM_STATE; the recv router
 * updates ctx->room and clears ctx->joining. We then transition to
 * IN_ROOM in the next input cycle.
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
#define INP_C     0x0080  /* not on standard saturn pad; refresh via START */

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

static uint8_t s_cursor;

static int pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask) {
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

static void send_list_req(void) {
    uint8_t buf[4];
    size_t  n = sapp_proto_encode_lobby_list_req(buf, sizeof(buf), 0xFF, 0);
    if (n) sapp_net_send_frame(buf, n);
}

void sapp_state_lobby_list_enter(void)
{
    s_cursor = 0;
    sapp_online_set_status("ROOM LIST");
    send_list_req();
}

lobby_state_t sapp_state_lobby_list_input(lobby_input_t now,
                                          lobby_input_t prev)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();

    sapp_net_poll();

    /* If a JOIN reply landed (ctx->room.valid && we asked), advance. */
    if (ctx->room.valid && !ctx->joining && !ctx->creating) {
        sapp_state_in_room_enter();
        return LOBBY_STATE_IN_ROOM;
    }

    if (pressed(now, prev, INP_B)) {
        sapp_net_disconnect();
        return LOBBY_STATE_LOCAL_LOBBY;
    }
    if (pressed(now, prev, INP_START)) {
        /* Treat START as Y → CREATE. */
        sapp_state_room_create_enter();
        return LOBBY_STATE_ROOM_CREATE;
    }
    if (ctx->list.n > 0) {
        if (pressed(now, prev, INP_UP)   && s_cursor > 0)
            s_cursor--;
        if (pressed(now, prev, INP_DOWN) && s_cursor + 1 < ctx->list.n)
            s_cursor++;
        if (pressed(now, prev, INP_A)) {
            const sapp_room_summary_t* r = &ctx->list.rooms[s_cursor];
            uint8_t buf[16];
            size_t  n = sapp_proto_encode_room_join(buf, sizeof(buf),
                                                    r->room_id);
            if (n && sapp_net_send_frame(buf, n)) {
                ctx->joining = true;
                sapp_online_set_status("JOINING...");
            } else {
                sapp_online_set_status("JOIN SEND FAILED");
            }
        }
    }
    return LOBBY_STATE_LOBBY_LIST;
}

void sapp_state_lobby_list_render(lobby_scene_t* scene)
{
    const sapp_online_ctx_t* ctx = sapp_online_ctx();
    if (!scene) return;

    lobby_scene_text(scene, 0, 0, PAL_HIGHLIGHT, "LOBBY LIST");
    lobby_scene_text(scene, 0, 1, PAL_DIM, "-------------------");

    if (ctx->list.n == 0) {
        lobby_scene_text(scene, 0, 4, PAL_DIM, "(no public rooms)");
    } else {
        for (uint8_t i = 0; i < ctx->list.n && i < 12; ++i) {
            const sapp_room_summary_t* r = &ctx->list.rooms[i];
            char buf[40];
            snprintf(buf, sizeof(buf), "%s %.10s %u/%u",
                     (i == s_cursor) ? ">" : " ",
                     r->name[0] ? r->name : "(no name)",
                     (unsigned)r->occ, (unsigned)r->max);
            lobby_scene_text(scene, 0, (uint8_t)(3 + i),
                             (i == s_cursor) ? PAL_HIGHLIGHT : PAL_DEFAULT,
                             buf);
        }
    }

    lobby_scene_text(scene, 0, 24, PAL_DIM,
                     "A:JOIN  START:CREATE  B:BACK");
    {
        const char* st = sapp_online_status();
        if (st && st[0])
            lobby_scene_text(scene, 0, 26, PAL_DIM, st);
    }
}
