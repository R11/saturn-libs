/*
 * tcp_transport.h -- BSD-socket-backed saturn_io transport for
 * host-side tests.
 *
 * This is test infrastructure only. It is NOT part of the library.
 * Do not ship or reuse on the Saturn side.
 */

#ifndef SATURN_IO_TEST_TCP_TRANSPORT_H
#define SATURN_IO_TEST_TCP_TRANSPORT_H

#ifdef __SATURN__
#error "tcp_transport.h is host-test-only -- do not include on Saturn"
#endif

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "saturn_io/transport.h"

typedef struct {
    int fd;
} tcp_transport_ctx_t;

static bool tcp_xport_rx_ready(void* c) {
    tcp_transport_ctx_t* ctx = (tcp_transport_ctx_t*)c;
    fd_set fds; FD_ZERO(&fds); FD_SET(ctx->fd, &fds);
    struct timeval tv = { 0, 0 };
    int n = select(ctx->fd + 1, &fds, 0, 0, &tv);
    return n > 0 && FD_ISSET(ctx->fd, &fds);
}

static uint8_t tcp_xport_rx_byte(void* c) {
    tcp_transport_ctx_t* ctx = (tcp_transport_ctx_t*)c;
    uint8_t b = 0;
    recv(ctx->fd, &b, 1, 0);
    return b;
}

static int tcp_xport_send(void* c, const uint8_t* d, int l) {
    tcp_transport_ctx_t* ctx = (tcp_transport_ctx_t*)c;
    int sent = send(ctx->fd, d, l, 0);
    return sent;
}

static bool tcp_xport_is_connected(void* c) {
    (void)c;
    return true;
}

static int tcp_xport_open(tcp_transport_ctx_t* ctx, int port) {
    ctx->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = htonl(0x7F000001); /* 127.0.0.1 */

    if (connect(ctx->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(ctx->fd);
        ctx->fd = -1;
        return -1;
    }
    return 0;
}

static void tcp_xport_close(tcp_transport_ctx_t* ctx) {
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

static void tcp_xport_fill(saturn_io_transport_t* t, tcp_transport_ctx_t* ctx) {
    t->rx_ready     = tcp_xport_rx_ready;
    t->rx_byte      = tcp_xport_rx_byte;
    t->send         = tcp_xport_send;
    t->is_connected = tcp_xport_is_connected;
    t->ctx          = ctx;
}

#endif /* SATURN_IO_TEST_TCP_TRANSPORT_H */
