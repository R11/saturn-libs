/*
 * tests/test_slot_drop.c — M6 cart-side SLOT_DROP behaviour.
 *
 *   1. Frame round-trip for SLOT_DROP encode/decode.
 *   2. Cart receives SLOT_DROP, accessor returns true for that slot and
 *      false for others; last reason is exposed.
 *   3. Cart continues to PLAY through subsequent BUNDLEs even after a
 *      drop (game state ticks normally).
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <string.h>

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
static int           tg_tick_count;

static void tg_init(void* s, const lobby_game_config_t* c) { (void)c; if (s) memset(s, 0, 16); tg_tick_count = 0; }
static void tg_tick(void* s, const lobby_input_t i[LOBBY_MAX_PLAYERS]) { (void)s; (void)i; tg_tick_count++; }
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
    extern void sapp_online_install_recv(void);
    extern void sapp_online_ctx_reset(void);
    sapp_online_ctx_reset();
    sapp_online_install_recv();
}

static void teardown(void) {
    sapp_net_uninstall();
    sapp_shutdown();
}

SATURN_TEST_FIXTURE(setup, teardown);

static void inject_slot_drop(uint8_t slot, uint8_t reason) {
    uint8_t buf[8];
    size_t n = sapp_proto_encode_slot_drop(buf, sizeof(buf), slot, reason);
    s_recv_cb(buf, n, s_recv_user);
}

static void inject_game_start(uint8_t game_id, uint32_t seed, uint8_t n_slots) {
    uint8_t buf[16];
    size_t n = sapp_proto_encode_game_start(buf, sizeof(buf), game_id, seed, 0, n_slots);
    s_recv_cb(buf, n, s_recv_user);
}

static void inject_bundle(uint32_t tick, uint8_t n,
                          const uint16_t* inputs) {
    uint8_t buf[64];
    size_t  m = sapp_proto_encode_bundle(buf, sizeof(buf), tick, n, inputs);
    s_recv_cb(buf, m, s_recv_user);
}

/* ---- 1: frame round-trip --------------------------------------------- */

SATURN_TEST(proto_slot_drop_roundtrip) {
    uint8_t buf[16];
    size_t n = sapp_proto_encode_slot_drop(buf, sizeof(buf),
                                           3, SAPP_DROP_REASON_TIMEOUT);
    SATURN_ASSERT_EQ((long)n, 3L);
    SATURN_ASSERT_EQ(buf[0], SAPP_MSG_SLOT_DROP);
    SATURN_ASSERT_EQ(buf[1], 3);
    SATURN_ASSERT_EQ(buf[2], SAPP_DROP_REASON_TIMEOUT);

    sapp_slot_drop_t out;
    SATURN_ASSERT(sapp_proto_decode_slot_drop(buf, n, &out));
    SATURN_ASSERT_EQ(out.slot, 3);
    SATURN_ASSERT_EQ(out.reason, SAPP_DROP_REASON_TIMEOUT);
}

SATURN_TEST(proto_slot_drop_rejects_short) {
    uint8_t bad[2] = { SAPP_MSG_SLOT_DROP, 1 };
    sapp_slot_drop_t out;
    SATURN_ASSERT(!sapp_proto_decode_slot_drop(bad, sizeof(bad), &out));
}

/* ---- 2: cart accessor reflects SLOT_DROP ---------------------------- */

SATURN_TEST(slot_drop_marks_accessor) {
    SATURN_ASSERT(!sapp_online_slot_is_dropped(2));
    SATURN_ASSERT_EQ(sapp_online_last_drop_reason(), 0xFF);

    inject_slot_drop(2, SAPP_DROP_REASON_DISCONNECT);
    sapp_run_one_frame(inp);

    SATURN_ASSERT(sapp_online_slot_is_dropped(2));
    SATURN_ASSERT(!sapp_online_slot_is_dropped(0));
    SATURN_ASSERT(!sapp_online_slot_is_dropped(1));
    SATURN_ASSERT_EQ(sapp_online_last_drop_reason(),
                     SAPP_DROP_REASON_DISCONNECT);
}

SATURN_TEST(slot_drop_cleared_on_game_start) {
    inject_slot_drop(1, SAPP_DROP_REASON_TIMEOUT);
    sapp_run_one_frame(inp);
    SATURN_ASSERT(sapp_online_slot_is_dropped(1));

    inject_game_start(0, 0xDEADBEEFu, 2);
    sapp_run_one_frame(inp);

    SATURN_ASSERT(!sapp_online_slot_is_dropped(1));
    SATURN_ASSERT_EQ(sapp_online_last_drop_reason(), 0xFF);
}

/* ---- 3: bundles still tick the game after a drop -------------------- */

SATURN_TEST(playing_continues_through_drops) {
    /* Force playing-online state with our test game. Drive through
     * lobby (online) -> PLAYING_ONLINE via injected GAME_START. */
    extern void sapp_state_lobby_enter(void);
    sapp_force_state(LOBBY_STATE_LOBBY);
    sapp_state_lobby_enter();
    inject_game_start(0, 0x1234u, 2);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING_ONLINE);

    int ticks_before = tg_tick_count;

    /* Drop slot 1 then bundle a couple of ticks for both slots
     * (server forces 0 for slot 1; cart doesn't care, it just applies
     * what's in the BUNDLE). */
    inject_slot_drop(1, SAPP_DROP_REASON_TIMEOUT);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING_ONLINE);
    SATURN_ASSERT(sapp_online_slot_is_dropped(1));

    uint16_t bundle[2] = { 0, 0 };
    inject_bundle(0, 2, bundle);
    sapp_run_one_frame(inp);
    inject_bundle(1, 2, bundle);
    sapp_run_one_frame(inp);

    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_PLAYING_ONLINE);
    SATURN_ASSERT_GT(tg_tick_count, ticks_before);
}
