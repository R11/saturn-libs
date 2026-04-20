/**
 * net.c - saturn-online core implementation
 *
 * Extracted from saturn-tools' saturn/lib/net/saturn_net.c with all
 * SCP (canvas_protocol.h) coupling removed. Incoming bytes are
 * reassembled into length-prefixed frames via saturn_online_frame_*;
 * the payload is delivered to on_frame without interpretation.
 *
 * Chunked-transfer helpers and SCP-shaped framing wrappers are
 * intentionally NOT part of this library. Consumers that want those
 * (e.g. saturn-tools' SCP dispatch) layer them above saturn_online_send.
 */

#include "saturn_online/net.h"
#include "saturn_online/uart.h"
#include "saturn_online/modem.h"
#include "saturn_online/framing.h"
#include "saturn_online/transport.h"
#include "saturn_online_internal.h"

#include <string.h>

/*============================================================================
 * Known Modem Hardware Addresses
 *
 * The Saturn NetLink modem (MK-80118) uses a 16550 UART on the A-bus.
 * Different hardware revisions and emulators may use different base
 * addresses and register strides. We probe all known variants.
 *============================================================================*/

static const struct {
    uint32_t base;
    uint32_t stride;
} s_modem_addrs[] = {
    { 0x25895001, 4 },  /* NetLink MK-80118 (confirmed on real hardware) */
    { 0x04895001, 4 },  /* Emulator mapping */
    { 0x25895001, 2 },  /* HSS-0127 candidate: byte-spaced registers */
    { 0x25895000, 4 },  /* Even-byte base variant */
    { 0x25895000, 2 },  /* Even-byte + byte-spaced */
    { 0x25894001, 4 },  /* Alternate A-bus offset */
    { 0x25894001, 2 },  /* Alternate + byte-spaced */
};
#define MODEM_ADDR_COUNT \
    (int)(sizeof(s_modem_addrs) / sizeof(s_modem_addrs[0]))

/*============================================================================
 * Interrupt-Driven RX -- Ring Buffer (RX path)
 *
 * Single-producer (ISR writes head) / single-consumer (poll reads tail).
 * Power-of-2 size for fast index masking. No lock needed.
 *
 * At 9600 baud (960 bytes/sec) and 60 fps, ~16 bytes arrive per frame.
 * A 512-byte buffer gives ~32 frames of headroom before overflow.
 *
 * Struct type lives in saturn_online_internal.h so other TUs
 * (connect_async.c, matchmaking.c) can inspect state through the shared
 * singleton.
 *============================================================================*/

static inline void ringbuf_reset(saturn_online_ringbuf_t* rb) {
    rb->head = 0;
    rb->tail = 0;
}

static inline uint16_t ringbuf_count(const saturn_online_ringbuf_t* rb) {
    return (rb->head - rb->tail) & SATURN_ONLINE_RINGBUF_MASK;
}

static inline bool ringbuf_empty(const saturn_online_ringbuf_t* rb) {
    return rb->head == rb->tail;
}

/*
 * ringbuf_put is only called from the Saturn UART ISR. On non-Saturn
 * hosts the ISR is absent, so ringbuf_put would be an unused static and
 * the compiler emits -Wunused-function. Gate the producer helper behind
 * __SATURN__ to keep host-side builds clean.
 */
#if defined(__SATURN__)
static inline bool ringbuf_put(saturn_online_ringbuf_t* rb, uint8_t byte) {
    uint16_t next = (rb->head + 1) & SATURN_ONLINE_RINGBUF_MASK;
    if (next == rb->tail) return false; /* full */
    rb->buf[rb->head] = byte;
    rb->head = next;
    return true;
}
#endif

static inline uint8_t ringbuf_get(saturn_online_ringbuf_t* rb) {
    uint8_t byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & SATURN_ONLINE_RINGBUF_MASK;
    return byte;
}

/*============================================================================
 * TX Ring Buffer (optional, main-loop only)
 *
 * When advanced.tx_buffer_size > 0, saturn_online_send() copies into a
 * heap-free fixed buffer (SATURN_ONLINE_TX_BUFFER_MAX bytes cap) and
 * returns immediately. saturn_online_poll() drains the buffer as UART
 * TX is ready. Both producer and consumer run on the main loop -- no
 * concurrency concerns.
 *
 * Struct type lives in saturn_online_internal.h.
 *============================================================================*/

static void txbuf_reset(saturn_online_txbuf_t* tb) {
    tb->head = tb->tail = tb->count = 0;
}

static uint32_t txbuf_free(const saturn_online_txbuf_t* tb) {
    return tb->cap - tb->count;
}

static bool txbuf_push(saturn_online_txbuf_t* tb, const uint8_t* data,
                        uint32_t len) {
    if (len > txbuf_free(tb)) return false;
    for (uint32_t i = 0; i < len; i++) {
        tb->buf[tb->head] = data[i];
        tb->head = (tb->head + 1) % tb->cap;
    }
    tb->count += len;
    return true;
}

static bool txbuf_peek(const saturn_online_txbuf_t* tb, uint8_t* out) {
    if (tb->count == 0) return false;
    *out = tb->buf[tb->tail];
    return true;
}

static void txbuf_pop(saturn_online_txbuf_t* tb) {
    if (tb->count == 0) return;
    tb->tail = (tb->tail + 1) % tb->cap;
    tb->count--;
}

/*============================================================================
 * Internal State
 *
 * Struct definition lives in saturn_online_internal.h; this is the
 * single instance.
 *============================================================================*/

struct saturn_online_net_s s_net;

/*============================================================================
 * Modem diagnostics storage (replaces static globals in modem.h)
 *============================================================================*/

void saturn_online_modem_record_response(const char* response, int len) {
    if (len < 0) len = 0;
    if (len >= (int)sizeof(s_net.last_modem_response)) {
        len = (int)sizeof(s_net.last_modem_response) - 1;
    }
    memcpy(s_net.last_modem_response, response, (size_t)len);
    s_net.last_modem_response[len] = '\0';
    s_net.last_modem_response_len = len;
}

void saturn_online_modem_record_probe_divisor(uint16_t divisor) {
    s_net.probe_baud_divisor = divisor;
}

uint16_t saturn_online_modem_get_probe_divisor(void) {
    return s_net.probe_baud_divisor;
}

int saturn_online_last_modem_response(char* buf, int len) {
    if (!buf || len <= 0) return 0;
    int n = s_net.last_modem_response_len;
    if (n >= len) n = len - 1;
    if (n > 0) memcpy(buf, s_net.last_modem_response, (size_t)n);
    buf[n] = '\0';
    return n;
}

/*============================================================================
 * Wall-clock timeout conversion
 *
 * See Phase 1 commit message for rationale; this remains a calibrated
 * busy-wait heuristic rather than an FRT-based measurement because FRT
 * clock dividers can be changed by the game after init and invalidate
 * any cached calibration.
 *============================================================================*/

static uint32_t seconds_to_loop_iters(uint32_t secs) {
    const uint32_t iters_per_sec = 3000000UL;
    if (secs == 0) secs = 1;
    if (secs > 600) secs = 600;
    return secs * iters_per_sec;
}

/*============================================================================
 * Interrupt-Driven RX -- Saturn Hardware Setup
 *============================================================================*/

#if defined(__SATURN__)

#define SCU_REG_IMS    (*(volatile uint32_t*)0x25FE00A0)
#define SCU_REG_IST    (*(volatile uint32_t*)0x25FE00A4)
#define SCU_EXTINT12_BIT  (1u << 28)
#define SATURN_ONLINE_IRQ_VECTOR  0x5C
#define SATURN_UART_IER_RDA  0x01

static inline uint32_t sh2_get_vbr(void) {
    uint32_t vbr;
    __asm__ volatile("stc vbr, %0" : "=r"(vbr));
    return vbr;
}

static void __attribute__((interrupt_handler)) saturn_online_uart_isr(void)
{
    while (saturn_uart_rx_ready(&s_net.uart)) {
        uint8_t byte = (uint8_t)saturn_uart_reg_read(&s_net.uart,
                                                      SATURN_UART_RBR);
        if (!ringbuf_put(&s_net.ringbuf, byte)) {
            s_net.stats.irq_overflows++;
        }
    }
    SCU_REG_IST = SCU_EXTINT12_BIT;
}

static void irq_setup(void)
{
    uint32_t vbr = sh2_get_vbr();
    volatile uint32_t* vtable = (volatile uint32_t*)vbr;
    uint32_t mask;

    s_net.saved_vector = vtable[SATURN_ONLINE_IRQ_VECTOR];
    vtable[SATURN_ONLINE_IRQ_VECTOR] = (uint32_t)saturn_online_uart_isr;

    ringbuf_reset(&s_net.ringbuf);

    mask = SCU_REG_IMS;
    mask &= ~SCU_EXTINT12_BIT;
    SCU_REG_IMS = mask;

    saturn_uart_reg_write(&s_net.uart, SATURN_UART_IER, SATURN_UART_IER_RDA);

    s_net.irq_active = true;
}

static void irq_teardown(void)
{
    uint32_t vbr;
    volatile uint32_t* vtable;
    uint32_t mask;

    if (!s_net.irq_active) return;

    saturn_uart_reg_write(&s_net.uart, SATURN_UART_IER, 0x00);

    mask = SCU_REG_IMS;
    mask |= SCU_EXTINT12_BIT;
    SCU_REG_IMS = mask;

    vbr = sh2_get_vbr();
    vtable = (volatile uint32_t*)vbr;
    vtable[SATURN_ONLINE_IRQ_VECTOR] = s_net.saved_vector;

    s_net.irq_active = false;
}

#else /* !__SATURN__ */

static void irq_setup(void)    { s_net.irq_active = false; }
static void irq_teardown(void) { }

#endif /* __SATURN__ */

/*============================================================================
 * Shared helpers (exposed to other TUs)
 *============================================================================*/

void saturn_online_set_state(saturn_online_state_t new_state,
                              saturn_online_status_t status)
{
    saturn_online_state_t old = s_net.state;
    if (old == new_state) return;
    s_net.state = new_state;
    if (s_net.config.on_state) {
        s_net.config.on_state(old, new_state, status, s_net.config.user);
    }
}

void saturn_online_report(const char* msg)
{
    if (s_net.config.on_status) {
        s_net.config.on_status(msg, s_net.config.user);
    }
}

/*============================================================================
 * Modem / connect internals
 *============================================================================*/

static bool detect_modem(void)
{
    int pass, i;

    for (pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            saturn_online_report("Powering on modem...");
            saturn_netlink_smpc_enable();
            saturn_online_report("Modem powered on");
        } else {
            saturn_online_report("Extended settle...");
            for (volatile uint32_t d = 0; d < 4000000; d++);
        }

        for (i = 0; i < MODEM_ADDR_COUNT; i++) {
            saturn_uart16550_t probe;
            probe.base   = s_modem_addrs[i].base;
            probe.stride = s_modem_addrs[i].stride;

            saturn_uart_detect_result_t r = saturn_uart_detect_verbose(&probe);

            if (r.detected) {
                s_net.uart = probe;
                s_net.uart_found = true;
                saturn_online_report("Modem found");
                return true;
            }

            if (r.lsr != 0xFF && r.lsr != 0x00 &&
                (r.lsr & (SATURN_UART_LSR_THRE | SATURN_UART_LSR_TEMT))) {
                s_net.uart = probe;
                if (saturn_online_modem_probe(&s_net.uart)
                        == SATURN_ONLINE_MODEM_OK) {
                    s_net.uart_found = true;
                    saturn_online_report("Modem found (AT)");
                    return true;
                }
            }
        }
    }
    return false;
}

static saturn_online_status_t probe_and_init_modem(void)
{
    saturn_online_modem_result_t probe_result;

    saturn_online_report("Probing modem...");

    if (s_net.config.advanced.fixed_baud_divisor != 0) {
        probe_result = saturn_online_modem_probe_fixed(
            &s_net.uart, s_net.config.advanced.fixed_baud_divisor);
    } else {
        probe_result = saturn_online_modem_probe(&s_net.uart);
    }

    if (probe_result != SATURN_ONLINE_MODEM_OK) {
        saturn_online_report("No modem response");
        return SATURN_ONLINE_ERR_NO_RESPONSE;
    }

    s_net.stats.baud_rate = saturn_online_modem_get_probe_baud();
    saturn_online_report("Initializing modem...");

    if (saturn_online_modem_init(&s_net.uart) != SATURN_ONLINE_MODEM_OK) {
        saturn_online_report("Modem init failed");
        return SATURN_ONLINE_ERR_INIT_FAILED;
    }

    saturn_online_report("Modem ready");
    return SATURN_ONLINE_OK;
}

static saturn_online_status_t do_dial(void)
{
    saturn_online_modem_result_t result;

    saturn_online_report("Dialing...");
    result = saturn_online_modem_dial(&s_net.uart,
                                       s_net.config.dial_number,
                                       s_net.dial_timeout_iters);

    switch (result) {
    case SATURN_ONLINE_MODEM_CONNECT:
        saturn_online_modem_flush_input(&s_net.uart);
        return SATURN_ONLINE_OK;
    case SATURN_ONLINE_MODEM_NO_CARRIER:  saturn_online_report("No carrier"); return SATURN_ONLINE_ERR_NO_CARRIER;
    case SATURN_ONLINE_MODEM_BUSY:        saturn_online_report("Busy");       return SATURN_ONLINE_ERR_BUSY;
    case SATURN_ONLINE_MODEM_NO_DIALTONE: saturn_online_report("No dial tone"); return SATURN_ONLINE_ERR_NO_DIALTONE;
    case SATURN_ONLINE_MODEM_NO_ANSWER:   saturn_online_report("No answer");  return SATURN_ONLINE_ERR_NO_ANSWER;
    case SATURN_ONLINE_MODEM_TIMEOUT_ERR: saturn_online_report("Dial timeout"); return SATURN_ONLINE_ERR_TIMEOUT;
    default:                              saturn_online_report("Dial failed"); return SATURN_ONLINE_ERR_NO_CARRIER;
    }
}

static saturn_online_status_t do_answer(void)
{
    char buf[SATURN_ONLINE_MODEM_LINE_MAX];
    saturn_online_modem_result_t result;

    saturn_online_report("Waiting for call...");

    result = saturn_online_modem_command_timeout(
        &s_net.uart, "ATS0=1", buf, sizeof(buf), s_net.dial_timeout_iters);
    if (result != SATURN_ONLINE_MODEM_OK) {
        saturn_online_report("Auto-answer setup failed");
        return SATURN_ONLINE_ERR_INIT_FAILED;
    }

    while (1) {
        int len = saturn_online_modem_read_line(
            &s_net.uart, buf, sizeof(buf), s_net.dial_timeout_iters);
        if (len < 0) {
            saturn_online_report("Answer timeout");
            return SATURN_ONLINE_ERR_TIMEOUT;
        }
        if (len == 0) continue;

        saturn_online_modem_result_t r =
            saturn_online_modem_parse_response(buf);
        if (r == SATURN_ONLINE_MODEM_CONNECT) {
            saturn_online_modem_flush_input(&s_net.uart);
            return SATURN_ONLINE_OK;
        }
        if (r == SATURN_ONLINE_MODEM_NO_CARRIER
                || r == SATURN_ONLINE_MODEM_ERROR) {
            saturn_online_report("Answer failed");
            return SATURN_ONLINE_ERR_NO_CARRIER;
        }
    }
}

static void reset_receiver(void)
{
    if (!s_net.transport) {
        saturn_uart_reg_write(&s_net.uart, SATURN_UART_FCR,
            SATURN_UART_FCR_ENABLE | SATURN_UART_FCR_RXRESET);
    }
    saturn_online_frame_init(&s_net.rx);
    ringbuf_reset(&s_net.ringbuf);
    txbuf_reset(&s_net.txbuf);
    s_net.heartbeat_ticks_remaining = s_net.heartbeat_ticks_period;
    s_net.heartbeat_rx_watchdog =
        s_net.heartbeat_ticks_period ? (s_net.heartbeat_ticks_period * 2) : 0;
}

static bool check_carrier(void)
{
    if (s_net.transport) {
        return saturn_online_transport_is_connected(s_net.transport);
    }
    uint8_t msr = saturn_uart_reg_read(&s_net.uart, SATURN_UART_MSR);
    return (msr & SATURN_UART_MSR_DCD) != 0;
}

static saturn_online_status_t do_reconnect_internal(void)
{
    char buf[SATURN_ONLINE_MODEM_LINE_MAX];
    saturn_online_status_t status;

    /* Transport-driven sessions don't support full reconnect (we don't
     * know how to re-open the underlying transport). Surface a clear
     * error to the caller. */
    if (s_net.transport) return SATURN_ONLINE_ERR_INVALID_CONFIG;

    saturn_online_set_state(SATURN_ONLINE_STATE_RECONNECTING,
                             SATURN_ONLINE_ERR_CARRIER_LOST);

    irq_teardown();

    saturn_online_report("Escape to command...");
    saturn_online_modem_escape_to_command(&s_net.uart);

    saturn_online_report("Hanging up...");
    saturn_online_modem_command(&s_net.uart, "ATH0", buf, sizeof(buf));

    status = probe_and_init_modem();
    if (status != SATURN_ONLINE_OK) return status;

    saturn_online_report("Re-dialing...");
    if (s_net.config.mode == SATURN_ONLINE_MODE_DIAL) {
        status = do_dial();
    } else {
        status = do_answer();
    }
    if (status != SATURN_ONLINE_OK) return status;

    reset_receiver();

    if (s_net.config.advanced.use_irq) {
        irq_setup();
    }

    s_net.stats.reconnections++;
    saturn_online_report("Reconnected");
    return SATURN_ONLINE_OK;
}

/*============================================================================
 * TX paths
 *
 * Centralizes the "send N bytes" operation so saturn_online_send,
 * the async connect path (Phase 3), and the heartbeat emitter can
 * share the same TX-buffer vs. direct-write choice.
 *============================================================================*/

static bool direct_putc(uint8_t byte)
{
    if (s_net.transport) {
        uint8_t b = byte;
        return saturn_online_transport_send(s_net.transport, &b, 1) == 1;
    }
    return saturn_uart_putc(&s_net.uart, byte);
}

static bool tx_write_direct(const uint8_t* data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (!direct_putc(data[i])) return false;
    }
    return true;
}

static bool tx_enqueue(const uint8_t* data, uint32_t len)
{
    if (!txbuf_push(&s_net.txbuf, data, len)) return false;
    if (s_net.txbuf.count > s_net.stats.tx_buffer_peak) {
        s_net.stats.tx_buffer_peak = s_net.txbuf.count;
    }
    return true;
}

static void tx_drain(void)
{
    if (!s_net.txbuf_enabled) return;

    uint8_t byte;
    while (txbuf_peek(&s_net.txbuf, &byte)) {
        if (s_net.transport) {
            int sent = saturn_online_transport_send(s_net.transport, &byte, 1);
            if (sent != 1) return;
        } else {
            if (!saturn_uart_tx_ready(&s_net.uart)) return;
            if (!saturn_uart_putc(&s_net.uart, byte)) return;
        }
        txbuf_pop(&s_net.txbuf);
    }
}

/*============================================================================
 * Heartbeat
 *
 * Emits a single-byte frame (payload 0x00) at the configured interval.
 * The emitter is driven from poll(), so it runs at the game's poll rate;
 * heartbeat_secs is interpreted as "seconds at 60 Hz poll".
 *============================================================================*/

static void heartbeat_emit(void)
{
    /* [LEN_HI=0][LEN_LO=1][payload=0x00] */
    uint8_t wire[3] = { 0x00, 0x01, 0x00 };

    if (s_net.txbuf_enabled) {
        if (txbuf_free(&s_net.txbuf) >= 3 && tx_enqueue(wire, 3)) {
            s_net.stats.bytes_sent += 3;
            s_net.stats.frames_sent++;
            s_net.stats.heartbeats_sent++;
        } else {
            s_net.stats.tx_frames_dropped++;
        }
    } else {
        if (tx_write_direct(wire, 3)) {
            s_net.stats.bytes_sent += 3;
            s_net.stats.frames_sent++;
            s_net.stats.heartbeats_sent++;
        }
    }
}

/*============================================================================
 * Public API -- Init / Connect
 *============================================================================*/

saturn_online_status_t saturn_online_init(const saturn_online_config_t* config)
{
    if (s_net.initialized) {
        return SATURN_ONLINE_ERR_ALREADY_INIT;
    }

    if (!config || !config->on_frame) {
        return SATURN_ONLINE_ERR_INVALID_CONFIG;
    }

    /* Reject placeholder dial_number when using the default UART
     * transport and dial mode. Transport override skips dial entirely,
     * as does answer mode. */
    if (!config->transport && config->mode == SATURN_ONLINE_MODE_DIAL) {
        const char* n = config->dial_number;
        if (n == 0 || n[0] == '\0' || strcmp(n, "0000000") == 0) {
            return SATURN_ONLINE_ERR_INVALID_CONFIG;
        }
    }

    uint32_t tx_cap = config->advanced.tx_buffer_size;
    if (tx_cap > SATURN_ONLINE_TX_BUFFER_MAX) {
        tx_cap = SATURN_ONLINE_TX_BUFFER_MAX;
    }

    memset(&s_net, 0, sizeof(s_net));
    s_net.config = *config;
    s_net.state = SATURN_ONLINE_STATE_IDLE;
    s_net.initialized = true;
    s_net.transport = config->transport;

    s_net.dial_timeout_iters =
        seconds_to_loop_iters(config->dial_timeout_secs);

    s_net.txbuf_enabled = (tx_cap > 0);
    s_net.txbuf.cap = tx_cap ? tx_cap : 1;  /* avoid div-by-zero */

    /* Heartbeat tick period measured in poll() invocations. Games that
     * poll at 60 Hz get actual seconds; off-rate callers scale to
     * their own poll frequency. */
    s_net.heartbeat_ticks_period = config->advanced.heartbeat_secs * 60;
    s_net.heartbeat_ticks_remaining = s_net.heartbeat_ticks_period;
    s_net.heartbeat_rx_watchdog =
        s_net.heartbeat_ticks_period ? (s_net.heartbeat_ticks_period * 2) : 0;

    saturn_online_frame_init(&s_net.rx);

    return SATURN_ONLINE_OK;
}

static saturn_online_status_t connect_via_transport(void)
{
    reset_receiver();
    s_net.reconnect_attempts = 0;
    saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTED, SATURN_ONLINE_OK);
    saturn_online_report("Connected via transport");
    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_connect(void)
{
    saturn_online_status_t status;

    if (!s_net.initialized) {
        return SATURN_ONLINE_ERR_INVALID_CONFIG;
    }

    if (s_net.transport) {
        return connect_via_transport();
    }

    saturn_online_set_state(SATURN_ONLINE_STATE_DETECTING, SATURN_ONLINE_OK);

    if (!s_net.uart_found) {
        if (!detect_modem()) {
            saturn_online_set_state(SATURN_ONLINE_STATE_ERROR,
                                     SATURN_ONLINE_ERR_NO_MODEM);
            return SATURN_ONLINE_ERR_NO_MODEM;
        }
    }

    saturn_online_set_state(SATURN_ONLINE_STATE_PROBING, SATURN_ONLINE_OK);

    status = probe_and_init_modem();
    if (status != SATURN_ONLINE_OK) {
        saturn_online_set_state(SATURN_ONLINE_STATE_ERROR, status);
        return status;
    }

    saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTING, SATURN_ONLINE_OK);

    if (s_net.config.mode == SATURN_ONLINE_MODE_DIAL) {
        status = do_dial();
    } else {
        status = do_answer();
    }

    if (status != SATURN_ONLINE_OK) {
        saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED, status);
        return status;
    }

    reset_receiver();
    s_net.reconnect_attempts = 0;

    if (s_net.config.advanced.use_irq) {
        irq_setup();
        if (s_net.irq_active) {
            saturn_online_report("IRQ receive enabled");
        }
    }

    saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTED, SATURN_ONLINE_OK);
    saturn_online_report("Connected");

    return SATURN_ONLINE_OK;
}

/*============================================================================
 * Public API -- Poll / Send
 *============================================================================*/

saturn_online_status_t saturn_online_poll(void)
{
    int bytes_read = 0;
    uint16_t frames_read = 0;

    if (!s_net.initialized) return SATURN_ONLINE_OK;

    s_net.stats.polls++;

    /* Drain any pending TX before other work. */
    if (s_net.txbuf_enabled) tx_drain();

    /* Check connection health */
    if (s_net.state == SATURN_ONLINE_STATE_CONNECTED &&
        s_net.config.advanced.monitor_dcd)
    {
        if (!check_carrier()) {
            s_net.stats.carrier_losses++;
            saturn_online_report("Carrier lost");

            if (s_net.config.advanced.auto_reconnect && !s_net.transport) {
                int max = s_net.config.advanced.max_reconnect_attempts;
                if (max == 0 || s_net.reconnect_attempts < max) {
                    s_net.reconnect_attempts++;
                    saturn_online_status_t rs = do_reconnect_internal();
                    if (rs == SATURN_ONLINE_OK) {
                        s_net.reconnect_attempts = 0;
                        saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTED,
                                                 SATURN_ONLINE_OK);
                        return SATURN_ONLINE_OK;
                    }
                }
                saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED,
                                         SATURN_ONLINE_ERR_CARRIER_LOST);
                return SATURN_ONLINE_ERR_CARRIER_LOST;
            }

            saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED,
                                     SATURN_ONLINE_ERR_CARRIER_LOST);
            return SATURN_ONLINE_ERR_CARRIER_LOST;
        }
    }

    /* Read bytes and feed to frame receiver.
     *
     * Three paths, same frame processing:
     *   transport override: read from the caller-supplied transport.
     *   use_irq=true:       ISR already buffered bytes -> drain ring buffer.
     *   use_irq=false:      poll UART registers directly (original behavior).
     */
    if (s_net.state == SATURN_ONLINE_STATE_CONNECTED) {
        uint16_t byte_limit  = s_net.config.advanced.max_bytes_per_poll;
        uint16_t frame_limit = s_net.config.advanced.max_frames_per_poll;

        if (s_net.transport) {
            while (saturn_online_transport_rx_ready(s_net.transport)) {
                uint8_t byte = saturn_online_transport_rx_byte(s_net.transport);
                s_net.stats.bytes_received++;
                bytes_read++;

                if (saturn_online_frame_feed(&s_net.rx, byte)) {
                    s_net.stats.frames_received++;
                    frames_read++;
                    s_net.config.on_frame(s_net.rx.buf,
                                           s_net.rx.payload_len,
                                           s_net.config.user);
                    if (frame_limit > 0 && frames_read >= frame_limit) break;
                }

                if (byte_limit > 0 && bytes_read >= byte_limit) break;
            }
        } else if (s_net.irq_active) {
            while (!ringbuf_empty(&s_net.ringbuf)) {
                uint8_t byte = ringbuf_get(&s_net.ringbuf);
                s_net.stats.bytes_received++;
                bytes_read++;

                if (saturn_online_frame_feed(&s_net.rx, byte)) {
                    s_net.stats.frames_received++;
                    frames_read++;
                    s_net.config.on_frame(s_net.rx.buf,
                                           s_net.rx.payload_len,
                                           s_net.config.user);
                    if (frame_limit > 0 && frames_read >= frame_limit) break;
                }

                if (byte_limit > 0 && bytes_read >= byte_limit) break;
            }
        } else {
            while (saturn_uart_rx_ready(&s_net.uart)) {
                uint8_t byte = (uint8_t)saturn_uart_reg_read(&s_net.uart,
                                                              SATURN_UART_RBR);
                s_net.stats.bytes_received++;
                bytes_read++;

                if (saturn_online_frame_feed(&s_net.rx, byte)) {
                    s_net.stats.frames_received++;
                    frames_read++;
                    s_net.config.on_frame(s_net.rx.buf,
                                           s_net.rx.payload_len,
                                           s_net.config.user);
                    if (frame_limit > 0 && frames_read >= frame_limit) break;
                }

                if (byte_limit > 0 && bytes_read >= byte_limit) break;
            }
        }

        /* Heartbeat book-keeping. */
        if (s_net.heartbeat_ticks_period != 0) {
            if (bytes_read > 0) {
                s_net.heartbeat_rx_watchdog = s_net.heartbeat_ticks_period * 2;
            } else if (s_net.heartbeat_rx_watchdog > 0) {
                if (--s_net.heartbeat_rx_watchdog == 0) {
                    saturn_online_report("Heartbeat watchdog");
                    saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED,
                                             SATURN_ONLINE_ERR_TIMEOUT);
                    return SATURN_ONLINE_ERR_TIMEOUT;
                }
            }

            if (s_net.heartbeat_ticks_remaining > 0) {
                if (--s_net.heartbeat_ticks_remaining == 0) {
                    heartbeat_emit();
                    s_net.heartbeat_ticks_remaining =
                        s_net.heartbeat_ticks_period;
                }
            }
        }

        s_net.stats.connected_frames++;
    }

    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_send(const uint8_t* payload, uint16_t len)
{
    if (s_net.state != SATURN_ONLINE_STATE_CONNECTED) {
        return SATURN_ONLINE_ERR_NOT_CONNECTED;
    }

    if (!payload || len == 0 || len > SATURN_ONLINE_MAX_PAYLOAD) {
        return SATURN_ONLINE_ERR_SEND_FAILED;
    }

    uint8_t hdr[2];
    hdr[0] = (uint8_t)(len >> 8);
    hdr[1] = (uint8_t)(len & 0xFF);

    if (s_net.txbuf_enabled) {
        /* Buffered path: atomic enqueue of header + payload. Return
         * WOULDBLOCK if either part can't fit (don't split). */
        if (txbuf_free(&s_net.txbuf) < (uint32_t)(2 + len)) {
            s_net.stats.tx_frames_dropped++;
            return SATURN_ONLINE_ERR_WOULDBLOCK;
        }
        if (!tx_enqueue(hdr, 2) || !tx_enqueue(payload, len)) {
            s_net.stats.tx_frames_dropped++;
            return SATURN_ONLINE_ERR_WOULDBLOCK;
        }
        s_net.stats.bytes_sent += 2 + len;
        s_net.stats.frames_sent++;
        return SATURN_ONLINE_OK;
    }

    if (!tx_write_direct(hdr, 2) || !tx_write_direct(payload, len)) {
        return SATURN_ONLINE_ERR_SEND_FAILED;
    }

    s_net.stats.bytes_sent += 2 + len;
    s_net.stats.frames_sent++;

    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_disconnect(void)
{
    char buf[SATURN_ONLINE_MODEM_LINE_MAX];

    if (!s_net.initialized) return SATURN_ONLINE_OK;

    irq_teardown();

    if (s_net.state == SATURN_ONLINE_STATE_CONNECTED && !s_net.transport) {
        saturn_online_modem_escape_to_command(&s_net.uart);
        saturn_online_modem_command(&s_net.uart, "ATH0", buf, sizeof(buf));
    }

    saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED, SATURN_ONLINE_OK);
    return SATURN_ONLINE_OK;
}

void saturn_online_shutdown(void)
{
    if (!s_net.initialized) return;

    saturn_online_disconnect();

    /* Power off the modem only when using the default UART transport. */
    if (!s_net.transport) {
        saturn_netlink_smpc_disable();
    }

    s_net.initialized = false;
    s_net.uart_found = false;
    saturn_online_set_state(SATURN_ONLINE_STATE_IDLE, SATURN_ONLINE_OK);
}

/*============================================================================
 * State & Statistics
 *============================================================================*/

saturn_online_state_t saturn_online_get_state(void)   { return s_net.state; }
bool saturn_online_is_connected(void)                 { return s_net.state == SATURN_ONLINE_STATE_CONNECTED; }
saturn_online_stats_t saturn_online_get_stats(void)   { return s_net.stats; }

void saturn_online_reset_stats(void)
{
    uint32_t baud = s_net.stats.baud_rate;
    memset(&s_net.stats, 0, sizeof(s_net.stats));
    s_net.stats.baud_rate = baud;
}

const char* saturn_online_state_name(saturn_online_state_t state)
{
    switch (state) {
    case SATURN_ONLINE_STATE_IDLE:          return "Idle";
    case SATURN_ONLINE_STATE_DETECTING:     return "Detecting";
    case SATURN_ONLINE_STATE_PROBING:       return "Probing";
    case SATURN_ONLINE_STATE_CONNECTING:    return "Connecting";
    case SATURN_ONLINE_STATE_CONNECTED:     return "Connected";
    case SATURN_ONLINE_STATE_DISCONNECTED:  return "Disconnected";
    case SATURN_ONLINE_STATE_RECONNECTING:  return "Reconnecting";
    case SATURN_ONLINE_STATE_ERROR:         return "Error";
    default:                                return "Unknown";
    }
}

uint16_t saturn_online_irq_pending(void)
{
    if (!s_net.irq_active) return 0;
    return ringbuf_count(&s_net.ringbuf);
}

const char* saturn_online_status_name(saturn_online_status_t status)
{
    switch (status) {
    case SATURN_ONLINE_OK:                  return "OK";
    case SATURN_ONLINE_ERR_NO_MODEM:        return "No modem found";
    case SATURN_ONLINE_ERR_NO_RESPONSE:     return "Modem not responding";
    case SATURN_ONLINE_ERR_INIT_FAILED:     return "Modem init failed";
    case SATURN_ONLINE_ERR_NO_CARRIER:      return "No carrier";
    case SATURN_ONLINE_ERR_BUSY:            return "Busy";
    case SATURN_ONLINE_ERR_NO_DIALTONE:     return "No dial tone";
    case SATURN_ONLINE_ERR_NO_ANSWER:       return "No answer";
    case SATURN_ONLINE_ERR_TIMEOUT:         return "Timeout";
    case SATURN_ONLINE_ERR_CARRIER_LOST:    return "Carrier lost";
    case SATURN_ONLINE_ERR_NOT_CONNECTED:   return "Not connected";
    case SATURN_ONLINE_ERR_SEND_FAILED:     return "Send failed";
    case SATURN_ONLINE_ERR_INVALID_CONFIG:  return "Invalid config";
    case SATURN_ONLINE_ERR_ALREADY_INIT:    return "Already initialized";
    case SATURN_ONLINE_ERR_WOULDBLOCK:      return "Would block (TX full)";
    case SATURN_ONLINE_ERR_INTERNAL:        return "Internal error";
    default:                                return "Unknown error";
    }
}

/*============================================================================
 * Advanced / Low-Level Access
 *============================================================================*/

const saturn_uart16550_t* saturn_online_get_uart(void)
{
    if (!s_net.initialized || !s_net.uart_found || s_net.transport) return 0;
    return &s_net.uart;
}

saturn_online_status_t saturn_online_reconnect(void)
{
    if (!s_net.initialized) return SATURN_ONLINE_ERR_INVALID_CONFIG;
    if (s_net.transport)    return SATURN_ONLINE_ERR_INVALID_CONFIG;

    saturn_online_status_t status = do_reconnect_internal();
    if (status == SATURN_ONLINE_OK) {
        s_net.reconnect_attempts = 0;
        saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTED, SATURN_ONLINE_OK);
    } else {
        saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED, status);
    }
    return status;
}

saturn_online_status_t saturn_online_at_command(const char* cmd,
                                                char* response, int buf_len)
{
    if (!s_net.initialized || !s_net.uart_found) {
        return SATURN_ONLINE_ERR_INVALID_CONFIG;
    }
    if (s_net.state == SATURN_ONLINE_STATE_CONNECTED) {
        return SATURN_ONLINE_ERR_NOT_CONNECTED;
    }

    saturn_online_modem_result_t r =
        saturn_online_modem_command(&s_net.uart, cmd, response, buf_len);
    return (r == SATURN_ONLINE_MODEM_OK)
                ? SATURN_ONLINE_OK
                : SATURN_ONLINE_ERR_NO_RESPONSE;
}
