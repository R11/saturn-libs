/*
 * libs/saturn-app/net/sapp_net — transport vtable for the online states.
 *
 * The online states (CONNECTING, LOBBY_LIST, ROOM_CREATE, IN_ROOM) push
 * frames out through this interface and pull frames in from it. The host
 * shell installs a TCP backend (sapp_net_host); the Saturn shell will
 * install a NetLink backend (M7); tests install a synthetic backend.
 *
 * Send semantics:
 *   sapp_net_send_frame(payload, len) prepends the [LEN_HI][LEN_LO]
 *   length prefix and ships the resulting frame on the transport. Returns
 *   true on success, false on backpressure / disconnect.
 *
 * Receive semantics:
 *   sapp_net_poll() pumps the transport once. Newly assembled inbound
 *   frame bodies are delivered to a single registered callback. The
 *   framework calls poll() once per frame from the online state inputs.
 */

#ifndef SATURN_APP_NET_H
#define SATURN_APP_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SAPP_NET_DISCONNECTED = 0,
    SAPP_NET_CONNECTING   = 1,
    SAPP_NET_CONNECTED    = 2,
    SAPP_NET_ERROR        = 3
} sapp_net_status_t;

typedef void (*sapp_net_recv_fn)(const uint8_t* body, size_t len, void* user);

typedef struct sapp_net_backend {
    /* Begin connecting to the configured endpoint. Idempotent: if a
     * connection is already up this is a no-op. Returns true on success
     * (transition to CONNECTING/CONNECTED), false on hard failure. */
    bool              (*connect)   (void* self);
    void              (*disconnect)(void* self);
    sapp_net_status_t (*status)    (void* self);
    /* Send a single frame body. Backend prepends the framing length. */
    bool              (*send_frame)(void* self,
                                    const uint8_t* body, size_t len);
    /* Pump receive. Calls the registered recv callback for each new frame. */
    void              (*poll)      (void* self);
    void              (*set_recv)  (void* self,
                                    sapp_net_recv_fn cb, void* user);
    void*             self;
} sapp_net_backend_t;

/* Install / remove the active backend. Passing NULL clears it.
 * Re-installing while connected disconnects the previous one. */
void sapp_net_install(const sapp_net_backend_t* be);
void sapp_net_uninstall(void);
const sapp_net_backend_t* sapp_net_active(void);

/* Convenience wrappers used by the online states. Each is a no-op if
 * no backend is installed. */
bool              sapp_net_connect    (void);
void              sapp_net_disconnect (void);
sapp_net_status_t sapp_net_status     (void);
bool              sapp_net_send_frame (const uint8_t* body, size_t len);
void              sapp_net_poll       (void);
void              sapp_net_set_recv   (sapp_net_recv_fn cb, void* user);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_APP_NET_H */
