/**
 * net.c - saturn-online core implementation
 *
 * Extracted from saturn-tools' saturn/lib/net/saturn_net.c with all
 * SCP (canvas_protocol.h) coupling removed. Incoming UART bytes are
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
 * Interrupt-Driven RX — Ring Buffer
 *
 * Single-producer (ISR writes head) / single-consumer (poll reads tail).
 * Power-of-2 size for fast index masking. No lock needed.
 *
 * At 9600 baud (960 bytes/sec) and 60 fps, ~16 bytes arrive per frame.
 * A 512-byte buffer gives ~32 frames of headroom before overflow.
 *============================================================================*/

#define SATURN_ONLINE_RINGBUF_SIZE  512   /* Must be power of 2 */
#define SATURN_ONLINE_RINGBUF_MASK  (SATURN_ONLINE_RINGBUF_SIZE - 1)

typedef struct {
    volatile uint8_t  buf[SATURN_ONLINE_RINGBUF_SIZE];
    volatile uint16_t head;       /* Next write position (ISR only) */
    volatile uint16_t tail;       /* Next read position (main loop only) */
} saturn_online_ringbuf_t;

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

static inline bool ringbuf_put(saturn_online_ringbuf_t* rb, uint8_t byte) {
    uint16_t next = (rb->head + 1) & SATURN_ONLINE_RINGBUF_MASK;
    if (next == rb->tail) return false; /* full */
    rb->buf[rb->head] = byte;
    rb->head = next;
    return true;
}

static inline uint8_t ringbuf_get(saturn_online_ringbuf_t* rb) {
    uint8_t byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & SATURN_ONLINE_RINGBUF_MASK;
    return byte;
}

/*============================================================================
 * Internal State
 *============================================================================*/

static struct {
    bool                        initialized;
    saturn_online_config_t      config;
    saturn_online_state_t       state;
    saturn_online_stats_t       stats;
    saturn_uart16550_t          uart;
    saturn_online_frame_rx_t    rx;
    bool                        uart_found;
    int                         reconnect_attempts;
    saturn_online_ringbuf_t     ringbuf;
    bool                        irq_active;     /* true when ISR is installed */
#if defined(__SATURN__)
    uint32_t                    saved_vector;   /* Previous ISR at vector 0x5C */
#endif
} s_net;

/*============================================================================
 * Interrupt-Driven RX — Saturn Hardware Setup
 *
 * The NetLink UART fires SCU External Interrupt 12 (vector 0x5C).
 * The ISR drains the UART FIFO into the ring buffer. The main-loop
 * poll() then reads from the ring buffer instead of touching the UART.
 *
 * This entire section compiles to nothing on non-Saturn builds.
 * When use_irq is false, none of these functions are called.
 *============================================================================*/

#if defined(__SATURN__)

/*
 * SCU registers for interrupt control.
 *
 * IMS (Interrupt Mask Set): writing a full 32-bit mask value.
 *   Bit 0  = V-blank IN  ...  Bit 15 = reserved
 *   Bits 16-31 = External Interrupts 0-15
 *   A '1' bit means the interrupt is MASKED (disabled).
 *
 * IST (Interrupt Status): read for pending, write to acknowledge.
 *
 * External Interrupt 12 = bit 28 (16 + 12).
 */
#define SCU_REG_IMS    (*(volatile uint32_t*)0x25FE00A0)
#define SCU_REG_IST    (*(volatile uint32_t*)0x25FE00A4)

#define SCU_EXTINT12_BIT  (1u << 28)

/* UART interrupt vector number */
#define SATURN_ONLINE_IRQ_VECTOR  0x5C

/* IER bit 0: Receive Data Available interrupt */
#define SATURN_UART_IER_RDA  0x01

/**
 * Read the SH-2 Vector Base Register.
 */
static inline uint32_t sh2_get_vbr(void) {
    uint32_t vbr;
    __asm__ volatile("stc vbr, %0" : "=r"(vbr));
    return vbr;
}

/**
 * UART receive ISR.
 *
 * Drains all available bytes from the 16550 FIFO into the ring buffer.
 * Marked interrupt_handler so GCC generates proper SH-2 exception
 * prologue/epilogue (save regs, use RTE instead of RTS).
 */
static void __attribute__((interrupt_handler)) saturn_online_uart_isr(void)
{
    while (saturn_uart_rx_ready(&s_net.uart)) {
        uint8_t byte = (uint8_t)saturn_uart_reg_read(&s_net.uart,
                                                      SATURN_UART_RBR);
        if (!ringbuf_put(&s_net.ringbuf, byte)) {
            s_net.stats.irq_overflows++;
        }
    }

    /* Acknowledge the SCU interrupt */
    SCU_REG_IST = SCU_EXTINT12_BIT;
}

/**
 * Install the UART ISR and enable the interrupt path.
 */
static void irq_setup(void)
{
    uint32_t vbr = sh2_get_vbr();
    volatile uint32_t* vtable = (volatile uint32_t*)vbr;
    uint32_t mask;

    /* Save previous vector for cleanup */
    s_net.saved_vector = vtable[SATURN_ONLINE_IRQ_VECTOR];

    /* Install our ISR */
    vtable[SATURN_ONLINE_IRQ_VECTOR] = (uint32_t)saturn_online_uart_isr;

    /* Reset ring buffer */
    ringbuf_reset(&s_net.ringbuf);

    /* Unmask External Interrupt 12 in SCU (clear bit 28) */
    mask = SCU_REG_IMS;
    mask &= ~SCU_EXTINT12_BIT;
    SCU_REG_IMS = mask;

    /* Enable UART Receive Data Available interrupt */
    saturn_uart_reg_write(&s_net.uart, SATURN_UART_IER, SATURN_UART_IER_RDA);

    s_net.irq_active = true;
}

/**
 * Disable the UART interrupt and restore the previous vector.
 */
static void irq_teardown(void)
{
    uint32_t vbr;
    volatile uint32_t* vtable;
    uint32_t mask;

    if (!s_net.irq_active) return;

    /* Disable UART receive interrupt */
    saturn_uart_reg_write(&s_net.uart, SATURN_UART_IER, 0x00);

    /* Re-mask External Interrupt 12 in SCU (set bit 28) */
    mask = SCU_REG_IMS;
    mask |= SCU_EXTINT12_BIT;
    SCU_REG_IMS = mask;

    /* Restore previous vector */
    vbr = sh2_get_vbr();
    vtable = (volatile uint32_t*)vbr;
    vtable[SATURN_ONLINE_IRQ_VECTOR] = s_net.saved_vector;

    s_net.irq_active = false;
}

#else /* !__SATURN__ */

/* Non-Saturn builds: IRQ is silently unavailable. */
static void irq_setup(void)    { s_net.irq_active = false; }
static void irq_teardown(void) { }

#endif /* __SATURN__ */

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/** Transition to a new state, firing the callback. */
static void set_state(saturn_online_state_t new_state,
                       saturn_online_status_t status)
{
    saturn_online_state_t old = s_net.state;
    if (old == new_state) return;
    s_net.state = new_state;
    if (s_net.config.on_state) {
        s_net.config.on_state(old, new_state, status, s_net.config.user);
    }
}

/** Report a status message to the user. */
static void report(const char* msg)
{
    if (s_net.config.on_status) {
        s_net.config.on_status(msg, s_net.config.user);
    }
}

/**
 * Detect modem hardware by scanning all known UART addresses.
 * Sets s_net.uart on success.
 *
 * Uses a two-pass approach: first with normal SMPC settle time,
 * then with an extended settle for slower modem controllers.
 */
static bool detect_modem(void)
{
    int pass, i;

    for (pass = 0; pass < 2; pass++) {
        if (pass == 0) {
            report("Powering on modem...");
            saturn_netlink_smpc_enable();
            report("Modem powered on");
        } else {
            report("Extended settle...");
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
                report("Modem found");
                return true;
            }

            /* SCR test failed but LSR looks live — try AT probe */
            if (r.lsr != 0xFF && r.lsr != 0x00 &&
                (r.lsr & (SATURN_UART_LSR_THRE | SATURN_UART_LSR_TEMT))) {
                s_net.uart = probe;
                if (modem_probe(&s_net.uart) == MODEM_OK) {
                    s_net.uart_found = true;
                    report("Modem found (AT)");
                    return true;
                }
            }
        }
    }

    return false;
}

/**
 * Probe modem baud rate and initialize AT settings.
 */
static saturn_online_status_t probe_and_init_modem(void)
{
    report("Probing modem...");

    if (modem_probe(&s_net.uart) != MODEM_OK) {
        report("No modem response");
        return SATURN_ONLINE_ERR_NO_RESPONSE;
    }

    s_net.stats.baud_rate = modem_get_probe_baud();
    report("Initializing modem...");

    if (modem_init(&s_net.uart) != MODEM_OK) {
        report("Modem init failed");
        return SATURN_ONLINE_ERR_INIT_FAILED;
    }

    report("Modem ready");
    return SATURN_ONLINE_OK;
}

/**
 * Dial out to the configured number.
 */
static saturn_online_status_t do_dial(void)
{
    modem_result_t result;

    report("Dialing...");
    result = modem_dial(&s_net.uart,
                         s_net.config.dial_number,
                         s_net.config.dial_timeout);

    switch (result) {
    case MODEM_CONNECT:
        modem_flush_input(&s_net.uart);
        return SATURN_ONLINE_OK;
    case MODEM_NO_CARRIER:
        report("No carrier");
        return SATURN_ONLINE_ERR_NO_CARRIER;
    case MODEM_BUSY:
        report("Busy");
        return SATURN_ONLINE_ERR_BUSY;
    case MODEM_NO_DIALTONE:
        report("No dial tone");
        return SATURN_ONLINE_ERR_NO_DIALTONE;
    case MODEM_NO_ANSWER:
        report("No answer");
        return SATURN_ONLINE_ERR_NO_ANSWER;
    case MODEM_TIMEOUT_ERR:
        report("Dial timeout");
        return SATURN_ONLINE_ERR_TIMEOUT;
    default:
        report("Dial failed");
        return SATURN_ONLINE_ERR_NO_CARRIER;
    }
}

/**
 * Wait for an incoming call (answer mode).
 */
static saturn_online_status_t do_answer(void)
{
    char buf[MODEM_LINE_MAX];
    modem_result_t result;

    report("Waiting for call...");

    /* Enable auto-answer */
    result = modem_command_timeout(&s_net.uart, "ATS0=1", buf, sizeof(buf),
                                    s_net.config.dial_timeout);
    if (result != MODEM_OK) {
        report("Auto-answer setup failed");
        return SATURN_ONLINE_ERR_INIT_FAILED;
    }

    /* Wait for CONNECT */
    while (1) {
        int len = modem_read_line(&s_net.uart, buf, sizeof(buf),
                                   s_net.config.dial_timeout);
        if (len < 0) {
            report("Answer timeout");
            return SATURN_ONLINE_ERR_TIMEOUT;
        }
        if (len == 0) continue;

        modem_result_t r = modem_parse_response(buf);
        if (r == MODEM_CONNECT) {
            modem_flush_input(&s_net.uart);
            return SATURN_ONLINE_OK;
        }
        if (r == MODEM_NO_CARRIER || r == MODEM_ERROR) {
            report("Answer failed");
            return SATURN_ONLINE_ERR_NO_CARRIER;
        }
        /* RING or other — keep waiting */
    }
}

/**
 * Flush UART FIFO and reset the frame receiver.
 * Also resets the ring buffer when in IRQ mode.
 */
static void reset_receiver(void)
{
    saturn_uart_reg_write(&s_net.uart, SATURN_UART_FCR,
        SATURN_UART_FCR_ENABLE | SATURN_UART_FCR_RXRESET);
    saturn_online_frame_init(&s_net.rx);
    ringbuf_reset(&s_net.ringbuf);
}

/**
 * Check DCD line to detect carrier loss.
 */
static bool check_carrier(void)
{
    uint8_t msr = saturn_uart_reg_read(&s_net.uart, SATURN_UART_MSR);
    return (msr & SATURN_UART_MSR_DCD) != 0;
}

/**
 * Full reconnect sequence: hangup, re-probe, re-init, re-dial.
 */
static saturn_online_status_t do_reconnect_internal(void)
{
    char buf[MODEM_LINE_MAX];
    saturn_online_status_t status;

    set_state(SATURN_ONLINE_STATE_RECONNECTING, SATURN_ONLINE_ERR_CARRIER_LOST);

    /* Disable IRQ during AT command mode */
    irq_teardown();

    report("Hanging up...");
    modem_command(&s_net.uart, "ATH0", buf, sizeof(buf));

    status = probe_and_init_modem();
    if (status != SATURN_ONLINE_OK) return status;

    report("Re-dialing...");
    if (s_net.config.mode == SATURN_ONLINE_MODE_DIAL) {
        status = do_dial();
    } else {
        status = do_answer();
    }

    if (status != SATURN_ONLINE_OK) return status;

    reset_receiver();

    /* Re-enable IRQ after reconnection */
    if (s_net.config.use_irq) {
        irq_setup();
    }

    s_net.stats.reconnections++;
    report("Reconnected");
    return SATURN_ONLINE_OK;
}

/*============================================================================
 * Public API
 *============================================================================*/

saturn_online_status_t saturn_online_init(const saturn_online_config_t* config)
{
    if (s_net.initialized) {
        return SATURN_ONLINE_ERR_ALREADY_INIT;
    }

    if (!config || !config->on_frame) {
        return SATURN_ONLINE_ERR_INVALID_CONFIG;
    }

    memset(&s_net, 0, sizeof(s_net));
    s_net.config = *config;
    s_net.state = SATURN_ONLINE_STATE_IDLE;
    s_net.initialized = true;

    saturn_online_frame_init(&s_net.rx);

    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_connect(void)
{
    saturn_online_status_t status;

    if (!s_net.initialized) {
        return SATURN_ONLINE_ERR_INVALID_CONFIG;
    }

    /* Phase 1: Detect modem hardware */
    set_state(SATURN_ONLINE_STATE_DETECTING, SATURN_ONLINE_OK);

    if (!s_net.uart_found) {
        if (!detect_modem()) {
            set_state(SATURN_ONLINE_STATE_ERROR, SATURN_ONLINE_ERR_NO_MODEM);
            return SATURN_ONLINE_ERR_NO_MODEM;
        }
    }

    /* Phase 2: Probe baud rate and initialize AT settings */
    set_state(SATURN_ONLINE_STATE_PROBING, SATURN_ONLINE_OK);

    status = probe_and_init_modem();
    if (status != SATURN_ONLINE_OK) {
        set_state(SATURN_ONLINE_STATE_ERROR, status);
        return status;
    }

    /* Phase 3: Establish connection */
    set_state(SATURN_ONLINE_STATE_CONNECTING, SATURN_ONLINE_OK);

    if (s_net.config.mode == SATURN_ONLINE_MODE_DIAL) {
        status = do_dial();
    } else {
        status = do_answer();
    }

    if (status != SATURN_ONLINE_OK) {
        set_state(SATURN_ONLINE_STATE_DISCONNECTED, status);
        return status;
    }

    /* Connected — flush FIFO and enter data mode */
    reset_receiver();
    s_net.reconnect_attempts = 0;

    /* Enable interrupt-driven RX if requested */
    if (s_net.config.use_irq) {
        irq_setup();
        if (s_net.irq_active) {
            report("IRQ receive enabled");
        }
    }

    set_state(SATURN_ONLINE_STATE_CONNECTED, SATURN_ONLINE_OK);
    report("Connected");

    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_poll(void)
{
    int bytes_read = 0;

    if (!s_net.initialized) return SATURN_ONLINE_OK;

    s_net.stats.polls++;

    /* Check connection health */
    if (s_net.state == SATURN_ONLINE_STATE_CONNECTED &&
        s_net.config.monitor_dcd)
    {
        if (!check_carrier()) {
            s_net.stats.carrier_losses++;
            report("Carrier lost");

            if (s_net.config.auto_reconnect) {
                int max = s_net.config.max_reconnect_attempts;
                if (max == 0 || s_net.reconnect_attempts < max) {
                    s_net.reconnect_attempts++;
                    saturn_online_status_t rs = do_reconnect_internal();
                    if (rs == SATURN_ONLINE_OK) {
                        s_net.reconnect_attempts = 0;
                        set_state(SATURN_ONLINE_STATE_CONNECTED,
                                   SATURN_ONLINE_OK);
                        return SATURN_ONLINE_OK;
                    }
                }
                set_state(SATURN_ONLINE_STATE_DISCONNECTED,
                           SATURN_ONLINE_ERR_CARRIER_LOST);
                return SATURN_ONLINE_ERR_CARRIER_LOST;
            }

            set_state(SATURN_ONLINE_STATE_DISCONNECTED,
                       SATURN_ONLINE_ERR_CARRIER_LOST);
            return SATURN_ONLINE_ERR_CARRIER_LOST;
        }
    }

    /* Read bytes and feed to frame receiver.
     *
     * Two paths, same frame processing:
     *   use_irq=true:  ISR already buffered bytes → drain ring buffer
     *   use_irq=false: poll UART registers directly (original behavior)
     */
    if (s_net.state == SATURN_ONLINE_STATE_CONNECTED) {
        int limit = s_net.config.max_bytes_per_poll;

        if (s_net.irq_active) {
            /* IRQ path: read from ring buffer (filled by ISR) */
            while (!ringbuf_empty(&s_net.ringbuf)) {
                uint8_t byte = ringbuf_get(&s_net.ringbuf);
                s_net.stats.bytes_received++;
                bytes_read++;

                if (saturn_online_frame_feed(&s_net.rx, byte)) {
                    s_net.stats.frames_received++;
                    s_net.config.on_frame(s_net.rx.buf,
                                           s_net.rx.payload_len,
                                           s_net.config.user);
                }

                if (limit > 0 && bytes_read >= limit) break;
            }
        } else {
            /* Polling path: read UART directly (default, unchanged) */
            while (saturn_uart_rx_ready(&s_net.uart)) {
                uint8_t byte = (uint8_t)saturn_uart_reg_read(&s_net.uart,
                                                              SATURN_UART_RBR);
                s_net.stats.bytes_received++;
                bytes_read++;

                if (saturn_online_frame_feed(&s_net.rx, byte)) {
                    s_net.stats.frames_received++;
                    s_net.config.on_frame(s_net.rx.buf,
                                           s_net.rx.payload_len,
                                           s_net.config.user);
                }

                if (limit > 0 && bytes_read >= limit) break;
            }
        }

        s_net.stats.connected_frames++;
    }

    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_send(const uint8_t* payload, uint16_t len)
{
    uint16_t i;

    if (s_net.state != SATURN_ONLINE_STATE_CONNECTED) {
        return SATURN_ONLINE_ERR_NOT_CONNECTED;
    }

    if (!payload || len == 0 || len > SATURN_ONLINE_MAX_PAYLOAD) {
        return SATURN_ONLINE_ERR_SEND_FAILED;
    }

    /* Length prefix (big-endian) */
    if (!saturn_uart_putc(&s_net.uart, (uint8_t)(len >> 8))) {
        return SATURN_ONLINE_ERR_SEND_FAILED;
    }
    if (!saturn_uart_putc(&s_net.uart, (uint8_t)(len & 0xFF))) {
        return SATURN_ONLINE_ERR_SEND_FAILED;
    }

    /* Payload */
    for (i = 0; i < len; i++) {
        if (!saturn_uart_putc(&s_net.uart, payload[i])) {
            return SATURN_ONLINE_ERR_SEND_FAILED;
        }
    }

    s_net.stats.bytes_sent += 2 + len;
    s_net.stats.frames_sent++;

    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_send_raw(const uint8_t* data, uint16_t len)
{
    uint16_t i;

    if (s_net.state != SATURN_ONLINE_STATE_CONNECTED) {
        return SATURN_ONLINE_ERR_NOT_CONNECTED;
    }

    for (i = 0; i < len; i++) {
        if (!saturn_uart_putc(&s_net.uart, data[i])) {
            return SATURN_ONLINE_ERR_SEND_FAILED;
        }
    }

    s_net.stats.bytes_sent += len;
    return SATURN_ONLINE_OK;
}

saturn_online_status_t saturn_online_disconnect(void)
{
    char buf[MODEM_LINE_MAX];

    if (!s_net.initialized) return SATURN_ONLINE_OK;

    /* Tear down IRQ before modem escape (avoids ISR firing during AT mode) */
    irq_teardown();

    if (s_net.state == SATURN_ONLINE_STATE_CONNECTED) {
        modem_escape_to_command(&s_net.uart);
        modem_command(&s_net.uart, "ATH0", buf, sizeof(buf));
    }

    set_state(SATURN_ONLINE_STATE_DISCONNECTED, SATURN_ONLINE_OK);
    return SATURN_ONLINE_OK;
}

void saturn_online_shutdown(void)
{
    if (!s_net.initialized) return;

    saturn_online_disconnect();

    /* Power off the modem */
    saturn_netlink_smpc_disable();

    s_net.initialized = false;
    s_net.uart_found = false;
    set_state(SATURN_ONLINE_STATE_IDLE, SATURN_ONLINE_OK);
}

/*============================================================================
 * State & Statistics
 *============================================================================*/

saturn_online_state_t saturn_online_get_state(void)
{
    return s_net.state;
}

bool saturn_online_is_connected(void)
{
    return s_net.state == SATURN_ONLINE_STATE_CONNECTED;
}

saturn_online_stats_t saturn_online_get_stats(void)
{
    return s_net.stats;
}

void saturn_online_reset_stats(void)
{
    uint32_t baud = s_net.stats.baud_rate; /* preserve baud */
    memset(&s_net.stats, 0, sizeof(s_net.stats));
    s_net.stats.baud_rate = baud;
}

const char* saturn_online_state_name(saturn_online_state_t state)
{
    switch (state) {
    case SATURN_ONLINE_STATE_IDLE:          return "Idle";
    case SATURN_ONLINE_STATE_DETECTING:     return "Detecting";
    case SATURN_ONLINE_STATE_PROBING:       return "Probing";
    case SATURN_ONLINE_STATE_INITIALIZING:  return "Initializing";
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
    default:                                return "Unknown error";
    }
}

/*============================================================================
 * Advanced / Low-Level Access
 *============================================================================*/

const void* saturn_online_get_uart(void)
{
    if (!s_net.initialized || !s_net.uart_found) return 0;
    return &s_net.uart;
}

saturn_online_status_t saturn_online_reconnect(void)
{
    if (!s_net.initialized) return SATURN_ONLINE_ERR_INVALID_CONFIG;

    saturn_online_status_t status = do_reconnect_internal();
    if (status == SATURN_ONLINE_OK) {
        s_net.reconnect_attempts = 0;
        set_state(SATURN_ONLINE_STATE_CONNECTED, SATURN_ONLINE_OK);
    } else {
        set_state(SATURN_ONLINE_STATE_DISCONNECTED, status);
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
        return SATURN_ONLINE_ERR_NOT_CONNECTED; /* can't AT in data mode */
    }

    modem_result_t r = modem_command(&s_net.uart, cmd, response, buf_len);
    return (r == MODEM_OK) ? SATURN_ONLINE_OK : SATURN_ONLINE_ERR_NO_RESPONSE;
}
