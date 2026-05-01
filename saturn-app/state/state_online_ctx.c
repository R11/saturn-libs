/*
 * libs/saturn-app/state/state_online_ctx.c — shared online context.
 *
 * Owns the single sapp_online_ctx_t for the framework. Routes inbound
 * frames by type; encodes outbound HELLO from current identity + lobby
 * seating. Public accessors live in include/saturn_app/online.h.
 */

#include "state_online.h"
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <stdio.h>
#include <string.h>

static sapp_online_ctx_t g_ctx;

sapp_online_ctx_t* sapp_online_ctx(void) { return &g_ctx; }

void sapp_online_ctx_reset(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.vote_timer_secs   = 0xFF;
    g_ctx.countdown_secs    = 0xFF;
    g_ctx.picked_game_id    = 0xFF;
    g_ctx.last_drop_reason  = 0xFF;
}

static void recv_router(const uint8_t* body, size_t len, void* user) {
    (void)user;
    sapp_online_handle_frame(body, len);
}

void sapp_online_install_recv(void)
{
    sapp_net_set_recv(recv_router, NULL);
}

const sapp_room_list_t*  sapp_online_room_list (void) { return &g_ctx.list; }
const sapp_room_state_t* sapp_online_room_state(void) { return &g_ctx.room; }
const char*              sapp_online_status    (void) { return g_ctx.status; }
bool                     sapp_online_handshaken(void) { return g_ctx.handshaken; }

const char* sapp_online_room_create_buffer(void)
{
    return g_ctx.room_create_buf[0] ? g_ctx.room_create_buf : NULL;
}

uint8_t  sapp_online_vote_timer_secs(void) { return g_ctx.vote_timer_secs; }
uint8_t  sapp_online_countdown_secs(void)  { return g_ctx.countdown_secs; }
uint8_t  sapp_online_picked_game_id(void)  { return g_ctx.picked_game_id; }
uint32_t sapp_online_round_seed(void)      { return g_ctx.round_seed; }
uint32_t sapp_online_lockstep_tick(void)   { return g_ctx.lockstep_tick; }
const sapp_last_bundle_t* sapp_online_last_bundle(void) { return &g_ctx.last_bundle; }
bool    sapp_online_round_ended(void)      { return g_ctx.round_ended; }
uint8_t sapp_online_round_outcome(void)    { return g_ctx.round_outcome; }
uint8_t sapp_online_round_winner(void)     { return g_ctx.round_winner; }

bool sapp_online_slot_is_dropped(uint8_t slot)
{
    if (slot >= SAPP_ROOM_SLOTS_MAX) return false;
    return g_ctx.dropped[slot];
}

uint8_t sapp_online_last_drop_reason(void) { return g_ctx.last_drop_reason; }

void sapp_online_set_status(const char* msg)
{
    if (!msg) { g_ctx.status[0] = '\0'; return; }
    size_t i;
    for (i = 0; i + 1 < sizeof(g_ctx.status) && msg[i]; ++i) {
        g_ctx.status[i] = msg[i];
    }
    g_ctx.status[i] = '\0';
}

bool sapp_online_send_hello(void)
{
    const sapp_identity_t*    id    = sapp_get_identity();
    const sapp_local_lobby_t* lobby = sapp_lobby_get();
    const char*               names[SAPP_ROOM_SLOTS_MAX];
    uint8_t                   n_local = 0;
    uint8_t                   buf[256];
    uint8_t                   uuid[16];
    size_t                    body_len;

    if (!id) {
        sapp_identity_t tmp;
        sapp_identity_default(&tmp);
        memcpy(uuid, tmp.session_uuid, 16);
    } else {
        memcpy(uuid, id->session_uuid, 16);
    }

    if (lobby) {
        for (uint8_t s = 0; s < SAPP_LOBBY_SLOTS && n_local < SAPP_ROOM_SLOTS_MAX; ++s) {
            if (lobby->seated[s] && lobby->seated_name[s][0]) {
                names[n_local++] = lobby->seated_name[s];
            }
        }
    }
    if (n_local == 0) {
        /* Fall back to current_name. */
        if (id && id->current_name[0]) names[n_local++] = id->current_name;
        else                            names[n_local++] = "P1";
    }

    body_len = sapp_proto_encode_hello(buf, sizeof(buf),
                                       SAPP_PROTO_VER, 0u,
                                       uuid, n_local, names);
    if (!body_len) return false;
    return sapp_net_send_frame(buf, body_len);
}

void sapp_online_handle_frame(const uint8_t* body, size_t len)
{
    uint8_t type;
    if (len < 1) return;
    type = body[0];
    switch (type) {
    case SAPP_MSG_HELLO_ACK: {
        sapp_hello_ack_t ack;
        if (sapp_proto_decode_hello_ack(body, len, &ack)) {
            g_ctx.handshaken = true;
            /* Persist the server's UUID into the framework identity if
             * we don't have one yet. (Optional in M4 — we use whatever
             * the cart already has.) */
            sapp_online_set_status("CONNECTED");
        }
        break;
    }
    case SAPP_MSG_LOBBY_LIST: {
        sapp_room_list_t list;
        if (sapp_proto_decode_lobby_list(body, len, &list)) {
            g_ctx.list = list;
        }
        break;
    }
    case SAPP_MSG_ROOM_STATE: {
        sapp_room_state_t rs;
        if (sapp_proto_decode_room_state(body, len, &rs)) {
            g_ctx.room    = rs;
            g_ctx.joining = false;
            g_ctx.creating = false;
        }
        break;
    }
    case SAPP_MSG_VOTE_TIMER: {
        uint8_t s;
        if (sapp_proto_decode_vote_timer(body, len, &s)) {
            g_ctx.vote_timer_secs = s;
        }
        break;
    }
    case SAPP_MSG_GAME_PICK: {
        sapp_game_pick_t p;
        if (sapp_proto_decode_game_pick(body, len, &p)) {
            g_ctx.picked_game_id    = p.game_id;
            g_ctx.round_seed        = p.seed;
            g_ctx.game_pick_pending = true;
            g_ctx.vote_timer_secs   = 0xFF;
        }
        break;
    }
    case SAPP_MSG_COUNTDOWN: {
        uint8_t s;
        if (sapp_proto_decode_countdown(body, len, &s)) {
            g_ctx.countdown_secs = s;
        }
        break;
    }
    case SAPP_MSG_GAME_START: {
        sapp_game_start_t gs;
        if (sapp_proto_decode_game_start(body, len, &gs)) {
            g_ctx.start              = gs;
            g_ctx.picked_game_id     = gs.game_id;
            g_ctx.round_seed         = gs.seed;
            g_ctx.lockstep_tick      = gs.start_tick;
            g_ctx.game_start_pending = true;
            g_ctx.countdown_secs     = 0xFF;
            /* M6: fresh round clears any prior drops. */
            for (uint8_t i = 0; i < SAPP_ROOM_SLOTS_MAX; ++i)
                g_ctx.dropped[i] = false;
            g_ctx.last_drop_reason = 0xFF;
        }
        break;
    }
    case SAPP_MSG_SLOT_DROP: {
        sapp_slot_drop_t d;
        if (sapp_proto_decode_slot_drop(body, len, &d)) {
            if (d.slot < SAPP_ROOM_SLOTS_MAX) {
                g_ctx.dropped[d.slot] = true;
            }
            g_ctx.last_drop_reason = d.reason;
        }
        break;
    }
    case SAPP_MSG_BUNDLE: {
        sapp_bundle_t b;
        if (sapp_proto_decode_bundle(body, len, &b)) {
            g_ctx.last_bundle.valid = true;
            g_ctx.last_bundle.tick  = b.tick;
            g_ctx.last_bundle.n     = b.n;
            for (uint8_t i = 0; i < b.n && i < SAPP_ROOM_SLOTS_MAX; ++i) {
                g_ctx.last_bundle.inputs[i] = b.inputs[i];
            }
        }
        break;
    }
    case SAPP_MSG_ROUND_END: {
        sapp_round_end_t r;
        if (sapp_proto_decode_round_end(body, len, &r)) {
            g_ctx.round_ended    = true;
            g_ctx.round_outcome  = r.outcome;
            g_ctx.round_winner   = r.winner;
        }
        break;
    }
    case SAPP_MSG_ERROR: {
        /* [type][code][len][detail...] */
        if (len >= 3) {
            uint8_t code = body[1];
            char    msg[64];
            snprintf(msg, sizeof(msg), "SERVER ERROR 0x%02X", code);
            sapp_online_set_status(msg);
            g_ctx.joining  = false;
            g_ctx.creating = false;
        }
        break;
    }
    default:
        /* Unknown message type — silently drop per protocol policy. */
        break;
    }
}
