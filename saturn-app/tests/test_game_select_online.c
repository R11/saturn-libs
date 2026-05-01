/*
 * tests/test_game_select_online.c — vote encoding + state transitions for
 * STATE_GAME_SELECT_ONLINE / STATE_VOTE_TIMER / STATE_COUNTDOWN.
 *
 * Synthetic backend pattern mirrors test_in_room.c.
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <string.h>

#define INP_DOWN  0x0004
#define INP_UP    0x0008
#define INP_START 0x0010
#define INP_A     0x0020
#define INP_B     0x0040

#define TX_BUF_CAP 1024u

static uint8_t          tx_buf[TX_BUF_CAP];
static size_t           tx_len;
static sapp_net_recv_fn s_recv_cb;
static void*            s_recv_user;
static sapp_net_status_t s_status;

static bool  fk_connect(void* self)  { (void)self; s_status = SAPP_NET_CONNECTED; return true; }
static void  fk_disconnect(void* self){ (void)self; s_status = SAPP_NET_DISCONNECTED; }
static sapp_net_status_t fk_status(void* self) { (void)self; return s_status; }
static bool  fk_send_frame(void* self, const uint8_t* body, size_t len) {
    (void)self;
    if (tx_len + 2 + len > TX_BUF_CAP) return false;
    tx_buf[tx_len++] = (uint8_t)(len >> 8);
    tx_buf[tx_len++] = (uint8_t)(len & 0xFF);
    memcpy(tx_buf + tx_len, body, len);
    tx_len += len;
    return true;
}
static void  fk_poll(void* self) { (void)self; }
static void  fk_set_recv(void* self, sapp_net_recv_fn cb, void* user) {
    (void)self; s_recv_cb = cb; s_recv_user = user;
}

static const sapp_net_backend_t k_be = {
    fk_connect, fk_disconnect, fk_status,
    fk_send_frame, fk_poll, fk_set_recv,
    NULL
};

static lobby_input_t inp[LOBBY_MAX_PLAYERS];

/* A trivial test game — registered so sapp_count_games() > 0. */
static void tg_init(void* s, const lobby_game_config_t* c) { (void)s; (void)c; }
static void tg_tick(void* s, const lobby_input_t i[LOBBY_MAX_PLAYERS]) { (void)s; (void)i; }
static void tg_render(const void* s, lobby_scene_t* o) { (void)s; (void)o; }
static void tg_done(const void* s, lobby_game_result_t* o) {
    (void)s;
    if (o) o->outcome = LOBBY_OUTCOME_RUNNING;
}
static void tg_teardown(void* s) { (void)s; }
static const lobby_game_t tg_game = {
    "tg", "TestGame", 1, 1, 16,
    tg_init, tg_tick, tg_render, tg_done, tg_teardown, NULL
};
static const lobby_game_t tg_game2 = {
    "tg2", "OtherGame", 1, 1, 16,
    tg_init, tg_tick, tg_render, tg_done, tg_teardown, NULL
};

static void setup(void) {
    sapp_init(320, 224, 0xC0FFEEu);
    sapp_register_game(&tg_game);
    sapp_register_game(&tg_game2);
    sapp_identity_t id; sapp_identity_default(&id);
    memcpy(id.current_name, "ALICE", 6);
    sapp_set_identity(&id);
    memset(inp, 0, sizeof(inp));
    tx_len = 0; s_recv_cb = NULL; s_recv_user = NULL;
    s_status = SAPP_NET_DISCONNECTED;
    sapp_net_install(&k_be);
    /* CONNECTING_enter installs the framework's recv router into the
     * backend so injected frames land in the online ctx. */
    extern void sapp_state_connecting_enter(void);
    sapp_state_connecting_enter();
    extern void sapp_state_game_select_online_enter(void);
    sapp_state_game_select_online_enter();
    sapp_force_state(LOBBY_STATE_GAME_SELECT_ONLINE);
}

static void teardown(void) {
    sapp_net_uninstall();
    sapp_shutdown();
}

SATURN_TEST_FIXTURE(setup, teardown);

static int find_msg_after(size_t start, uint8_t type) {
    for (size_t i = start; i + 2 < tx_len; ++i) {
        if (tx_buf[i + 2] == type) return (int)i;
    }
    return -1;
}

SATURN_TEST(start_sends_ready_with_current_cursor) {
    /* Cursor starts at 0; START sends READY+vote=0+ready=1. */
    inp[0] = INP_START;
    sapp_run_one_frame(inp);
    inp[0] = 0;
    sapp_run_one_frame(inp);

    int idx = find_msg_after(0, SAPP_MSG_READY);
    SATURN_ASSERT_GE(idx, 0);
    SATURN_ASSERT_EQ(tx_buf[idx + 3], 1); /* ready */
    SATURN_ASSERT_EQ(tx_buf[idx + 4], 0); /* game_id */
}

SATURN_TEST(down_then_start_votes_for_second_game) {
    inp[0] = INP_DOWN; sapp_run_one_frame(inp);
    inp[0] = 0;        sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    inp[0] = 0;        sapp_run_one_frame(inp);

    /* Find the latest READY frame. */
    int last = -1;
    for (size_t i = 0; i + 2 < tx_len; ++i) {
        if (tx_buf[i + 2] == SAPP_MSG_READY) last = (int)i;
    }
    SATURN_ASSERT_GE(last, 0);
    SATURN_ASSERT_EQ(tx_buf[last + 3], 1); /* ready */
    SATURN_ASSERT_EQ(tx_buf[last + 4], 1); /* game_id = 1 */
}

SATURN_TEST(start_twice_toggles_ready_off) {
    inp[0] = INP_START; sapp_run_one_frame(inp);
    inp[0] = 0;         sapp_run_one_frame(inp);
    inp[0] = INP_START; sapp_run_one_frame(inp);
    inp[0] = 0;         sapp_run_one_frame(inp);
    /* The second READY should carry ready=0. */
    int last = -1;
    for (size_t i = 0; i + 2 < tx_len; ++i) {
        if (tx_buf[i + 2] == SAPP_MSG_READY) last = (int)i;
    }
    SATURN_ASSERT_GE(last, 0);
    SATURN_ASSERT_EQ(tx_buf[last + 3], 0); /* ready off */
}

SATURN_TEST(vote_timer_inbound_advances_to_vote_timer_state) {
    uint8_t buf[4];
    size_t n = sapp_proto_encode_vote_timer(buf, sizeof(buf), 5);
    s_recv_cb(buf, n, s_recv_user);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_VOTE_TIMER);
    SATURN_ASSERT_EQ(sapp_online_vote_timer_secs(), 5);
}

SATURN_TEST(game_pick_inbound_advances_to_countdown) {
    uint8_t buf[8];
    size_t n = sapp_proto_encode_game_pick(buf, sizeof(buf), 1, 0xABCDEFu);
    s_recv_cb(buf, n, s_recv_user);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_COUNTDOWN);
    SATURN_ASSERT_EQ(sapp_online_picked_game_id(), 1);
    SATURN_ASSERT_EQ((long)sapp_online_round_seed(), (long)0xABCDEFu);
}

SATURN_TEST(game_start_inbound_jumps_to_playing) {
    uint8_t buf[16];
    size_t n = sapp_proto_encode_game_start(buf, sizeof(buf), 0,
                                            0xC0FFEEu, 0, 1);
    s_recv_cb(buf, n, s_recv_user);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING_ONLINE);
}
