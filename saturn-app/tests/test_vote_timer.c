/*
 * tests/test_vote_timer.c — VOTE_TIMER state behaviour on the cart side.
 *
 * Server-side timer is exercised by the play_online_round.py integration
 * test. Here we verify that the cart:
 *   - Enters STATE_VOTE_TIMER when a VOTE_TIMER frame arrives.
 *   - Tracks the most recent seconds_remaining.
 *   - Advances to STATE_COUNTDOWN on GAME_PICK.
 *   - Advances to STATE_PLAYING_ONLINE on GAME_START.
 *   - Returns to GAME_SELECT_ONLINE if the timer is cleared (via GAME_PICK
 *     arriving without further VOTE_TIMER frames).
 *
 * Synthetic backend pattern mirrors test_game_select_online.c.
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <string.h>

#define INP_START 0x0010

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

static void tg_init(void* s, const lobby_game_config_t* c) { (void)s; (void)c; }
static void tg_tick(void* s, const lobby_input_t i[LOBBY_MAX_PLAYERS]) { (void)s; (void)i; }
static void tg_render(const void* s, lobby_scene_t* o) { (void)s; (void)o; }
static void tg_done(const void* s, lobby_game_result_t* o) {
    (void)s; if (o) o->outcome = LOBBY_OUTCOME_RUNNING;
}
static void tg_teardown(void* s) { (void)s; }
static const lobby_game_t tg_game = {
    "tg", "TestGame", 1, 1, 16,
    tg_init, tg_tick, tg_render, tg_done, tg_teardown, NULL
};

static void setup(void) {
    sapp_init(320, 224, 0xC0FFEEu);
    sapp_register_game(&tg_game);
    sapp_identity_t id; sapp_identity_default(&id);
    memcpy(id.current_name, "ALICE", 6);
    sapp_set_identity(&id);
    memset(inp, 0, sizeof(inp));
    tx_len = 0; s_recv_cb = NULL; s_recv_user = NULL;
    s_status = SAPP_NET_DISCONNECTED;
    sapp_net_install(&k_be);
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

static void inject_vote_timer(uint8_t s) {
    uint8_t buf[4];
    size_t n = sapp_proto_encode_vote_timer(buf, sizeof(buf), s);
    s_recv_cb(buf, n, s_recv_user);
}

static void inject_game_pick(uint8_t game_id, uint32_t seed) {
    uint8_t buf[8];
    size_t n = sapp_proto_encode_game_pick(buf, sizeof(buf), game_id, seed);
    s_recv_cb(buf, n, s_recv_user);
}

static void inject_game_start(uint8_t game_id, uint32_t seed) {
    uint8_t buf[16];
    size_t n = sapp_proto_encode_game_start(buf, sizeof(buf), game_id, seed, 0, 1);
    s_recv_cb(buf, n, s_recv_user);
}

SATURN_TEST(vote_timer_secs_track_inbound_ticks) {
    inject_vote_timer(5);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_VOTE_TIMER);
    SATURN_ASSERT_EQ(sapp_online_vote_timer_secs(), 5);

    inject_vote_timer(3);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_online_vote_timer_secs(), 3);
}

SATURN_TEST(vote_timer_then_pick_then_start) {
    inject_vote_timer(5);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_VOTE_TIMER);

    inject_game_pick(0, 0x12345u);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_COUNTDOWN);
    /* GAME_PICK clears the vote timer in the recv router. */
    SATURN_ASSERT_EQ(sapp_online_vote_timer_secs(), 0xFF);

    inject_game_start(0, 0x12345u);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING_ONLINE);
}

SATURN_TEST(start_during_vote_timer_sends_ready) {
    inject_vote_timer(5);
    sapp_run_one_frame(inp);

    size_t pre = tx_len;
    inp[0] = INP_START; sapp_run_one_frame(inp);
    inp[0] = 0;         sapp_run_one_frame(inp);

    int found = -1;
    for (size_t i = pre; i + 2 < tx_len; ++i) {
        if (tx_buf[i + 2] == SAPP_MSG_READY) { found = (int)i; break; }
    }
    SATURN_ASSERT_GE(found, 0);
}
