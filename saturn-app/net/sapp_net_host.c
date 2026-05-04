/*
 * libs/saturn-app/net/sapp_net_host.c — POSIX TCP backend.
 *
 * Thin glue between sapp_net_backend_t and a non-blocking TCP socket. The
 * lobby host shell calls sapp_net_host_install("host:port"); the M4
 * online states then drive HELLO / ROOM_LIST / ROOM_STATE through it.
 *
 * Receive: we run a tiny streaming framer that reassembles
 * [LEN_HI][LEN_LO][BODY] frames out of TCP byte chunks. Once a frame is
 * complete we hand the body to the registered recv callback.
 */

#define _DEFAULT_SOURCE  /* getaddrinfo, struct addrinfo on glibc */

#include "sapp_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* Public install hook for the host shell. Caller passes "host:port" or
 * "host" (port defaults to 7780). Subsequent sapp_net_connect() will dial.
 * Returns 0 on success, -1 on parse failure. */
int sapp_net_host_install(const char* endpoint);

#define MAX_FRAME_BODY 256u

typedef struct {
    char              host[128];
    uint16_t          port;
    int               fd;
    sapp_net_status_t status;
    /* RX framing state. */
    uint8_t  rx_buf[MAX_FRAME_BODY + 4];
    size_t   rx_len;
    sapp_net_recv_fn recv_cb;
    void*    recv_user;
} host_be_t;

static host_be_t g_host;

static void host_close(host_be_t* h)
{
    if (h->fd >= 0) close(h->fd);
    h->fd     = -1;
    h->rx_len = 0;
    h->status = SAPP_NET_DISCONNECTED;
}

static bool set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
    return true;
}

static bool host_connect(void* self)
{
    host_be_t* h = (host_be_t*)self;
    struct addrinfo  hints;
    struct addrinfo* ai;
    char             portstr[8];
    int              rv;

    if (h->fd >= 0) return true;

    snprintf(portstr, sizeof(portstr), "%u", (unsigned)h->port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    rv = getaddrinfo(h->host, portstr, &hints, &ai);
    if (rv != 0 || !ai) return false;

    h->fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (h->fd < 0) { freeaddrinfo(ai); return false; }

    if (connect(h->fd, ai->ai_addr, ai->ai_addrlen) < 0) {
        close(h->fd); h->fd = -1; freeaddrinfo(ai); return false;
    }
    freeaddrinfo(ai);

    {
        int one = 1;
        setsockopt(h->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    if (!set_nonblock(h->fd)) { host_close(h); return false; }

    h->rx_len = 0;
    h->status = SAPP_NET_CONNECTED;
    return true;
}

static void host_disconnect(void* self)
{
    host_close((host_be_t*)self);
}

static sapp_net_status_t host_status(void* self)
{
    return ((host_be_t*)self)->status;
}

static bool send_all(int fd, const uint8_t* p, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, p, n, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct timeval tv = { 0, 50 * 1000 };
                fd_set ws; FD_ZERO(&ws); FD_SET(fd, &ws);
                if (select(fd + 1, NULL, &ws, NULL, &tv) <= 0) return false;
                continue;
            }
            return false;
        }
        p += w; n -= (size_t)w;
    }
    return true;
}

static bool host_send_frame(void* self, const uint8_t* body, size_t len)
{
    host_be_t* h = (host_be_t*)self;
    uint8_t    hdr[2];
    if (h->fd < 0 || h->status != SAPP_NET_CONNECTED) return false;
    if (len > MAX_FRAME_BODY) return false;
    hdr[0] = (uint8_t)((len >> 8) & 0xFF);
    hdr[1] = (uint8_t)(len & 0xFF);
    if (!send_all(h->fd, hdr, 2)) { host_close(h); return false; }
    if (!send_all(h->fd, body, len)) { host_close(h); return false; }
    return true;
}

static void host_poll(void* self)
{
    host_be_t* h = (host_be_t*)self;
    if (h->fd < 0) return;

    /* Pull whatever bytes are ready and feed the framer until we run dry.
     *
     * On the host shell the lobby is driven by stdin lines (no real
     * vsync), so the per-frame poll cadence can outrun TCP loopback
     * delivery if we only ever do a non-blocking recv. We do one
     * non-blocking recv first; if it returns EAGAIN AND the framer
     * hasn't completed any frame yet this poll, we block briefly via
     * select to give the kernel a chance to surface the response. This
     * keeps real-time mode cheap while making test-mode reliable. */
    int got_any_this_poll = 0;
    int blocked_once      = 0;
    for (;;) {
        uint8_t  buf[512];
        ssize_t  r = recv(h->fd, buf, sizeof(buf), 0);
        if (r == 0) { host_close(h); return; }
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!got_any_this_poll && !blocked_once) {
                    /* Block briefly waiting for first byte. */
                    struct timeval tv = { 0, 5 * 1000 };
                    fd_set rs; FD_ZERO(&rs); FD_SET(h->fd, &rs);
                    blocked_once = 1;
                    if (select(h->fd + 1, &rs, NULL, NULL, &tv) > 0) continue;
                }
                break;
            }
            if (errno == EINTR) continue;
            host_close(h); return;
        }
        got_any_this_poll = 1;
        for (ssize_t i = 0; i < r; ++i) {
            if (h->rx_len >= sizeof(h->rx_buf)) {
                /* Overflow — drop the connection rather than scrambling. */
                host_close(h); return;
            }
            h->rx_buf[h->rx_len++] = buf[i];
            /* Try to consume any complete frames in rx_buf. */
            while (h->rx_len >= 2) {
                uint16_t blen = (uint16_t)((h->rx_buf[0] << 8) | h->rx_buf[1]);
                if (blen > MAX_FRAME_BODY) { host_close(h); return; }
                if (h->rx_len < (size_t)(2 + blen)) break;
                if (h->recv_cb) {
                    h->recv_cb(h->rx_buf + 2, blen, h->recv_user);
                }
                /* Shift remaining bytes forward. */
                size_t consumed = 2u + blen;
                size_t left     = h->rx_len - consumed;
                if (left) memmove(h->rx_buf, h->rx_buf + consumed, left);
                h->rx_len = left;
            }
        }
    }
}

static void host_set_recv(void* self, sapp_net_recv_fn cb, void* user)
{
    host_be_t* h = (host_be_t*)self;
    h->recv_cb   = cb;
    h->recv_user = user;
}

int sapp_net_host_install(const char* endpoint)
{
    const char* colon;
    if (!endpoint || !*endpoint) return -1;

    memset(&g_host, 0, sizeof(g_host));
    g_host.fd     = -1;
    g_host.status = SAPP_NET_DISCONNECTED;

    colon = strchr(endpoint, ':');
    if (colon) {
        size_t hl = (size_t)(colon - endpoint);
        if (hl >= sizeof(g_host.host)) return -1;
        memcpy(g_host.host, endpoint, hl);
        g_host.host[hl] = '\0';
        g_host.port = (uint16_t)strtoul(colon + 1, NULL, 10);
    } else {
        snprintf(g_host.host, sizeof(g_host.host), "%s", endpoint);
        g_host.port = 7780;
    }
    if (!g_host.port) g_host.port = 7780;

    static const sapp_net_backend_t be = {
        host_connect,
        host_disconnect,
        host_status,
        host_send_frame,
        host_poll,
        host_set_recv,
        NULL    /* self filled below */
    };
    sapp_net_backend_t copy = be;
    copy.self = &g_host;
    sapp_net_install(&copy);
    return 0;
}
