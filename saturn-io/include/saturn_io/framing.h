/**
 * saturn_io/framing.h - Generic length-prefixed framing
 *
 * Wire format: [LEN_HI:u8][LEN_LO:u8][payload...]
 *   - LEN is a big-endian uint16 of the payload length
 *   - Max payload length is SATURN_IO_MAX_PAYLOAD (128 bytes)
 *   - No assumptions about the payload contents — the first byte
 *     is NOT reserved. Games carry their own protocol byte(s) in
 *     the payload however they like.
 *
 * The receiver is a byte-at-a-time state machine. Feed UART bytes
 * one by one; when a complete frame lands, saturn_io_frame_feed()
 * returns true and the payload is available at (rx->buf, rx->payload_len).
 *
 * This helper is intentionally minimal. It has no I/O, no allocation,
 * and no dependency on the transport. It's equally usable from
 * saturn_io_net (the UART path), an emulator TCP bridge, or a
 * PC-side decoder.
 */

#ifndef SATURN_IO_FRAMING_H
#define SATURN_IO_FRAMING_H

#include <stdint.h>
#include <stdbool.h>

/** Maximum payload size inside a single frame. */
#define SATURN_IO_MAX_PAYLOAD  128

typedef enum {
    SATURN_IO_FRAME_LEN_HI = 0,   /* Waiting for length high byte */
    SATURN_IO_FRAME_LEN_LO,       /* Waiting for length low byte */
    SATURN_IO_FRAME_PAYLOAD       /* Reading payload bytes */
} saturn_io_frame_state_t;

typedef struct {
    saturn_io_frame_state_t state;
    uint8_t  buf[SATURN_IO_MAX_PAYLOAD];
    uint16_t payload_len;      /* Expected payload length (from header) */
    uint16_t payload_pos;      /* How many payload bytes we've read */
} saturn_io_frame_rx_t;

/**
 * Reset the frame receiver to idle state.
 */
static inline void saturn_io_frame_init(saturn_io_frame_rx_t* rx)
{
    rx->state = SATURN_IO_FRAME_LEN_HI;
    rx->payload_len = 0;
    rx->payload_pos = 0;
}

/**
 * Feed a single byte to the frame receiver.
 *
 * @return true if a complete frame is now available in rx->buf.
 *         The payload length is rx->payload_len. The caller must
 *         consume the payload before feeding the next byte — the
 *         receiver resets itself to idle automatically after a
 *         complete frame is signalled.
 */
static inline bool saturn_io_frame_feed(saturn_io_frame_rx_t* rx,
                                             uint8_t byte)
{
    switch (rx->state) {
    case SATURN_IO_FRAME_LEN_HI:
        rx->payload_len = (uint16_t)byte << 8;
        rx->state = SATURN_IO_FRAME_LEN_LO;
        return false;

    case SATURN_IO_FRAME_LEN_LO:
        rx->payload_len |= byte;
        if (rx->payload_len == 0 ||
            rx->payload_len > SATURN_IO_MAX_PAYLOAD)
        {
            /* Invalid length — drop frame and reset. */
            saturn_io_frame_init(rx);
            return false;
        }
        rx->payload_pos = 0;
        rx->state = SATURN_IO_FRAME_PAYLOAD;
        return false;

    case SATURN_IO_FRAME_PAYLOAD:
        rx->buf[rx->payload_pos++] = byte;
        if (rx->payload_pos >= rx->payload_len) {
            /* Frame complete. Caller will consume; receiver is ready
             * to start the next header on the next byte. Note we do
             * NOT clear rx->buf or payload_len here — the caller is
             * expected to use them before feeding the next byte. */
            rx->state = SATURN_IO_FRAME_LEN_HI;
            return true;
        }
        return false;
    }
    return false;
}

#endif /* SATURN_IO_FRAMING_H */
