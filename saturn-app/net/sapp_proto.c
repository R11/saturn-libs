/*
 * libs/saturn-app/net/sapp_proto.c — encode/decode helpers.
 *
 * All multi-byte integers are big-endian (network order). Field layouts
 * match design/05-protocol.md plus the M4 deltas locked in the milestone
 * plan (extended HELLO with [n_local][names×11]).
 */

#include "sapp_proto.h"

#include <string.h>

/* ---- byte put/get ------------------------------------------------------ */

void sapp_proto_put_u8(uint8_t* buf, size_t* off, uint8_t v) {
    buf[*off] = v;
    *off += 1;
}

void sapp_proto_put_u16(uint8_t* buf, size_t* off, uint16_t v) {
    buf[*off + 0] = (uint8_t)(v >> 8);
    buf[*off + 1] = (uint8_t)(v & 0xFF);
    *off += 2;
}

void sapp_proto_put_u32(uint8_t* buf, size_t* off, uint32_t v) {
    buf[*off + 0] = (uint8_t)((v >> 24) & 0xFF);
    buf[*off + 1] = (uint8_t)((v >> 16) & 0xFF);
    buf[*off + 2] = (uint8_t)((v >>  8) & 0xFF);
    buf[*off + 3] = (uint8_t)((v      ) & 0xFF);
    *off += 4;
}

void sapp_proto_put_bytes(uint8_t* buf, size_t* off,
                          const void* src, size_t n) {
    if (n) memcpy(buf + *off, src, n);
    *off += n;
}

bool sapp_proto_get_u8(const uint8_t* buf, size_t len,
                       size_t* off, uint8_t* out) {
    if (*off + 1 > len) return false;
    *out = buf[*off];
    *off += 1;
    return true;
}

bool sapp_proto_get_u16(const uint8_t* buf, size_t len,
                        size_t* off, uint16_t* out) {
    if (*off + 2 > len) return false;
    *out = (uint16_t)((buf[*off] << 8) | buf[*off + 1]);
    *off += 2;
    return true;
}

bool sapp_proto_get_u32(const uint8_t* buf, size_t len,
                        size_t* off, uint32_t* out) {
    if (*off + 4 > len) return false;
    *out = ((uint32_t)buf[*off] << 24)
         | ((uint32_t)buf[*off + 1] << 16)
         | ((uint32_t)buf[*off + 2] <<  8)
         | ((uint32_t)buf[*off + 3]);
    *off += 4;
    return true;
}

bool sapp_proto_get_bytes(const uint8_t* buf, size_t len,
                          size_t* off, void* dst, size_t n) {
    if (*off + n > len) return false;
    if (n) memcpy(dst, buf + *off, n);
    *off += n;
    return true;
}

/* ---- helpers ----------------------------------------------------------- */

static void copy_name_padded(uint8_t* dst, const char* src, size_t cap) {
    /* Copy up to cap-1 chars, NUL-terminate, NUL-pad to cap. */
    size_t i = 0;
    if (src) {
        for (; i + 1 < cap && src[i] != '\0'; ++i) dst[i] = (uint8_t)src[i];
    }
    for (; i < cap; ++i) dst[i] = 0;
}

static void copy_name_in(char* dst, const uint8_t* src, size_t cap) {
    size_t i = 0;
    for (; i + 1 < cap; ++i) {
        dst[i] = (char)src[i];
        if (src[i] == 0) break;
    }
    dst[cap - 1] = '\0';
}

/* ---- encoders ---------------------------------------------------------- */

size_t sapp_proto_encode_hello(uint8_t* buf, size_t cap,
                               uint8_t  ver,
                               uint32_t caps,
                               const uint8_t session_uuid[16],
                               uint8_t  n_local,
                               const char* const* local_names)
{
    size_t off = 0;
    size_t needed = 1 + 1 + 4 + 16 + 1 + (size_t)n_local * SAPP_NAME_CAP;
    if (cap < needed) return 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_HELLO);
    sapp_proto_put_u8 (buf, &off, ver);
    sapp_proto_put_u32(buf, &off, caps);
    sapp_proto_put_bytes(buf, &off, session_uuid, 16);
    sapp_proto_put_u8 (buf, &off, n_local);
    for (uint8_t i = 0; i < n_local; ++i) {
        uint8_t name_buf[SAPP_NAME_CAP];
        copy_name_padded(name_buf,
                         local_names ? local_names[i] : NULL,
                         SAPP_NAME_CAP);
        sapp_proto_put_bytes(buf, &off, name_buf, SAPP_NAME_CAP);
    }
    return off;
}

size_t sapp_proto_encode_hello_ack(uint8_t* buf, size_t cap,
                                   uint8_t  ver,
                                   uint32_t caps,
                                   const uint8_t uuid[16],
                                   uint8_t  slot_seed)
{
    size_t off = 0;
    if (cap < 1 + 1 + 4 + 16 + 1) return 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_HELLO_ACK);
    sapp_proto_put_u8 (buf, &off, ver);
    sapp_proto_put_u32(buf, &off, caps);
    sapp_proto_put_bytes(buf, &off, uuid, 16);
    sapp_proto_put_u8 (buf, &off, slot_seed);
    return off;
}

size_t sapp_proto_encode_lobby_list_req(uint8_t* buf, size_t cap,
                                        uint8_t game_filter, uint8_t page)
{
    size_t off = 0;
    if (cap < 3) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_LOBBY_LIST_REQ);
    sapp_proto_put_u8(buf, &off, game_filter);
    sapp_proto_put_u8(buf, &off, page);
    return off;
}

size_t sapp_proto_encode_room_create(uint8_t* buf, size_t cap,
                                     uint8_t game_id,
                                     uint8_t max_slots,
                                     uint8_t visibility,
                                     const char* room_name)
{
    size_t off = 0;
    size_t name_len = 0;
    if (room_name) {
        while (room_name[name_len] && name_len < SAPP_ROOM_NAME_CAP - 1)
            name_len++;
    }
    if (cap < 1 + 1 + 1 + 1 + 1 + name_len) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_ROOM_CREATE);
    sapp_proto_put_u8(buf, &off, game_id);
    sapp_proto_put_u8(buf, &off, max_slots);
    sapp_proto_put_u8(buf, &off, visibility);
    sapp_proto_put_u8(buf, &off, (uint8_t)name_len);
    sapp_proto_put_bytes(buf, &off, room_name, name_len);
    return off;
}

size_t sapp_proto_encode_room_join(uint8_t* buf, size_t cap,
                                   const uint8_t room_id[SAPP_ROOM_ID_LEN])
{
    size_t off = 0;
    if (cap < 1 + SAPP_ROOM_ID_LEN) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_ROOM_JOIN);
    sapp_proto_put_bytes(buf, &off, room_id, SAPP_ROOM_ID_LEN);
    return off;
}

size_t sapp_proto_encode_room_leave(uint8_t* buf, size_t cap)
{
    size_t off = 0;
    if (cap < 1) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_ROOM_LEAVE);
    return off;
}

/* ---- decoders ---------------------------------------------------------- */

bool sapp_proto_decode_hello(const uint8_t* body, size_t len,
                             sapp_hello_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_HELLO) return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->ver))  return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->caps)) return false;
    if (!sapp_proto_get_bytes(body, len, &off, out->session_uuid, 16))
        return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->n_local)) return false;
    if (out->n_local > SAPP_ROOM_SLOTS_MAX) return false;
    for (uint8_t i = 0; i < out->n_local; ++i) {
        uint8_t name_buf[SAPP_NAME_CAP];
        if (!sapp_proto_get_bytes(body, len, &off, name_buf, SAPP_NAME_CAP))
            return false;
        copy_name_in(out->local_names[i], name_buf, SAPP_NAME_CAP);
    }
    return true;
}

bool sapp_proto_decode_hello_ack(const uint8_t* body, size_t len,
                                 sapp_hello_ack_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_HELLO_ACK) return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->ver))  return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->caps)) return false;
    if (!sapp_proto_get_bytes(body, len, &off, out->uuid, 16)) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->slot_seed)) return false;
    return true;
}

bool sapp_proto_decode_lobby_list(const uint8_t* body, size_t len,
                                  sapp_room_list_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_LOBBY_LIST) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->page))  return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->total)) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->n))     return false;
    if (out->n > SAPP_MAX_ROOMS) return false;
    for (uint8_t i = 0; i < out->n; ++i) {
        sapp_room_summary_t* r = &out->rooms[i];
        uint8_t name_len;
        uint8_t name_buf[SAPP_ROOM_NAME_CAP];
        if (!sapp_proto_get_bytes(body, len, &off, r->room_id,
                                  SAPP_ROOM_ID_LEN)) return false;
        if (!sapp_proto_get_u8(body, len, &off, &r->game_id)) return false;
        if (!sapp_proto_get_u8(body, len, &off, &r->occ))     return false;
        if (!sapp_proto_get_u8(body, len, &off, &r->max))     return false;
        if (!sapp_proto_get_u8(body, len, &off, &name_len))   return false;
        if (name_len >= SAPP_ROOM_NAME_CAP) name_len = SAPP_ROOM_NAME_CAP - 1;
        memset(name_buf, 0, sizeof(name_buf));
        if (!sapp_proto_get_bytes(body, len, &off, name_buf, name_len))
            return false;
        copy_name_in(r->name, name_buf, SAPP_ROOM_NAME_CAP);
    }
    return true;
}

bool sapp_proto_decode_room_create(const uint8_t* body, size_t len,
                                   sapp_room_create_t* out)
{
    size_t off = 0;
    uint8_t type;
    uint8_t name_len;
    uint8_t name_buf[SAPP_ROOM_NAME_CAP];
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_ROOM_CREATE) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->game_id))    return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->max_slots))  return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->visibility)) return false;
    if (!sapp_proto_get_u8(body, len, &off, &name_len))        return false;
    if (name_len >= SAPP_ROOM_NAME_CAP) return false;
    memset(name_buf, 0, sizeof(name_buf));
    if (!sapp_proto_get_bytes(body, len, &off, name_buf, name_len))
        return false;
    copy_name_in(out->name, name_buf, SAPP_ROOM_NAME_CAP);
    return true;
}

bool sapp_proto_decode_room_join(const uint8_t* body, size_t len,
                                 uint8_t out_room_id[SAPP_ROOM_ID_LEN])
{
    size_t off = 0;
    uint8_t type;
    if (!out_room_id) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_ROOM_JOIN) return false;
    if (!sapp_proto_get_bytes(body, len, &off, out_room_id,
                              SAPP_ROOM_ID_LEN)) return false;
    return true;
}

bool sapp_proto_decode_room_state(const uint8_t* body, size_t len,
                                  sapp_room_state_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_ROOM_STATE) return false;
    if (!sapp_proto_get_bytes(body, len, &off, out->room_id,
                              SAPP_ROOM_ID_LEN)) return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->game_id))    return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->mode))       return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->host_slot))  return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->max_slots))  return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->n_members))  return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->tick))       return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->room_flags)) return false;
    if (out->n_members > SAPP_ROOM_SLOTS_MAX) return false;
    for (uint8_t i = 0; i < out->n_members; ++i) {
        sapp_room_member_t* m = &out->members[i];
        uint8_t name_len;
        uint8_t name_buf[SAPP_NAME_CAP];
        if (!sapp_proto_get_u8(body, len, &off, &m->slot)) return false;
        if (!sapp_proto_get_bytes(body, len, &off, m->uuid, 16)) return false;
        if (!sapp_proto_get_u8(body, len, &off, &m->flags)) return false;
        if (!sapp_proto_get_u8(body, len, &off, &m->cart_id)) return false;
        if (!sapp_proto_get_u8(body, len, &off, &name_len)) return false;
        if (name_len >= SAPP_NAME_CAP) name_len = SAPP_NAME_CAP - 1;
        memset(name_buf, 0, sizeof(name_buf));
        if (!sapp_proto_get_bytes(body, len, &off, name_buf, name_len))
            return false;
        copy_name_in(m->name, name_buf, SAPP_NAME_CAP);
    }
    out->valid = true;
    return true;
}

/* ---- M5: ready/vote/timer/countdown/start/input/bundle/round_end ------ */

size_t sapp_proto_encode_ready(uint8_t* buf, size_t cap,
                               uint8_t ready, uint8_t game_id)
{
    size_t off = 0;
    if (cap < 3) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_READY);
    sapp_proto_put_u8(buf, &off, ready ? 1u : 0u);
    sapp_proto_put_u8(buf, &off, game_id);
    return off;
}

bool sapp_proto_decode_ready(const uint8_t* body, size_t len, sapp_ready_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_READY) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->ready))   return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->game_id)) return false;
    return true;
}

size_t sapp_proto_encode_vote_timer(uint8_t* buf, size_t cap, uint8_t secs)
{
    size_t off = 0;
    if (cap < 2) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_VOTE_TIMER);
    sapp_proto_put_u8(buf, &off, secs);
    return off;
}

bool sapp_proto_decode_vote_timer(const uint8_t* body, size_t len, uint8_t* out_secs)
{
    size_t off = 0;
    uint8_t type;
    if (!out_secs) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_VOTE_TIMER) return false;
    return sapp_proto_get_u8(body, len, &off, out_secs);
}

size_t sapp_proto_encode_game_pick(uint8_t* buf, size_t cap,
                                   uint8_t game_id, uint32_t seed)
{
    size_t off = 0;
    if (cap < 6) return 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_GAME_PICK);
    sapp_proto_put_u8 (buf, &off, game_id);
    sapp_proto_put_u32(buf, &off, seed);
    return off;
}

bool sapp_proto_decode_game_pick(const uint8_t* body, size_t len,
                                 sapp_game_pick_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_GAME_PICK) return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->game_id)) return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->seed))    return false;
    return true;
}

size_t sapp_proto_encode_countdown(uint8_t* buf, size_t cap, uint8_t secs)
{
    size_t off = 0;
    if (cap < 2) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_COUNTDOWN);
    sapp_proto_put_u8(buf, &off, secs);
    return off;
}

bool sapp_proto_decode_countdown(const uint8_t* body, size_t len, uint8_t* out_secs)
{
    size_t off = 0;
    uint8_t type;
    if (!out_secs) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_COUNTDOWN) return false;
    return sapp_proto_get_u8(body, len, &off, out_secs);
}

size_t sapp_proto_encode_game_start(uint8_t* buf, size_t cap,
                                    uint8_t  game_id,
                                    uint32_t seed,
                                    uint32_t start_tick,
                                    uint8_t  n_slots)
{
    size_t off = 0;
    if (cap < 1 + 1 + 4 + 4 + 1) return 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_GAME_START);
    sapp_proto_put_u8 (buf, &off, game_id);
    sapp_proto_put_u32(buf, &off, seed);
    sapp_proto_put_u32(buf, &off, start_tick);
    sapp_proto_put_u8 (buf, &off, n_slots);
    return off;
}

bool sapp_proto_decode_game_start(const uint8_t* body, size_t len,
                                  sapp_game_start_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_GAME_START) return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->game_id))    return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->seed))       return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->start_tick)) return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->n_slots))    return false;
    return true;
}

size_t sapp_proto_encode_input(uint8_t* buf, size_t cap,
                               uint32_t tick, uint8_t slot, uint16_t input)
{
    size_t off = 0;
    if (cap < 1 + 4 + 1 + 2) return 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_INPUT);
    sapp_proto_put_u32(buf, &off, tick);
    sapp_proto_put_u8 (buf, &off, slot);
    sapp_proto_put_u16(buf, &off, input);
    return off;
}

bool sapp_proto_decode_input(const uint8_t* body, size_t len,
                             sapp_input_msg_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_INPUT) return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->tick))  return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->slot))  return false;
    if (!sapp_proto_get_u16(body, len, &off, &out->input)) return false;
    return true;
}

size_t sapp_proto_encode_bundle(uint8_t* buf, size_t cap,
                                uint32_t tick, uint8_t n,
                                const uint16_t* inputs)
{
    size_t off = 0;
    size_t needed = 1 + 4 + 1 + (size_t)n * 2;
    if (cap < needed) return 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_BUNDLE);
    sapp_proto_put_u32(buf, &off, tick);
    sapp_proto_put_u8 (buf, &off, n);
    for (uint8_t i = 0; i < n; ++i) {
        sapp_proto_put_u16(buf, &off, inputs ? inputs[i] : 0);
    }
    return off;
}

bool sapp_proto_decode_bundle(const uint8_t* body, size_t len,
                              sapp_bundle_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_BUNDLE) return false;
    if (!sapp_proto_get_u32(body, len, &off, &out->tick)) return false;
    if (!sapp_proto_get_u8 (body, len, &off, &out->n))    return false;
    if (out->n > SAPP_ROOM_SLOTS_MAX) return false;
    for (uint8_t i = 0; i < out->n; ++i) {
        if (!sapp_proto_get_u16(body, len, &off, &out->inputs[i])) return false;
    }
    return true;
}

size_t sapp_proto_encode_round_end(uint8_t* buf, size_t cap,
                                   uint8_t outcome, uint8_t winner)
{
    size_t off = 0;
    if (cap < 3) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_ROUND_END);
    sapp_proto_put_u8(buf, &off, outcome);
    sapp_proto_put_u8(buf, &off, winner);
    return off;
}

bool sapp_proto_decode_round_end(const uint8_t* body, size_t len,
                                 sapp_round_end_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_ROUND_END) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->outcome)) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->winner))  return false;
    return true;
}

/* ---- M6: SLOT_DROP ---------------------------------------------------- */

size_t sapp_proto_encode_slot_drop(uint8_t* buf, size_t cap,
                                   uint8_t slot, uint8_t reason)
{
    size_t off = 0;
    if (cap < 3) return 0;
    sapp_proto_put_u8(buf, &off, SAPP_MSG_SLOT_DROP);
    sapp_proto_put_u8(buf, &off, slot);
    sapp_proto_put_u8(buf, &off, reason);
    return off;
}

bool sapp_proto_decode_slot_drop(const uint8_t* body, size_t len,
                                 sapp_slot_drop_t* out)
{
    size_t off = 0;
    uint8_t type;
    if (!out) return false;
    if (!sapp_proto_get_u8(body, len, &off, &type)) return false;
    if (type != SAPP_MSG_SLOT_DROP) return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->slot))   return false;
    if (!sapp_proto_get_u8(body, len, &off, &out->reason)) return false;
    return true;
}
