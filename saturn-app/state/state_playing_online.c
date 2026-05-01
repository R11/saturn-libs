/*
 * state/state_playing_online.c — lockstep PLAYING via server BUNDLE.
 *
 * Per-frame:
 *   1. Pump network. Apply any received BUNDLE for ctx->lockstep_tick.
 *   2. If a bundle for the current tick is in hand, build per-slot input
 *      array, tick the game, render, advance lockstep_tick.
 *   3. Send our local seat's INPUT for the new tick (single seat for M5,
 *      slot determined from ROOM_STATE membership for our cart).
 *   4. Check is_done; on done, send ROUND_END once.
 *   5. On receiving ROUND_END from server -> RESULTS_ONLINE.
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <saturn_app.h>

#include <stdio.h>
#include <string.h>

#define INP_B 0x0040

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

/* Per-game arena private to this module. Sized to match SAPP_GAME_STATE_BYTES. */
static uint8_t              s_game_state[SAPP_GAME_STATE_BYTES];
static const lobby_game_t*  s_game;
static uint32_t             s_tick_sent_for; /* last tick we sent INPUT for */
static bool                 s_round_end_sent;
static lobby_input_t        s_local_input;   /* most recent local pad */

static int our_slot(void)
{
    /* Find this cart's first seated slot. We don't know our cart_id
     * directly, but the cart_id assigned by the server matches the order
     * carts joined. For M5 we assume the cart's HELLO uuid identifies it
     * uniquely; the server's ROOM_STATE includes uuid per member. We
     * match on uuid. */
    const sapp_room_state_t* rs = sapp_online_room_state();
    const sapp_identity_t*   id = sapp_get_identity();
    if (!rs || !rs->valid || !id) return 0;
    for (uint8_t i = 0; i < rs->n_members; ++i) {
        if (memcmp(rs->members[i].uuid, id->session_uuid, 16) == 0) {
            return rs->members[i].slot;
        }
    }
    return 0;
}

static void send_input(uint32_t tick, uint8_t slot, uint16_t input)
{
    uint8_t buf[16];
    size_t n = sapp_proto_encode_input(buf, sizeof(buf), tick, slot, input);
    if (n) (void)sapp_net_send_frame(buf, n);
}

static void send_round_end(uint8_t outcome, uint8_t winner)
{
    uint8_t buf[8];
    size_t n = sapp_proto_encode_round_end(buf, sizeof(buf), outcome, winner);
    if (n) (void)sapp_net_send_frame(buf, n);
}

void sapp_state_playing_online_enter(void)
{
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    lobby_game_config_t cfg;

    s_game = sapp_get_game(ctx->picked_game_id);
    if (!s_game) {
        sapp_online_set_status("BAD GAME ID");
        return;
    }

    cfg.seed       = ctx->round_seed;
    cfg.n_players  = ctx->start.n_slots ? ctx->start.n_slots : 1;
    cfg.difficulty = 0;
    memset(s_game_state, 0, sizeof(s_game_state));
    if (s_game->init) s_game->init(s_game_state, &cfg);

    ctx->lockstep_tick      = ctx->start.start_tick;
    ctx->game_start_pending = false;
    ctx->last_bundle.valid  = false;
    ctx->round_ended        = false;

    s_tick_sent_for  = (uint32_t)-1;
    s_round_end_sent = false;
    s_local_input    = 0;
    sapp_online_set_status("PLAYING");
}

lobby_state_t sapp_state_playing_online_input(
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev[LOBBY_MAX_PLAYERS])
{
    (void)prev;
    sapp_online_ctx_t* ctx = sapp_online_ctx();

    if (!s_game) return LOBBY_STATE_LOBBY;

    s_local_input = inputs ? inputs[0] : 0;

    sapp_net_poll();

    if (ctx->round_ended) {
        return LOBBY_STATE_RESULTS_ONLINE;
    }

    /* Send our INPUT for the current expected tick exactly once. */
    if (s_tick_sent_for != ctx->lockstep_tick) {
        send_input(ctx->lockstep_tick, (uint8_t)our_slot(), s_local_input);
        s_tick_sent_for = ctx->lockstep_tick;
    }

    /* If we have a bundle for the current tick, tick the game. */
    if (ctx->last_bundle.valid && ctx->last_bundle.tick == ctx->lockstep_tick) {
        lobby_input_t slot_inputs[LOBBY_MAX_PLAYERS] = {0};
        for (uint8_t i = 0; i < ctx->last_bundle.n
                          && i < LOBBY_MAX_PLAYERS; ++i) {
            slot_inputs[i] = ctx->last_bundle.inputs[i];
        }
        if (s_game->tick) s_game->tick(s_game_state, slot_inputs);

        /* Check end-of-game and notify server once. */
        if (s_game->is_done && !s_round_end_sent) {
            lobby_game_result_t r;
            memset(&r, 0, sizeof(r));
            s_game->is_done(s_game_state, &r);
            if (r.outcome != LOBBY_OUTCOME_RUNNING) {
                send_round_end(r.outcome, r.winner_slot);
                s_round_end_sent = true;
            }
        }

        ctx->last_bundle.valid = false;
        ctx->lockstep_tick++;
    }

    return LOBBY_STATE_PLAYING_ONLINE;
}

void sapp_state_playing_online_render(lobby_scene_t* scene)
{
    if (!scene) return;
    if (s_game && s_game->render_scene) {
        s_game->render_scene(s_game_state, scene);
    } else {
        lobby_scene_text(scene, 0, 0, PAL_DIM, "(no game)");
    }
    /* M6: overlay a "DROPPED <name>" line per dropped slot, top-right
     * column, dim palette. */
    {
        const sapp_room_state_t* rs = sapp_online_room_state();
        uint8_t row = 0;
        if (rs && rs->valid) {
            for (uint8_t i = 0; i < rs->n_members; ++i) {
                const sapp_room_member_t* m = &rs->members[i];
                if (sapp_online_slot_is_dropped(m->slot)) {
                    char line[24];
                    snprintf(line, sizeof(line), "DROPPED %s",
                             m->name[0] ? m->name : "?");
                    lobby_scene_text(scene, 25, row++, PAL_DIM, line);
                }
            }
        }
    }
}

/* For test/HUD: peek at the per-state game arena (e.g. score). */
const void* sapp_playing_online_game_state(void) { return s_game_state; }
const lobby_game_t* sapp_playing_online_game(void) { return s_game; }
