/*
 * tests/test_room_create.c — drive ROOM_CREATE keyboard.
 *
 * Type a name, commit (START), confirm a ROOM_CREATE frame is sent.
 */

#include <saturn_test/test.h>
#include <saturn_app.h>
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

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
    sapp_force_state(LOBBY_STATE_ROOM_CREATE);
    extern void sapp_state_room_create_enter(void);
    sapp_state_room_create_enter();
}
static void teardown(void) { sapp_net_uninstall(); sapp_shutdown(); }

SATURN_TEST_FIXTURE(setup, teardown);

static void tap(uint16_t mask) {
    inp[0] = mask;        sapp_run_one_frame(inp);
    inp[0] = 0;           sapp_run_one_frame(inp);
}

SATURN_TEST(room_create_commit_sends_create_frame) {
    /* Default kbd cursor is (0,0) -> 'A'. Press A to type 'A'. */
    tap(INP_A);
    /* Press START to commit (acts as OK from anywhere). */
    inp[0] = INP_START;
    sapp_run_one_frame(inp);
    inp[0] = 0;
    sapp_run_one_frame(inp);

    /* After commit we should have transitioned back to LOBBY_LIST. */
    SATURN_ASSERT_EQ(sapp_state(), LOBBY_STATE_LOBBY_LIST);

    /* And a ROOM_CREATE frame must be in tx_buf. */
    int found = 0;
    for (size_t i = 0; i + 3 < tx_len; ++i) {
        if (tx_buf[i + 2] == SAPP_MSG_ROOM_CREATE) { found = 1; break; }
    }
    SATURN_ASSERT(found);
}

SATURN_TEST(room_create_buffer_visible_during_typing) {
    tap(INP_A);   /* 'A' */
    tap(INP_RIGHT);
    tap(INP_A);   /* 'B' */
    const char* b = sapp_online_room_create_buffer();
    SATURN_ASSERT_NOT_NULL(b);
    SATURN_ASSERT_STR_EQ(b, "AB");
}
