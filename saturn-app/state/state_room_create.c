/*
 * libs/saturn-app/state/state_room_create.c — keyboard for room name.
 *
 * Reuses sapp_kbd_t with allow_cancel=true. On commit, send ROOM_CREATE
 * with the typed name, mark ctx->creating, and wait for ROOM_STATE.
 * On cancel: back to LOBBY_LIST.
 *
 * The server's reply will be a ROOM_STATE for the freshly-created room,
 * since CREATE auto-joins the creator. The recv router updates
 * ctx->room.valid and clears ctx->creating; the LOBBY_LIST state will
 * then advance to IN_ROOM. We bounce through LOBBY_LIST for that path
 * so the create state itself doesn't carry the "post-CREATE" wait.
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <saturn_app/widgets/keyboard.h>

#include <stdio.h>
#include <string.h>

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

static sapp_kbd_t s_kbd;
static bool       s_inited;

void sapp_state_room_create_enter(void)
{
    sapp_kbd_layout_t L = SAPP_KBD_DEFAULT_LAYOUT(true);
    L.cap_chars = SAPP_ROOM_NAME_CAP - 1;
    if (L.cap_chars > SAPP_KBD_BUF_CAP - 1) L.cap_chars = SAPP_KBD_BUF_CAP - 1;
    sapp_kbd_init(&s_kbd, NULL, L);
    s_inited = true;

    /* Clear any prior room state so the IN_ROOM transition only fires
     * when the new ROOM_STATE arrives. */
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    memset(&ctx->room, 0, sizeof(ctx->room));
    ctx->joining  = false;
    ctx->creating = false;
}

lobby_state_t sapp_state_room_create_input(lobby_input_t now,
                                           lobby_input_t prev)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    if (!s_inited) sapp_state_room_create_enter();

    sapp_net_poll();

    sapp_kbd_input(&s_kbd, now, prev);

    /* Stash the keyboard buffer so tests can inspect what's been typed. */
    {
        size_t i = 0;
        for (; i + 1 < sizeof(ctx->room_create_buf) && s_kbd.buffer[i]; ++i)
            ctx->room_create_buf[i] = s_kbd.buffer[i];
        ctx->room_create_buf[i] = '\0';
    }

    if (s_kbd.committed) {
        uint8_t buf[40];
        size_t  n = sapp_proto_encode_room_create(buf, sizeof(buf),
                                                  /*game_id*/ 0,
                                                  /*max_slots*/ SAPP_ROOM_SLOTS_MAX,
                                                  /*visibility*/ SAPP_VIS_PUBLIC,
                                                  s_kbd.buffer);
        s_inited = false;
        if (n && sapp_net_send_frame(buf, n)) {
            ctx->creating = true;
            sapp_online_set_status("CREATING...");
        } else {
            sapp_online_set_status("CREATE SEND FAILED");
        }
        return LOBBY_STATE_LOBBY_LIST;
    }
    if (s_kbd.cancelled) {
        s_inited = false;
        return LOBBY_STATE_LOBBY_LIST;
    }
    return LOBBY_STATE_ROOM_CREATE;
}

void sapp_state_room_create_render(lobby_scene_t* scene)
{
    if (!scene) return;
    if (!s_inited) sapp_state_room_create_enter();
    lobby_scene_text(scene, 0, 0, PAL_HIGHLIGHT, "CREATE ROOM");
    lobby_scene_text(scene, 0, 1, PAL_DIM, "-------------------");
    sapp_kbd_render(&s_kbd, scene, "ROOM NAME");
    lobby_scene_text(scene, 0, 24, PAL_DIM,
                     "START:OK  C:CANCEL");
}
