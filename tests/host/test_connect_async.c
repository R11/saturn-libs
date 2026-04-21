/*
 * test_connect_async.c -- host-side unit test
 *
 * Exercises the Phase 3a non-blocking connect path. Only the
 * transport-override branch is host-testable (the hardware branch
 * dereferences SH-2 physical addresses). That's still enough to
 * verify the state-transition contract: connect_start() returns OK,
 * the first tick delivers OK, the state machine reports CONNECTED,
 * poll() runs cleanly, and a frame round-trip works.
 */

#include <stdio.h>
#include <string.h>

#include "saturn_online/net.h"
#include "saturn_online/transport.h"

/* ------------------------------------------------------------------------- */

typedef struct {
    uint8_t rx_buf[1024];
    uint32_t rx_len, rx_pos;
    uint8_t tx_buf[1024];
    uint32_t tx_len;
    bool connected;
} fake_transport_t;

static bool    ft_rx_ready(void* c) { fake_transport_t* f = c; return f->rx_pos < f->rx_len; }
static uint8_t ft_rx_byte(void* c)  { fake_transport_t* f = c; return f->rx_buf[f->rx_pos++]; }
static int     ft_send(void* c, const uint8_t* d, int l) {
    fake_transport_t* f = c;
    memcpy(f->tx_buf + f->tx_len, d, l);
    f->tx_len += l;
    return l;
}
static bool    ft_is_connected(void* c) { fake_transport_t* f = c; return f->connected; }

static fake_transport_t g_ft = { .connected = true };
static const saturn_online_transport_t g_transport = {
    ft_rx_ready, ft_rx_byte, ft_send, ft_is_connected, &g_ft
};

static int g_frames;
static void on_frame(const uint8_t* p, uint16_t l, void* u) { (void)p; (void)l; (void)u; g_frames++; }

/* ------------------------------------------------------------------------- */

static int expect(const char* label, int got, int want) {
    int ok = (got == want);
    printf("%-44s : %s (got=%d, want=%d)\n", label,
           ok ? "PASS" : "FAIL", got, want);
    return ok ? 0 : 1;
}

int main(void) {
    int failures = 0;

    saturn_online_config_t cfg = SATURN_ONLINE_DEFAULTS;
    cfg.on_frame  = on_frame;
    cfg.transport = &g_transport;
    cfg.advanced.monitor_dcd = false;

    failures += expect("init with transport",
                       saturn_online_init(&cfg),
                       SATURN_ONLINE_OK);

    failures += expect("state is IDLE after init",
                       saturn_online_get_state(),
                       SATURN_ONLINE_STATE_IDLE);

    failures += expect("connect_start OK",
                       saturn_online_connect_start(),
                       SATURN_ONLINE_OK);

    saturn_online_connect_tick_result_t r1 = saturn_online_connect_tick();
    failures += expect("first tick returns TICK_OK (transport)",
                       r1, SATURN_ONLINE_TICK_OK);

    failures += expect("state is CONNECTED",
                       saturn_online_get_state(),
                       SATURN_ONLINE_STATE_CONNECTED);

    /* Subsequent ticks continue to report OK. */
    saturn_online_connect_tick_result_t r2 = saturn_online_connect_tick();
    failures += expect("second tick remains TICK_OK",
                       r2, SATURN_ONLINE_TICK_OK);

    /* Round-trip: send a frame, feed one back via RX. */
    uint8_t hi[] = { 'h', 'i' };
    failures += expect("send works post-async-connect",
                       saturn_online_send(hi, 2),
                       SATURN_ONLINE_OK);
    failures += expect("transport received framed hi",
                       g_ft.tx_len >= 4 ? 1 : 0, 1);

    /* Inject a reply: [0x00][0x02]"ok" */
    const uint8_t reply[] = { 0x00, 0x02, 'o', 'k' };
    memcpy(g_ft.rx_buf, reply, sizeof(reply));
    g_ft.rx_len = sizeof(reply);
    g_ft.rx_pos = 0;
    saturn_online_poll();
    failures += expect("RX frame via transport",
                       g_frames, 1);

    if (failures == 0) {
        printf("\nAll checks passed.\n");
        return 0;
    }
    printf("\n%d check(s) failed.\n", failures);
    return 1;
}
