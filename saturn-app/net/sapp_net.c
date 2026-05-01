/*
 * libs/saturn-app/net/sapp_net.c — backend dispatch.
 *
 * Holds one global backend pointer (owned by the caller; we just dispatch).
 * No malloc, no per-frame allocation.
 */

#include "sapp_net.h"

#include <string.h>

static sapp_net_backend_t g_be;
static bool               g_installed;

void sapp_net_uninstall(void) { sapp_net_install(NULL); }

void sapp_net_install(const sapp_net_backend_t* be)
{
    if (g_installed && g_be.disconnect) g_be.disconnect(g_be.self);
    if (!be) {
        memset(&g_be, 0, sizeof(g_be));
        g_installed = false;
        return;
    }
    g_be        = *be;
    g_installed = true;
}

const sapp_net_backend_t* sapp_net_active(void)
{
    return g_installed ? &g_be : NULL;
}

bool sapp_net_connect(void) {
    if (!g_installed || !g_be.connect) return false;
    return g_be.connect(g_be.self);
}

void sapp_net_disconnect(void) {
    if (g_installed && g_be.disconnect) g_be.disconnect(g_be.self);
}

sapp_net_status_t sapp_net_status(void) {
    if (!g_installed || !g_be.status) return SAPP_NET_DISCONNECTED;
    return g_be.status(g_be.self);
}

bool sapp_net_send_frame(const uint8_t* body, size_t len) {
    if (!g_installed || !g_be.send_frame) return false;
    return g_be.send_frame(g_be.self, body, len);
}

void sapp_net_poll(void) {
    if (g_installed && g_be.poll) g_be.poll(g_be.self);
}

void sapp_net_set_recv(sapp_net_recv_fn cb, void* user) {
    if (g_installed && g_be.set_recv) g_be.set_recv(g_be.self, cb, user);
}
