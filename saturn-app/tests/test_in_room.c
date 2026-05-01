/*
 * tests/test_in_room.c — drive the lobby's online mode via a synthetic
 * backend.
 *
 * Post-redesign there is no separate IN_ROOM state; "in a room" is the
 * lobby with mode == ONLINE (set automatically when a ROOM_STATE arrives
 * with valid=true). The action row shows LEAVE; pressing A on it (or B
 * from anywhere) sends ROOM_LEAVE and drops back to offline mode.
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"
#include "../state/state_online.h"
#include "../state/state_internal.h"

#include <string.h>

#define INP_DOWN  0x0004
#define INP_A     0x0020
#define INP_B     0x0040

#define TX_BUF_CAP 1024u

static uint8_t          tx_buf[TX_BUF_CAP];
static size_t           tx_len;
static sapp_net_recv_fn s_recv_cb;
static void*            s_recv_user;
static sapp_net_status_t s_status;

static bool  fake_connect(void* self)  { (void)self; s_status = SAPP_NET_CONNECTED; return true; }
static void  fake_disconnect(void* self){ (void)self; s_status = SAPP_NET_DISCONNECTED; }
static sapp_net_status_t fake_status(void* self) { (void)self; return s_status; }
static bool  fake_send_frame(void* self, const uint8_t* body, size_t len) {
    (void)self;
    if (tx_len + 2 + len > TX_BUF_CAP) return false;
    tx_buf[tx_len++] = (uint8_t)(len >> 8);
    tx_buf[tx_len++] = (uint8_t)(len & 0xFF);
    memcpy(tx_buf + tx_len, body, len);
    tx_len += len;
    return true;
}
static void  fake_poll(void* self) { (void)self; }
static void  fake_set_recv(void* self, sapp_net_recv_fn cb, void* user) {
    (void)self; s_recv_cb = cb; s_recv_user = user;
}

static const sapp_net_backend_t k_fake_be = {
    fake_connect, fake_disconnect, fake_status,
    fake_send_frame, fake_poll, fake_set_recv,
    NULL
};

static lobby_input_t inp[LOBBY_MAX_PLAYERS];

static void setup(void) {
    sapp_init(320, 224, 0xC0FFEEu);
    sapp_identity_t id;
    sapp_identity_default(&id);
    memcpy(id.current_name, "ALICE", 6);
    sapp_set_identity(&id);
    sapp_force_state(LOBBY_STATE_LOBBY);
    sapp_state_lobby_enter();
    memset(inp, 0, sizeof(inp));

    tx_len = 0; s_recv_cb = NULL; s_recv_user = NULL; s_status = SAPP_NET_DISCONNECTED;
    sapp_net_install(&k_fake_be);
}

static void teardown(void) {
    sapp_net_uninstall();
    sapp_shutdown();
}

SATURN_TEST_FIXTURE(setup, teardown);

/* Drive lobby into online mode by injecting CONNECT view + HELLO_ACK +
 * ROOM_STATE. */
static void drive_to_in_room(void) {
    /* DOWN through 8 slots to reach ACTION row. cursor_action defaults
     * to 0 (CONNECT). A activates -> CONNECTING view. */
    for (unsigned i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        inp[0] = INP_DOWN; sapp_run_one_frame(inp);
        inp[0] = 0;        sapp_run_one_frame(inp);
    }
    inp[0] = INP_A; sapp_run_one_frame(inp);
    inp[0] = 0;     sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_CONNECTING);

    /* CONNECTING view runs connect + sends HELLO. Pump one frame. */
    sapp_run_one_frame(inp);
    SATURN_ASSERT_GE(tx_len, 2u);
    SATURN_ASSERT_EQ(tx_buf[2], SAPP_MSG_HELLO);

    /* Inject HELLO_ACK. */
    uint8_t ack[64];
    uint8_t uuid[16] = {0};
    size_t  n = sapp_proto_encode_hello_ack(ack, sizeof(ack),
                                            SAPP_PROTO_VER, 0u, uuid, 0);
    SATURN_ASSERT_GT(n, 0);
    s_recv_cb(ack, n, s_recv_user);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_LOBBY_LIST);

    /* Inject a ROOM_STATE so the lobby auto-enters online mode. */
    uint8_t buf[256];
    size_t  off = 0;
    sapp_proto_put_u8 (buf, &off, SAPP_MSG_ROOM_STATE);
    sapp_proto_put_bytes(buf, &off, "ABCDEF", 6);
    sapp_proto_put_u8 (buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 8);
    sapp_proto_put_u8 (buf, &off, 1);
    sapp_proto_put_u32(buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 0);
    { uint8_t u[16]={0}; sapp_proto_put_bytes(buf, &off, u, 16); }
    sapp_proto_put_u8 (buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 0);
    sapp_proto_put_u8 (buf, &off, 5);
    sapp_proto_put_bytes(buf, &off, "ALICE", 5);
    s_recv_cb(buf, off, s_recv_user);

    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOBBY);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_DEFAULT);
    SATURN_ASSERT_EQ(sapp_lobby_get()->mode, SAPP_LOBBY_MODE_ONLINE);
}

SATURN_TEST(online_mode_entered_via_room_state) {
    drive_to_in_room();
    const sapp_room_state_t* rs = sapp_online_room_state();
    SATURN_ASSERT(rs->valid);
    SATURN_ASSERT_EQ(rs->n_members, 1);
    SATURN_ASSERT_STR_EQ(rs->members[0].name, "ALICE");
}

SATURN_TEST(online_b_anywhere_does_not_send_leave_yet) {
    /* In the new design, B in the default-online view is unbound (the
     * action row's LEAVE button must be activated). Just sanity-check
     * online mode persists when B is pressed in DEFAULT view. */
    drive_to_in_room();
    size_t pre_tx = tx_len;
    inp[0] = INP_B; sapp_run_one_frame(inp);
    inp[0] = 0;     sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_lobby_get()->mode, SAPP_LOBBY_MODE_ONLINE);
    /* No LEAVE sent (B in DEFAULT is a no-op in the unified lobby). */
    int found = 0;
    for (size_t i = pre_tx; i + 2 < tx_len; ++i) {
        if (tx_buf[i + 2] == SAPP_MSG_ROOM_LEAVE) { found = 1; break; }
    }
    SATURN_ASSERT(!found);
}

SATURN_TEST(online_action_leave_sends_leave_and_returns_to_offline) {
    drive_to_in_room();
    /* Move cursor to action row. Up to 10 DOWNs (wrap-safe). */
    for (unsigned i = 0; i < 10 && sapp_lobby_get()->focus != SAPP_LOBBY_FOCUS_ACTION; ++i) {
        inp[0] = INP_DOWN; sapp_run_one_frame(inp);
        inp[0] = 0;        sapp_run_one_frame(inp);
    }
    SATURN_ASSERT_EQ(sapp_lobby_get()->focus, SAPP_LOBBY_FOCUS_ACTION);
    size_t pre_tx = tx_len;
    inp[0] = INP_A; sapp_run_one_frame(inp);
    inp[0] = 0;     sapp_run_one_frame(inp);
    /* LEAVE sent and mode flipped back to offline. */
    int found = 0;
    for (size_t i = pre_tx; i + 2 < tx_len; ++i) {
        if (tx_buf[i + 2] == SAPP_MSG_ROOM_LEAVE) { found = 1; break; }
    }
    SATURN_ASSERT(found);
    SATURN_ASSERT_EQ(sapp_lobby_get()->mode, SAPP_LOBBY_MODE_OFFLINE);
}
