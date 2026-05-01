/*
 * state/state_online.h — internal coordination for the M4 online states.
 *
 * Not installed in include/. The online states share a single context
 * (room list, room state, status string) and a recv callback that
 * routes incoming frames by message type.
 */

#ifndef SATURN_APP_STATE_ONLINE_H
#define SATURN_APP_STATE_ONLINE_H

#include <stdbool.h>
#include <stdint.h>

#include <saturn_app.h>
#include <saturn_app/online.h>

#include "../net/sapp_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared online context. Each module reaches into this for read; the
 * dispatcher (state_connecting + recv callback) writes. */
typedef struct {
    bool                handshaken;     /* HELLO_ACK received this session */
    sapp_room_list_t    list;
    sapp_room_state_t   room;
    char                status[64];
    char                room_create_buf[SAPP_ROOM_NAME_CAP];
    /* Pending JOIN: recorded so recv can transition to IN_ROOM on the
     * subsequent ROOM_STATE without needing JOIN_ACK in M4. */
    bool                joining;
    bool                creating;

    /* M5 lifecycle bits set by recv router. */
    uint8_t             vote_timer_secs;   /* 0xFF when no timer */
    uint8_t             countdown_secs;    /* 0xFF when no countdown */
    bool                game_pick_pending; /* a GAME_PICK arrived */
    uint8_t             picked_game_id;
    uint32_t            round_seed;

    bool                game_start_pending;/* a GAME_START arrived */
    sapp_game_start_t   start;
    uint32_t            lockstep_tick;     /* tick we expect a bundle for */

    sapp_last_bundle_t  last_bundle;

    bool                round_ended;
    uint8_t             round_outcome;
    uint8_t             round_winner;

    /* M6: drop state. dropped[slot] persists across the round until
     * GAME_START (next round) clears it. last_drop_reason is 0xFF until
     * a SLOT_DROP arrives. */
    bool                dropped[SAPP_ROOM_SLOTS_MAX];
    uint8_t             last_drop_reason;
} sapp_online_ctx_t;

sapp_online_ctx_t* sapp_online_ctx(void);

/* Set status from a printf-style formatter (not variadic — keep it
 * embedded-friendly; the modules pass plain strings). */
void sapp_online_set_status(const char* msg);

/* Routed by the framework recv callback installed in CONNECTING. */
void sapp_online_handle_frame(const uint8_t* body, size_t len);

/* Send HELLO using the framework identity + local lobby seating. */
bool sapp_online_send_hello(void);

/* ---- per-state hooks ---------------------------------------------- */

void          sapp_state_connecting_enter   (void);
lobby_state_t sapp_state_connecting_input   (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_connecting_render  (lobby_scene_t* s);

void          sapp_state_lobby_list_enter   (void);
lobby_state_t sapp_state_lobby_list_input   (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_lobby_list_render  (lobby_scene_t* s);

void          sapp_state_room_create_enter  (void);
lobby_state_t sapp_state_room_create_input  (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_room_create_render (lobby_scene_t* s);

void          sapp_state_in_room_enter      (void);
lobby_state_t sapp_state_in_room_input      (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_in_room_render     (lobby_scene_t* s);

/* M5 states. */
void          sapp_state_game_select_online_enter  (void);
lobby_state_t sapp_state_game_select_online_input  (lobby_input_t now,
                                                    lobby_input_t prev);
void          sapp_state_game_select_online_render (lobby_scene_t* s);

void          sapp_state_vote_timer_enter   (void);
lobby_state_t sapp_state_vote_timer_input   (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_vote_timer_render  (lobby_scene_t* s);

void          sapp_state_countdown_enter    (void);
lobby_state_t sapp_state_countdown_input    (lobby_input_t now,
                                             lobby_input_t prev);
void          sapp_state_countdown_render   (lobby_scene_t* s);

void          sapp_state_playing_online_enter (void);
lobby_state_t sapp_state_playing_online_input (
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev[LOBBY_MAX_PLAYERS]);
void          sapp_state_playing_online_render(lobby_scene_t* s);

void          sapp_state_results_online_enter  (void);
lobby_state_t sapp_state_results_online_input  (lobby_input_t now,
                                                lobby_input_t prev);
void          sapp_state_results_online_render (lobby_scene_t* s);

/* M5: lookup the registered game by id (0..n-1) for the online states.
 * Returns NULL if out of range. */
const struct lobby_game* sapp_get_game(uint8_t game_id);
uint8_t                  sapp_count_games(void);

#ifdef __cplusplus
}
#endif
#endif
