/*
 * libs/saturn-app/online — public surface for the online states (M4).
 *
 * The framework owns one online context: a network backend (vtable),
 * a connection sub-state (CONNECTING -> LOBBY_LIST -> ROOM_CREATE ->
 * IN_ROOM), the most recent ROOM_LIST snapshot, and the most recent
 * ROOM_STATE snapshot. The host shell installs a TCP backend; the
 * Saturn shell will install a NetLink backend in M7.
 *
 * Tests can install a synthetic backend (sapp_net_install) that captures
 * outbound frames and injects inbound frames programmatically, so the
 * online states are exercisable without TCP.
 */

#ifndef SATURN_APP_ONLINE_H
#define SATURN_APP_ONLINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <saturn_app/identity.h>     /* SAPP_NAME_CAP */
#include <saturn_app/local_lobby.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- protocol-side caps ------------------------------------------------ */

#define SAPP_PROTO_VER       0x01u
#define SAPP_ROOM_ID_LEN     6u
#define SAPP_ROOM_NAME_CAP  16u
#define SAPP_MAX_ROOMS      16u
#define SAPP_ROOM_SLOTS_MAX  8u

/* ---- ROOM_LIST cache --------------------------------------------------- */

typedef struct {
    uint8_t  room_id[SAPP_ROOM_ID_LEN];
    uint8_t  game_id;
    uint8_t  occ;
    uint8_t  max;
    char     name[SAPP_ROOM_NAME_CAP];
} sapp_room_summary_t;

typedef struct {
    uint8_t  page;
    uint8_t  total;
    uint8_t  n;
    sapp_room_summary_t rooms[SAPP_MAX_ROOMS];
} sapp_room_list_t;

/* ---- ROOM_STATE cache -------------------------------------------------- */

typedef struct {
    uint8_t  slot;
    uint8_t  uuid[16];
    uint8_t  flags;
    char     name[SAPP_NAME_CAP];
    uint8_t  cart_id;          /* 0..n; matches the cart that supplied this name */
} sapp_room_member_t;

typedef struct {
    bool     valid;
    uint8_t  room_id[SAPP_ROOM_ID_LEN];
    uint8_t  game_id;
    uint8_t  mode;
    uint8_t  host_slot;
    uint8_t  max_slots;
    uint8_t  n_members;
    uint32_t tick;
    uint8_t  room_flags;
    sapp_room_member_t members[SAPP_ROOM_SLOTS_MAX];
} sapp_room_state_t;

/* ---- accessors -------------------------------------------------------- */

const sapp_room_list_t*  sapp_online_room_list (void);
const sapp_room_state_t* sapp_online_room_state(void);

/* Status string for the HUD. NULL means "no status". */
const char* sapp_online_status(void);

/* True if a HELLO_ACK has been received this session. Tests use this to
 * verify the handshake completed. */
bool sapp_online_handshaken(void);

/* The room-name buffer being typed in ROOM_CREATE. NULL if not in
 * that state. Used by tests. */
const char* sapp_online_room_create_buffer(void);

/* M5 — exposed for tests/HUD. */

/* Per-slot vote/ready snapshot from the most recent ROOM_STATE update
 * (server stamps these into ROOM_STATE flags in M5). */
typedef struct {
    uint8_t  game_id;        /* slot's chosen game id */
    uint8_t  ready;          /* 0/1 */
} sapp_slot_vote_t;

/* Most recently observed VOTE_TIMER seconds_remaining; 0xFF when no
 * timer is active. */
uint8_t sapp_online_vote_timer_secs(void);

/* Most recently observed COUNTDOWN seconds_remaining; 0xFF when none. */
uint8_t sapp_online_countdown_secs(void);

/* The selected game id (after GAME_PICK). 0xFF before a pick. */
uint8_t sapp_online_picked_game_id(void);

/* Per-round seed (after GAME_PICK / GAME_START). */
uint32_t sapp_online_round_seed(void);

/* Authoritative tick we are currently waiting for a BUNDLE on. */
uint32_t sapp_online_lockstep_tick(void);

/* Most recently observed bundle (read-only; updated by recv router). */
typedef struct {
    bool     valid;
    uint32_t tick;
    uint8_t  n;
    uint16_t inputs[SAPP_ROOM_SLOTS_MAX];
} sapp_last_bundle_t;
const sapp_last_bundle_t* sapp_online_last_bundle(void);

/* Round-end signal (set by recv router on ROUND_END). */
bool    sapp_online_round_ended(void);
uint8_t sapp_online_round_outcome(void);
uint8_t sapp_online_round_winner(void);

/* M6: per-slot drop state. true once the server has broadcast SLOT_DROP
 * for `slot`. Persists for the remainder of the round. Slot indices are
 * 0..SAPP_ROOM_SLOTS_MAX-1. */
bool    sapp_online_slot_is_dropped(uint8_t slot);

/* Reason for the most recent SLOT_DROP (or 0xFF if none seen). Tests
 * use this to assert the server marked a timeout vs. disconnect. */
uint8_t sapp_online_last_drop_reason(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_APP_ONLINE_H */
