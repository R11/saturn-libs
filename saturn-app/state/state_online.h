/*
 * state/state_online.h — internal coordination for the online side.
 *
 * Not installed in include/. The online layer owns a single context
 * (room list, room state, status string, vote/timer/countdown bits)
 * and a recv callback that routes incoming frames by message type.
 *
 * After the M-redesign the unified lobby state directly drives the
 * online context — there are no per-state hooks for CONNECTING /
 * LOBBY_LIST / ROOM_CREATE / IN_ROOM / GAME_SELECT_ONLINE /
 * VOTE_TIMER / COUNTDOWN any more.
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

/* Shared online context. */
typedef struct {
    bool                handshaken;     /* HELLO_ACK received this session */
    sapp_room_list_t    list;
    sapp_room_state_t   room;
    char                status[64];
    char                room_create_buf[SAPP_ROOM_NAME_CAP];
    /* Pending JOIN/CREATE: recorded so recv can transition properly
     * on the subsequent ROOM_STATE. */
    bool                joining;
    bool                creating;

    /* Lifecycle bits set by recv router. */
    uint8_t             vote_timer_secs;   /* 0xFF when no timer */
    uint8_t             countdown_secs;    /* 0xFF when no countdown */
    bool                game_pick_pending;
    uint8_t             picked_game_id;
    uint32_t            round_seed;

    bool                game_start_pending;
    sapp_game_start_t   start;
    uint32_t            lockstep_tick;

    sapp_last_bundle_t  last_bundle;

    bool                round_ended;
    uint8_t             round_outcome;
    uint8_t             round_winner;

    bool                dropped[SAPP_ROOM_SLOTS_MAX];
    uint8_t             last_drop_reason;
} sapp_online_ctx_t;

sapp_online_ctx_t* sapp_online_ctx(void);

/* Reset all fields back to their fresh-session defaults. */
void sapp_online_ctx_reset(void);

/* Set status from a plain string (truncated). */
void sapp_online_set_status(const char* msg);

/* Routed by the framework recv callback. */
void sapp_online_handle_frame(const uint8_t* body, size_t len);

/* Send HELLO using the framework identity + local lobby seating. */
bool sapp_online_send_hello(void);

/* Lookup the registered game by id. NULL if out of range. */
const struct lobby_game* sapp_get_game(uint8_t game_id);
uint8_t                  sapp_count_games(void);

/* Standard recv router that the unified lobby installs once the user
 * starts a connect attempt. */
void sapp_online_install_recv(void);

#ifdef __cplusplus
}
#endif
#endif
