/*
 * test_heartbeat.c -- host-side unit test
 *
 * Exercises Phase 3c:
 *   - advanced.heartbeat_secs > 0 emits a ping frame every N poll-seconds
 *     (computed at init as heartbeat_secs * 60 poll ticks).
 *   - RX watchdog fires after 2*period of inactivity, surfacing
 *     STATE_DISCONNECTED + ERR_TIMEOUT to on_state.
 *
 * The library converts "seconds" to "poll ticks at 60 Hz", so on a
 * host driven by direct poll() calls one second == 60 polls. We
 * emulate this by calling saturn_online_poll() that many times.
 */

#include <stdio.h>
#include <string.h>

#include "saturn_online/net.h"
#include "saturn_online/transport.h"

/* ------------------------------------------------------------------------- */

typedef struct {
    uint8_t rx_buf[1024];
    uint32_t rx_len, rx_pos;
    uint8_t tx_buf[8192];
    uint32_t tx_len;
    bool connected;
} fake_transport_t;

static bool    ft_rx_ready(void* c) { fake_transport_t* f = c; return f->rx_pos < f->rx_len; }
static uint8_t ft_rx_byte(void* c)  { fake_transport_t* f = c; return f->rx_buf[f->rx_pos++]; }
static int     ft_send(void* c, const uint8_t* d, int l) {
    fake_transport_t* f = c;
    if (f->tx_len + l > sizeof(f->tx_buf)) return -1;
    memcpy(f->tx_buf + f->tx_len, d, l);
    f->tx_len += l;
    return l;
}
static bool    ft_is_connected(void* c) { fake_transport_t* f = c; return f->connected; }

static fake_transport_t g_ft = { .connected = true };
static const saturn_online_transport_t g_transport = {
    ft_rx_ready, ft_rx_byte, ft_send, ft_is_connected, &g_ft
};

static void on_frame(const uint8_t* p, uint16_t l, void* u) { (void)p; (void)l; (void)u; }

/* Capture the most recent state callback so the test can assert the
 * watchdog fired. */
static saturn_online_state_t  g_last_state_new  = SATURN_ONLINE_STATE_IDLE;
static saturn_online_status_t g_last_state_code = SATURN_ONLINE_OK;

static void on_state(saturn_online_state_t o, saturn_online_state_t n,
                      saturn_online_status_t s, void* u) {
    (void)o; (void)u;
    g_last_state_new  = n;
    g_last_state_code = s;
}

/* ------------------------------------------------------------------------- */

static int expect_i(const char* label, uint32_t got, uint32_t want) {
    int ok = (got == want);
    printf("%-50s : %s (got=%u, want=%u)\n", label,
           ok ? "PASS" : "FAIL", got, want);
    return ok ? 0 : 1;
}

static int expect_ge(const char* label, uint32_t got, uint32_t want_at_least) {
    int ok = (got >= want_at_least);
    printf("%-50s : %s (got=%u, want>=%u)\n", label,
           ok ? "PASS" : "FAIL", got, want_at_least);
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------------- */

int main(void) {
    int failures = 0;

    saturn_online_config_t cfg = SATURN_ONLINE_DEFAULTS;
    cfg.on_frame  = on_frame;
    cfg.on_state  = on_state;
    cfg.transport = &g_transport;
    cfg.advanced.monitor_dcd    = false;
    cfg.advanced.heartbeat_secs = 1;   /* period = 60 ticks, watchdog = 120 */

    failures += expect_i("init", saturn_online_init(&cfg), SATURN_ONLINE_OK);
    failures += expect_i("connect", saturn_online_connect(), SATURN_ONLINE_OK);

    /* --- heartbeat emission cadence ---
     *
     * To exercise two emissions we have to keep the RX watchdog happy,
     * because the watchdog fires after 2*period ticks of no RX and
     * would flip state to DISCONNECTED before the second emit. Feed
     * an arbitrary byte every poll to reset the watchdog. */
    static uint8_t keepalive_byte = 0xAA;

    for (int i = 0; i < 60; i++) {
        /* Re-arm the transport RX with a single byte so on_frame stays
         * unfired (single 0xAA is parsed as a frame header that stays
         * in the LEN_HI state) but the watchdog sees activity. */
        g_ft.rx_buf[0] = keepalive_byte;
        g_ft.rx_len = 1;
        g_ft.rx_pos = 0;
        saturn_online_poll();
    }

    saturn_online_stats_t s1 = saturn_online_get_stats();
    failures += expect_i("one heartbeat after 60 polls",
                         s1.heartbeats_sent, 1);
    failures += expect_ge("transport got >=3 heartbeat bytes",
                          (uint32_t)g_ft.tx_len, 3);

    for (int i = 0; i < 60; i++) {
        g_ft.rx_buf[0] = keepalive_byte;
        g_ft.rx_len = 1;
        g_ft.rx_pos = 0;
        saturn_online_poll();
    }
    s1 = saturn_online_get_stats();
    failures += expect_i("two heartbeats after 120 polls",
                         s1.heartbeats_sent, 2);

    /* --- watchdog ---
     * Now stop feeding RX. Watchdog was fully re-armed during the last
     * poll (because an RX byte arrived). We poll 2*period = 120 times
     * without data; on the 120th iteration the watchdog triggers. */
    g_ft.rx_len = 0;
    g_ft.rx_pos = 0;
    for (int i = 0; i < 200 && saturn_online_get_state()
                                    == SATURN_ONLINE_STATE_CONNECTED; i++) {
        saturn_online_poll();
    }

    failures += expect_i("watchdog moved state to DISCONNECTED",
                         g_last_state_new, SATURN_ONLINE_STATE_DISCONNECTED);
    failures += expect_i("watchdog status == ERR_TIMEOUT",
                         g_last_state_code, SATURN_ONLINE_ERR_TIMEOUT);

    if (failures == 0) {
        printf("\nAll checks passed.\n");
        return 0;
    }
    printf("\n%d check(s) failed.\n", failures);
    return 1;
}
