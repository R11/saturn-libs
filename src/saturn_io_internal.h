/**
 * saturn_io_internal.h -- shared internal helpers
 *
 * Strictly private to the library's own translation units
 * (src/net.c, src/connect_async.c, src/matchmaking.c). Not part of the
 * public API -- do not install under include/. Callers who reach past
 * the public headers are on their own.
 */

#ifndef SATURN_IO_INTERNAL_H
#define SATURN_IO_INTERNAL_H

#include "saturn_io/net.h"
#include "saturn_io/uart.h"
#include "saturn_io/framing.h"
#include "saturn_io/transport.h"
#include "saturn_io/modem.h"

#include <stdbool.h>
#include <stdint.h>

/*============================================================================
 * Internal type: library singleton state
 *
 * Defined here so src/connect_async.c and src/matchmaking.c can share
 * the same state without duplicating logic. Not for public consumption.
 *============================================================================*/

#define SATURN_IO_RINGBUF_SIZE  512   /* Must be power of 2 */
#define SATURN_IO_RINGBUF_MASK  (SATURN_IO_RINGBUF_SIZE - 1)

/* Hard upper bound on the optional TX ring buffer capacity. */
#define SATURN_IO_TX_BUFFER_MAX 2048

typedef struct {
    volatile uint8_t  buf[SATURN_IO_RINGBUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} saturn_io_ringbuf_t;

typedef struct {
    uint8_t  buf[SATURN_IO_TX_BUFFER_MAX];
    uint32_t cap;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} saturn_io_txbuf_t;

struct saturn_io_net_s {
    bool                        initialized;
    saturn_io_config_t      config;
    saturn_io_state_t       state;
    saturn_io_stats_t       stats;
    saturn_uart16550_t          uart;
    saturn_io_frame_rx_t    rx;
    bool                        uart_found;
    int                         reconnect_attempts;
    saturn_io_ringbuf_t     ringbuf;
    bool                        irq_active;

    uint32_t                    dial_timeout_iters;

    saturn_io_txbuf_t       txbuf;
    bool                        txbuf_enabled;

    uint32_t                    heartbeat_ticks_period;
    uint32_t                    heartbeat_ticks_remaining;
    uint32_t                    heartbeat_rx_watchdog;

    const saturn_io_transport_t* transport;

    char                        last_modem_response[SATURN_IO_MODEM_LINE_MAX];
    int                         last_modem_response_len;
    uint16_t                    probe_baud_divisor;

#if defined(__SATURN__)
    uint32_t                    saved_vector;
#endif
};

extern struct saturn_io_net_s s_net;

/* Helpers shared between translation units. */
void saturn_io_set_state(saturn_io_state_t new_state,
                              saturn_io_status_t status);
void saturn_io_report(const char* msg);

#endif /* SATURN_IO_INTERNAL_H */
