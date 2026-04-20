/**
 * saturn_online/transport.h - Byte-stream Transport Abstraction
 *
 * Function-pointer interface for networked play. The default
 * saturn-online implementation wires this to the NetLink UART
 * (see net.h). Games can swap the transport for emulator TCP,
 * file-backed capture, or a future DreamPi direct-socket backend
 * without touching their own protocol logic.
 *
 * Shape borrowed from flickys-flock-netlink/net/net_transport.h,
 * which in turn came from the CUI Platform Abstraction Layer.
 */

#ifndef SATURN_ONLINE_TRANSPORT_H
#define SATURN_ONLINE_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct net_transport {
    /**
     * Check if at least one byte is available to read.
     * Non-blocking.
     */
    bool (*rx_ready)(void* ctx);

    /**
     * Read a single byte. Only call when rx_ready() returns true.
     */
    uint8_t (*rx_byte)(void* ctx);

    /**
     * Send a buffer of bytes.
     * @return number of bytes sent, or -1 on error
     */
    int (*send)(void* ctx, const uint8_t* data, int len);

    /**
     * Check if the transport link is still active.
     * May be NULL if the transport has no notion of connection state.
     */
    bool (*is_connected)(void* ctx);

    /** Opaque context pointer passed to all callbacks. */
    void* ctx;

} net_transport_t;

/*============================================================================
 * Convenience Helpers
 *============================================================================*/

static inline bool net_transport_rx_ready(const net_transport_t* t)
{
    return (t && t->rx_ready) ? t->rx_ready(t->ctx) : false;
}

static inline uint8_t net_transport_rx_byte(const net_transport_t* t)
{
    return (t && t->rx_byte) ? t->rx_byte(t->ctx) : 0;
}

static inline int net_transport_send(const net_transport_t* t,
                                     const uint8_t* data, int len)
{
    return (t && t->send) ? t->send(t->ctx, data, len) : -1;
}

static inline bool net_transport_is_connected(const net_transport_t* t)
{
    if (!t) return false;
    return t->is_connected ? t->is_connected(t->ctx) : true;
}

#endif /* SATURN_ONLINE_TRANSPORT_H */
