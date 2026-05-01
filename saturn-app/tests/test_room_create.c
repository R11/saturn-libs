/*
 * tests/test_room_create.c — drive ROOM_CREATE_KBD view inside the
 * unified lobby.
 *
 * Path: lobby ACTION CONNECT -> CONNECTING view -> HELLO_ACK ->
 * LOBBY_LIST view -> START opens ROOM_CREATE_KBD -> typing + commit
 * sends ROOM_CREATE.
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"
#include "../state/state_internal.h"

#include <string.h>

#define INP_RIGHT 0x0001
#define INP_DOWN  0x0004
#define INP_START 0x0010
#define INP_A     0x0020

#define TX_BUF_CAP 1024u
static uint8_t          tx_buf[TX_BUF_CAP];
static size_t           tx_len;
static sapp_net_recv_fn s_recv_cb;
static void*            s_recv_user;
static sapp_net_status_t s_status;

static bool  fconnect(void* s){(void)s; s_status=SAPP_NET_CONNECTED; return true;}
static void  fdisc(void* s)   {(void)s; s_status=SAPP_NET_DISCONNECTED;}
static sapp_net_status_t fstat(void* s){(void)s; return s_status;}
static bool  fsend(void* s, const uint8_t* b, size_t l){
    (void)s;
    if (tx_len + 2 + l > TX_BUF_CAP) return false;
    tx_buf[tx_len++]=(uint8_t)(l>>8); tx_buf[tx_len++]=(uint8_t)(l&0xFF);
    memcpy(tx_buf+tx_len, b, l); tx_len+=l; return true;
}
static void  fpoll(void* s){(void)s;}
static void  fset(void* s, sapp_net_recv_fn cb, void* u){(void)s; s_recv_cb=cb; s_recv_user=u;}

static const sapp_net_backend_t k_be = {
    fconnect, fdisc, fstat, fsend, fpoll, fset, NULL
};

static lobby_input_t inp[LOBBY_MAX_PLAYERS];

static void setup(void) {
    sapp_init(320, 224, 0xC0FFEEu);
    sapp_identity_t id;
    sapp_identity_default(&id);
    memcpy(id.current_name, "ALICE", 6);
    sapp_set_identity(&id);
    memset(inp, 0, sizeof(inp));
    tx_len = 0; s_recv_cb = NULL; s_status = SAPP_NET_CONNECTED;
    sapp_net_install(&k_be);
    sapp_force_state(LOBBY_STATE_LOBBY);
    sapp_state_lobby_enter();
}
static void teardown(void) { sapp_net_uninstall(); sapp_shutdown(); }

SATURN_TEST_FIXTURE(setup, teardown);

static void tap(uint16_t mask) {
    inp[0] = mask;        sapp_run_one_frame(inp);
    inp[0] = 0;           sapp_run_one_frame(inp);
}

/* Drive lobby into LOBBY_LIST view via CONNECT + HELLO_ACK. */
static void drive_to_lobby_list(void) {
    /* DOWN to action row. */
    for (unsigned i = 0; i < SAPP_LOBBY_SLOTS; ++i) tap(INP_DOWN);
    tap(INP_A);   /* CONNECT */
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_CONNECTING);
    sapp_run_one_frame(inp); /* drives connect + HELLO */
    /* Inject HELLO_ACK. */
    uint8_t ack[64];
    uint8_t uuid[16] = {0};
    size_t  n = sapp_proto_encode_hello_ack(ack, sizeof(ack),
                                            SAPP_PROTO_VER, 0u, uuid, 0);
    s_recv_cb(ack, n, s_recv_user);
    sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_LOBBY_LIST);
}

SATURN_TEST(room_create_commit_sends_create_frame) {
    drive_to_lobby_list();
    /* START in LOBBY_LIST opens ROOM_CREATE_KBD. */
    tap(INP_START);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_ROOM_CREATE_KBD);
    /* Default kbd cursor (0,0) -> 'A'. */
    tap(INP_A);
    /* START commits. */
    size_t pre = tx_len;
    inp[0] = INP_START; sapp_run_one_frame(inp);
    inp[0] = 0;         sapp_run_one_frame(inp);
    SATURN_ASSERT_EQ(sapp_lobby_get()->view, SAPP_LOBBY_VIEW_LOBBY_LIST);

    int found = 0;
    for (size_t i = pre; i + 3 < tx_len; ++i) {
        if (tx_buf[i + 2] == SAPP_MSG_ROOM_CREATE) { found = 1; break; }
    }
    SATURN_ASSERT(found);
}

SATURN_TEST(room_create_buffer_visible_during_typing) {
    drive_to_lobby_list();
    tap(INP_START);
    tap(INP_A);     /* type 'A' */
    tap(INP_RIGHT);
    tap(INP_A);     /* type 'B' */
    const char* b = sapp_online_room_create_buffer();
    SATURN_ASSERT_NOT_NULL(b);
    SATURN_ASSERT_STR_EQ(b, "AB");
}
