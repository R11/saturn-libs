/*
 * libs/saturn-app/net/sapp_proto — encode/decode for M4 protocol messages.
 *
 * Frame on the wire is [LEN_HI][LEN_LO][TYPE][PAYLOAD], big-endian, where
 * LEN counts TYPE+PAYLOAD bytes. The encode helpers in this module write
 * just the [TYPE][PAYLOAD] body — the framing prefix is added by the
 * net layer (sapp_net_send_frame).
 *
 * Decode helpers consume an entire received frame body (TYPE + payload)
 * and never raise; they return false on any size/format mismatch and
 * the caller drops the frame.
 *
 * Atomic identity (per locked decision #12):
 *   HELLO carries [ver:1][caps:4][session_uuid:16][n_local:1][n × 11
 *   ASCII names, NUL-padded].
 */

#ifndef SATURN_APP_NET_PROTO_H
#define SATURN_APP_NET_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <saturn_app/identity.h>
#include <saturn_app/online.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type IDs (from design/05-protocol.md). */
enum {
    SAPP_MSG_HELLO          = 0x01,
    SAPP_MSG_HELLO_ACK      = 0x02,
    SAPP_MSG_LOBBY_LIST_REQ = 0x10,
    SAPP_MSG_LOBBY_LIST     = 0x11,
    SAPP_MSG_ROOM_CREATE    = 0x12,
    SAPP_MSG_ROOM_JOIN      = 0x13,
    SAPP_MSG_ROOM_LEAVE     = 0x14,
    SAPP_MSG_ROOM_STATE     = 0x15,
    /* M5: ready/vote/timer/countdown. */
    SAPP_MSG_READY          = 0x16, /* C2S: [ready:1][game_id:1] */
    SAPP_MSG_VOTE_TIMER     = 0x17, /* S2C: [seconds_remaining:1] */
    SAPP_MSG_GAME_PICK      = 0x18, /* S2C: [game_id:1][seed:4] */
    SAPP_MSG_COUNTDOWN      = 0x19, /* S2C: [seconds_remaining:1] */
    SAPP_MSG_GAME_START     = 0x1A, /* S2C: [game_id:1][seed:4][start_tick:4][n_slots:1] */
    SAPP_MSG_INPUT          = 0x1B, /* C2S: [tick:4][slot:1][input:2] */
    SAPP_MSG_BUNDLE         = 0x1C, /* S2C: [tick:4][n:1] N×[input:2] */
    SAPP_MSG_ROUND_END      = 0x1D, /* both ways: [outcome:1][winner:1] */
    /* M6: drop policy. */
    SAPP_MSG_SLOT_DROP      = 0x32, /* S2C: [slot:1][reason:1] */
    SAPP_MSG_ERROR          = 0xF0
};

/* Visibility values for ROOM_CREATE. */
enum { SAPP_VIS_PUBLIC = 0, SAPP_VIS_PRIVATE = 1 };

/* ---- generic helpers --------------------------------------------------- */

void sapp_proto_put_u8 (uint8_t* buf, size_t* off, uint8_t v);
void sapp_proto_put_u16(uint8_t* buf, size_t* off, uint16_t v);
void sapp_proto_put_u32(uint8_t* buf, size_t* off, uint32_t v);
void sapp_proto_put_bytes(uint8_t* buf, size_t* off,
                          const void* src, size_t n);

bool sapp_proto_get_u8 (const uint8_t* buf, size_t len,
                        size_t* off, uint8_t* out);
bool sapp_proto_get_u16(const uint8_t* buf, size_t len,
                        size_t* off, uint16_t* out);
bool sapp_proto_get_u32(const uint8_t* buf, size_t len,
                        size_t* off, uint32_t* out);
bool sapp_proto_get_bytes(const uint8_t* buf, size_t len,
                          size_t* off, void* dst, size_t n);

/* ---- encoders ---------------------------------------------------------- */

/* Encode HELLO. local_names is an array of n_local pointers to NUL-
 * terminated names (≤10 chars each). Returns body length on success or
 * 0 on overflow. buf must be at least 256 bytes. */
size_t sapp_proto_encode_hello(uint8_t* buf, size_t cap,
                               uint8_t  ver,
                               uint32_t caps,
                               const uint8_t session_uuid[16],
                               uint8_t  n_local,
                               const char* const* local_names);

size_t sapp_proto_encode_hello_ack(uint8_t* buf, size_t cap,
                                   uint8_t  ver,
                                   uint32_t caps,
                                   const uint8_t uuid[16],
                                   uint8_t  slot_seed);

size_t sapp_proto_encode_lobby_list_req(uint8_t* buf, size_t cap,
                                        uint8_t game_filter, uint8_t page);

size_t sapp_proto_encode_room_create(uint8_t* buf, size_t cap,
                                     uint8_t game_id,
                                     uint8_t max_slots,
                                     uint8_t visibility,
                                     const char* room_name);

size_t sapp_proto_encode_room_join(uint8_t* buf, size_t cap,
                                   const uint8_t room_id[SAPP_ROOM_ID_LEN]);

size_t sapp_proto_encode_room_leave(uint8_t* buf, size_t cap);

/* ---- decoders ---------------------------------------------------------- */

typedef struct {
    uint8_t  ver;
    uint32_t caps;
    uint8_t  session_uuid[16];
    uint8_t  n_local;
    char     local_names[SAPP_ROOM_SLOTS_MAX][SAPP_NAME_CAP];
} sapp_hello_t;

bool sapp_proto_decode_hello(const uint8_t* body, size_t len,
                             sapp_hello_t* out);

typedef struct {
    uint8_t  ver;
    uint32_t caps;
    uint8_t  uuid[16];
    uint8_t  slot_seed;
} sapp_hello_ack_t;

bool sapp_proto_decode_hello_ack(const uint8_t* body, size_t len,
                                 sapp_hello_ack_t* out);

bool sapp_proto_decode_lobby_list(const uint8_t* body, size_t len,
                                  sapp_room_list_t* out);

typedef struct {
    uint8_t game_id;
    uint8_t max_slots;
    uint8_t visibility;
    char    name[SAPP_ROOM_NAME_CAP];
} sapp_room_create_t;

bool sapp_proto_decode_room_create(const uint8_t* body, size_t len,
                                   sapp_room_create_t* out);

bool sapp_proto_decode_room_join(const uint8_t* body, size_t len,
                                 uint8_t out_room_id[SAPP_ROOM_ID_LEN]);

bool sapp_proto_decode_room_state(const uint8_t* body, size_t len,
                                  sapp_room_state_t* out);

/* ---- M5 encoders/decoders --------------------------------------------- */

size_t sapp_proto_encode_ready(uint8_t* buf, size_t cap,
                               uint8_t ready, uint8_t game_id);

typedef struct { uint8_t ready; uint8_t game_id; } sapp_ready_t;
bool sapp_proto_decode_ready(const uint8_t* body, size_t len,
                             sapp_ready_t* out);

size_t sapp_proto_encode_vote_timer(uint8_t* buf, size_t cap, uint8_t secs);
bool   sapp_proto_decode_vote_timer(const uint8_t* body, size_t len,
                                    uint8_t* out_secs);

size_t sapp_proto_encode_game_pick(uint8_t* buf, size_t cap,
                                   uint8_t game_id, uint32_t seed);
typedef struct { uint8_t game_id; uint32_t seed; } sapp_game_pick_t;
bool sapp_proto_decode_game_pick(const uint8_t* body, size_t len,
                                 sapp_game_pick_t* out);

size_t sapp_proto_encode_countdown(uint8_t* buf, size_t cap, uint8_t secs);
bool   sapp_proto_decode_countdown(const uint8_t* body, size_t len,
                                   uint8_t* out_secs);

size_t sapp_proto_encode_game_start(uint8_t* buf, size_t cap,
                                    uint8_t  game_id,
                                    uint32_t seed,
                                    uint32_t start_tick,
                                    uint8_t  n_slots);
typedef struct {
    uint8_t  game_id;
    uint32_t seed;
    uint32_t start_tick;
    uint8_t  n_slots;
} sapp_game_start_t;
bool sapp_proto_decode_game_start(const uint8_t* body, size_t len,
                                  sapp_game_start_t* out);

size_t sapp_proto_encode_input(uint8_t* buf, size_t cap,
                               uint32_t tick, uint8_t slot, uint16_t input);
typedef struct { uint32_t tick; uint8_t slot; uint16_t input; } sapp_input_msg_t;
bool sapp_proto_decode_input(const uint8_t* body, size_t len,
                             sapp_input_msg_t* out);

size_t sapp_proto_encode_bundle(uint8_t* buf, size_t cap,
                                uint32_t tick, uint8_t n,
                                const uint16_t* inputs);
typedef struct {
    uint32_t tick;
    uint8_t  n;
    uint16_t inputs[SAPP_ROOM_SLOTS_MAX];
} sapp_bundle_t;
bool sapp_proto_decode_bundle(const uint8_t* body, size_t len,
                              sapp_bundle_t* out);

size_t sapp_proto_encode_round_end(uint8_t* buf, size_t cap,
                                   uint8_t outcome, uint8_t winner);
typedef struct { uint8_t outcome; uint8_t winner; } sapp_round_end_t;
bool sapp_proto_decode_round_end(const uint8_t* body, size_t len,
                                 sapp_round_end_t* out);

/* M6: SLOT_DROP. Server-emitted only; cart never sends. */
enum {
    SAPP_DROP_REASON_TIMEOUT       = 0,
    SAPP_DROP_REASON_DISCONNECT    = 1,
};
size_t sapp_proto_encode_slot_drop(uint8_t* buf, size_t cap,
                                   uint8_t slot, uint8_t reason);
typedef struct { uint8_t slot; uint8_t reason; } sapp_slot_drop_t;
bool sapp_proto_decode_slot_drop(const uint8_t* body, size_t len,
                                 sapp_slot_drop_t* out);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_APP_NET_PROTO_H */
