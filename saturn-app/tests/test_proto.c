/*
 * tests/test_proto.c — round-trip tests for the M4 wire protocol.
 *
 * Covers the message types CONNECTING / LOBBY_LIST / ROOM_CREATE / JOIN /
 * LEAVE / ROOM_STATE need to function. Encode then decode and check
 * equality of all material fields.
 */

#include <saturn_test/test.h>
#include "../net/sapp_proto.h"

#include <string.h>

SATURN_TEST(proto_hello_roundtrip) {
    uint8_t  uuid[16];
    for (int i = 0; i < 16; ++i) uuid[i] = (uint8_t)(0xA0 + i);
    const char* names[2] = { "ALICE", "BOB" };
    uint8_t buf[256];
    size_t n = sapp_proto_encode_hello(buf, sizeof(buf),
                                       SAPP_PROTO_VER, 0x00000003u,
                                       uuid, 2, names);
    SATURN_ASSERT_GT(n, 0);

    sapp_hello_t out;
    SATURN_ASSERT(sapp_proto_decode_hello(buf, n, &out));
    SATURN_ASSERT_EQ(out.ver,     SAPP_PROTO_VER);
    SATURN_ASSERT_EQ((long)out.caps,    (long)0x00000003u);
    SATURN_ASSERT_MEM_EQ(out.session_uuid, uuid, 16);
    SATURN_ASSERT_EQ(out.n_local, 2);
    SATURN_ASSERT_STR_EQ(out.local_names[0], "ALICE");
    SATURN_ASSERT_STR_EQ(out.local_names[1], "BOB");
}

SATURN_TEST(proto_hello_ack_roundtrip) {
    uint8_t uuid[16];
    for (int i = 0; i < 16; ++i) uuid[i] = (uint8_t)i;
    uint8_t buf[64];
    size_t n = sapp_proto_encode_hello_ack(buf, sizeof(buf),
                                           SAPP_PROTO_VER, 0x42u,
                                           uuid, 7);
    SATURN_ASSERT_GT(n, 0);
    sapp_hello_ack_t out;
    SATURN_ASSERT(sapp_proto_decode_hello_ack(buf, n, &out));
    SATURN_ASSERT_EQ(out.ver,       SAPP_PROTO_VER);
    SATURN_ASSERT_EQ((long)out.caps, 0x42L);
    SATURN_ASSERT_EQ(out.slot_seed, 7);
    SATURN_ASSERT_MEM_EQ(out.uuid, uuid, 16);
}

SATURN_TEST(proto_lobby_list_req_roundtrip) {
    uint8_t buf[8];
    size_t n = sapp_proto_encode_lobby_list_req(buf, sizeof(buf), 0xFF, 1);
    SATURN_ASSERT_EQ((long)n, 3L);
    SATURN_ASSERT_EQ(buf[0], SAPP_MSG_LOBBY_LIST_REQ);
    SATURN_ASSERT_EQ(buf[1], 0xFF);
    SATURN_ASSERT_EQ(buf[2], 1);
}

SATURN_TEST(proto_room_create_roundtrip) {
    uint8_t buf[64];
    size_t n = sapp_proto_encode_room_create(buf, sizeof(buf),
                                             3, 8, SAPP_VIS_PUBLIC,
                                             "BIG ROOM");
    SATURN_ASSERT_GT(n, 0);
    sapp_room_create_t rc;
    SATURN_ASSERT(sapp_proto_decode_room_create(buf, n, &rc));
    SATURN_ASSERT_EQ(rc.game_id,    3);
    SATURN_ASSERT_EQ(rc.max_slots,  8);
    SATURN_ASSERT_EQ(rc.visibility, SAPP_VIS_PUBLIC);
    SATURN_ASSERT_STR_EQ(rc.name, "BIG ROOM");
}

SATURN_TEST(proto_room_join_roundtrip) {
    uint8_t rid_in[6]  = { 'A','B','C','D','E','F' };
    uint8_t rid_out[6];
    uint8_t buf[16];
    size_t n = sapp_proto_encode_room_join(buf, sizeof(buf), rid_in);
    SATURN_ASSERT_EQ((long)n, 7L);
    SATURN_ASSERT(sapp_proto_decode_room_join(buf, n, rid_out));
    SATURN_ASSERT_MEM_EQ(rid_in, rid_out, 6);
}

SATURN_TEST(proto_room_leave_roundtrip) {
    uint8_t buf[2];
    size_t n = sapp_proto_encode_room_leave(buf, sizeof(buf));
    SATURN_ASSERT_EQ((long)n, 1L);
    SATURN_ASSERT_EQ(buf[0], SAPP_MSG_ROOM_LEAVE);
}

SATURN_TEST(proto_room_state_roundtrip) {
    /* Build a fake ROOM_STATE by hand so we don't depend on a server-
     * side encoder. */
    uint8_t buf[256];
    size_t  off = 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_ROOM_STATE);
    sapp_proto_put_bytes(buf, &off, "QQQQQQ", 6);
    sapp_proto_put_u8 (buf, &off, 0);    /* game_id */
    sapp_proto_put_u8 (buf, &off, 0);    /* mode */
    sapp_proto_put_u8 (buf, &off, 0);    /* host_slot */
    sapp_proto_put_u8 (buf, &off, 8);    /* max_slots */
    sapp_proto_put_u8 (buf, &off, 2);    /* n_members */
    sapp_proto_put_u32(buf, &off, 0);    /* tick */
    sapp_proto_put_u8 (buf, &off, 0);    /* room_flags */
    /* Member 0: ALICE on cart 0 */
    sapp_proto_put_u8 (buf, &off, 0);
    {   uint8_t u[16] = {0}; sapp_proto_put_bytes(buf, &off, u, 16); }
    sapp_proto_put_u8 (buf, &off, 0);    /* flags */
    sapp_proto_put_u8 (buf, &off, 0);    /* cart_id */
    sapp_proto_put_u8 (buf, &off, 5);
    sapp_proto_put_bytes(buf, &off, "ALICE", 5);
    /* Member 1: BOB on cart 1 */
    sapp_proto_put_u8 (buf, &off, 1);
    {   uint8_t u[16] = {0}; sapp_proto_put_bytes(buf, &off, u, 16); }
    sapp_proto_put_u8 (buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 1);
    sapp_proto_put_u8 (buf, &off, 3);
    sapp_proto_put_bytes(buf, &off, "BOB", 3);

    sapp_room_state_t rs;
    SATURN_ASSERT(sapp_proto_decode_room_state(buf, off, &rs));
    SATURN_ASSERT(rs.valid);
    SATURN_ASSERT_EQ(rs.n_members, 2);
    SATURN_ASSERT_STR_EQ(rs.members[0].name, "ALICE");
    SATURN_ASSERT_STR_EQ(rs.members[1].name, "BOB");
    SATURN_ASSERT_EQ(rs.members[0].cart_id, 0);
    SATURN_ASSERT_EQ(rs.members[1].cart_id, 1);
}

SATURN_TEST(proto_short_buffers_rejected) {
    sapp_hello_ack_t ack;
    uint8_t bogus[3] = { SAPP_MSG_HELLO_ACK, 1, 2 };
    SATURN_ASSERT(!sapp_proto_decode_hello_ack(bogus, sizeof(bogus), &ack));
}

SATURN_TEST(proto_wrong_type_rejected) {
    uint8_t buf[4] = { 0xAB, 0, 0, 0 };
    sapp_hello_t h;
    SATURN_ASSERT(!sapp_proto_decode_hello(buf, sizeof(buf), &h));
}

/* ---- M5 round-trips ---------------------------------------------------- */

SATURN_TEST(proto_ready_roundtrip) {
    uint8_t buf[8];
    size_t n = sapp_proto_encode_ready(buf, sizeof(buf), 1, 7);
    SATURN_ASSERT_EQ((long)n, 3L);
    sapp_ready_t r;
    SATURN_ASSERT(sapp_proto_decode_ready(buf, n, &r));
    SATURN_ASSERT_EQ(r.ready,   1);
    SATURN_ASSERT_EQ(r.game_id, 7);
}

SATURN_TEST(proto_vote_timer_roundtrip) {
    uint8_t buf[4];
    size_t n = sapp_proto_encode_vote_timer(buf, sizeof(buf), 4);
    SATURN_ASSERT_EQ((long)n, 2L);
    uint8_t s = 0;
    SATURN_ASSERT(sapp_proto_decode_vote_timer(buf, n, &s));
    SATURN_ASSERT_EQ(s, 4);
}

SATURN_TEST(proto_game_pick_roundtrip) {
    uint8_t buf[8];
    size_t n = sapp_proto_encode_game_pick(buf, sizeof(buf), 2, 0xDEADBEEFu);
    SATURN_ASSERT_EQ((long)n, 6L);
    sapp_game_pick_t p;
    SATURN_ASSERT(sapp_proto_decode_game_pick(buf, n, &p));
    SATURN_ASSERT_EQ(p.game_id, 2);
    SATURN_ASSERT_EQ((long)p.seed, (long)0xDEADBEEFu);
}

SATURN_TEST(proto_countdown_roundtrip) {
    uint8_t buf[4];
    size_t n = sapp_proto_encode_countdown(buf, sizeof(buf), 3);
    SATURN_ASSERT_EQ((long)n, 2L);
    uint8_t s = 0;
    SATURN_ASSERT(sapp_proto_decode_countdown(buf, n, &s));
    SATURN_ASSERT_EQ(s, 3);
}

SATURN_TEST(proto_game_start_roundtrip) {
    uint8_t buf[16];
    size_t n = sapp_proto_encode_game_start(buf, sizeof(buf), 1, 0x12345678u, 0, 2);
    SATURN_ASSERT_EQ((long)n, 11L);
    sapp_game_start_t gs;
    SATURN_ASSERT(sapp_proto_decode_game_start(buf, n, &gs));
    SATURN_ASSERT_EQ(gs.game_id,        1);
    SATURN_ASSERT_EQ((long)gs.seed,     (long)0x12345678u);
    SATURN_ASSERT_EQ((long)gs.start_tick, 0L);
    SATURN_ASSERT_EQ(gs.n_slots, 2);
}

SATURN_TEST(proto_input_roundtrip) {
    uint8_t buf[16];
    size_t n = sapp_proto_encode_input(buf, sizeof(buf), 42, 1, 0x000Au);
    SATURN_ASSERT_EQ((long)n, 8L);
    sapp_input_msg_t in;
    SATURN_ASSERT(sapp_proto_decode_input(buf, n, &in));
    SATURN_ASSERT_EQ((long)in.tick, 42L);
    SATURN_ASSERT_EQ(in.slot,  1);
    SATURN_ASSERT_EQ(in.input, 0x000Au);
}

SATURN_TEST(proto_bundle_roundtrip) {
    uint8_t buf[64];
    uint16_t inputs[] = { 0x1111, 0x2222, 0x3333 };
    size_t n = sapp_proto_encode_bundle(buf, sizeof(buf), 7, 3, inputs);
    SATURN_ASSERT_EQ((long)n, 1L + 4 + 1 + 6);
    sapp_bundle_t b;
    SATURN_ASSERT(sapp_proto_decode_bundle(buf, n, &b));
    SATURN_ASSERT_EQ((long)b.tick, 7L);
    SATURN_ASSERT_EQ(b.n, 3);
    SATURN_ASSERT_EQ(b.inputs[0], 0x1111);
    SATURN_ASSERT_EQ(b.inputs[1], 0x2222);
    SATURN_ASSERT_EQ(b.inputs[2], 0x3333);
}

SATURN_TEST(proto_round_end_roundtrip) {
    uint8_t buf[4];
    size_t n = sapp_proto_encode_round_end(buf, sizeof(buf),
                                           LOBBY_OUTCOME_WINNER, 0);
    SATURN_ASSERT_EQ((long)n, 3L);
    sapp_round_end_t r;
    SATURN_ASSERT(sapp_proto_decode_round_end(buf, n, &r));
    SATURN_ASSERT_EQ(r.outcome, LOBBY_OUTCOME_WINNER);
    SATURN_ASSERT_EQ(r.winner,  0);
}
