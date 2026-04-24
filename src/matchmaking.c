/**
 * matchmaking.c -- saturn_io_matchmake implementation
 *
 * Owns a whole saturn_io session for the duration of a single
 * dial-post-receive-hangup round-trip. See
 * include/saturn_io/matchmaking.h for the wire protocol.
 */

#include "saturn_io/matchmaking.h"
#include "saturn_io/net.h"
#include "saturn_io/framing.h"
#include "saturn_io/transport.h"
#include "saturn_io_internal.h"

#include <string.h>

/* =========================================================================
 * State carried between the on_frame callback and the caller
 * =========================================================================*/

typedef struct {
    saturn_io_matchmake_result_t* out;
    bool received;
    bool parse_failed;
} matchmake_ctx_t;

static void matchmake_on_frame(const uint8_t* payload, uint16_t len, void* user)
{
    matchmake_ctx_t* ctx = (matchmake_ctx_t*)user;
    if (!ctx || ctx->received) return;

    /* Minimum response: opponent_id(1) + dial_len(1) + extra_len(1). */
    if (len < 3) { ctx->parse_failed = true; return; }

    uint16_t pos = 0;
    uint8_t  opponent_id = payload[pos++];
    uint8_t  dial_len    = payload[pos++];

    if (dial_len >= sizeof(ctx->out->opponent_dial)) {
        ctx->parse_failed = true;
        return;
    }
    if (pos + dial_len + 1 > len) {
        ctx->parse_failed = true;
        return;
    }
    if (dial_len > 0) {
        memcpy(ctx->out->opponent_dial, payload + pos, dial_len);
    }
    ctx->out->opponent_dial[dial_len] = '\0';
    pos += dial_len;

    uint8_t extra_len = payload[pos++];
    if (extra_len > sizeof(ctx->out->match_data)) {
        ctx->parse_failed = true;
        return;
    }
    if (pos + extra_len > len) {
        ctx->parse_failed = true;
        return;
    }
    if (extra_len > 0) {
        memcpy(ctx->out->match_data, payload + pos, extra_len);
    }
    ctx->out->match_data_len = extra_len;
    ctx->out->opponent_id    = opponent_id;
    ctx->received = true;
}

/* =========================================================================
 * Public API
 * =========================================================================*/

saturn_io_status_t saturn_io_matchmake(
    const char* server_dial,
    const saturn_io_matchmake_opts_t* opts,
    saturn_io_matchmake_result_t* result)
{
    if (!opts || !result) return SATURN_IO_ERR_INVALID_CONFIG;

    /* Require either a dial number or a transport override. */
    if (!opts->transport) {
        if (!server_dial || server_dial[0] == '\0'
                || strcmp(server_dial, "0000000") == 0) {
            return SATURN_IO_ERR_INVALID_CONFIG;
        }
    }

    memset(result, 0, sizeof(*result));

    matchmake_ctx_t ctx;
    ctx.out = result;
    ctx.received = false;
    ctx.parse_failed = false;

    uint32_t timeout_secs = opts->timeout_secs ? opts->timeout_secs : 30;

    saturn_io_config_t cfg = SATURN_IO_DEFAULTS;
    cfg.dial_number       = server_dial;
    cfg.mode              = SATURN_IO_MODE_DIAL;
    cfg.dial_timeout_secs = timeout_secs;
    cfg.on_frame          = matchmake_on_frame;
    cfg.user              = &ctx;
    cfg.transport         = opts->transport;
    cfg.advanced.monitor_dcd = false;  /* carry on even if DCD flaps */

    saturn_io_status_t s = saturn_io_init(&cfg);
    if (s != SATURN_IO_OK) return s;

    s = saturn_io_connect();
    if (s != SATURN_IO_OK) {
        saturn_io_shutdown();
        return s;
    }

    /* Build and send the request frame:
     *   [u16 game_id BE][u8 name_len][name_len bytes] */
    uint8_t  req[2 + 1 + 128];
    uint16_t req_len = 0;
    req[req_len++] = (uint8_t)(opts->game_id >> 8);
    req[req_len++] = (uint8_t)(opts->game_id & 0xFF);

    uint8_t name_len = 0;
    if (opts->username) {
        size_t ul = strlen(opts->username);
        if (ul > 128) ul = 128;
        name_len = (uint8_t)ul;
    }
    req[req_len++] = name_len;
    if (name_len) {
        memcpy(req + req_len, opts->username, name_len);
        req_len += name_len;
    }

    s = saturn_io_send(req, req_len);
    if (s != SATURN_IO_OK) {
        saturn_io_disconnect();
        saturn_io_shutdown();
        return s;
    }

    /* Poll until the on_frame callback fires or we time out.
     *
     * We size the budget generously: on Saturn, one poll per frame
     * at 60 Hz means timeout_secs*60 iterations is ~timeout_secs
     * wall-clock seconds. On host the polls are much faster; we add
     * a short busy-wait delay inside the loop so the budget still
     * corresponds to roughly the same wall time. The busy-wait is
     * negligible on Saturn (polls already take ~16 ms of real
     * work), so the two worlds converge.
     *
     * The per-iter busy-wait is calibrated against the same 3M-iter/s
     * reference used elsewhere: ~50k iters ~= 16 ms at Saturn speed
     * and a tractable microsecond on host. Budget is scaled
     * accordingly to cover the full wall-clock window in both
     * environments. */
    uint32_t poll_budget = timeout_secs * 200UL;  /* ~1 poll per 5ms */
    if (poll_budget == 0) poll_budget = 200;

    saturn_io_status_t poll_status = SATURN_IO_OK;
    while (poll_budget-- && !ctx.received && !ctx.parse_failed) {
        poll_status = saturn_io_poll();
        if (poll_status != SATURN_IO_OK) break;
        /* Short busy-wait so the loop doesn't burn the budget in
         * microseconds on a fast host. On Saturn this overhead is
         * dwarfed by poll()'s own register polling. */
        for (volatile uint32_t d = 0; d < 50000UL; d++);
    }

    saturn_io_disconnect();
    saturn_io_shutdown();

    if (ctx.parse_failed) return SATURN_IO_ERR_INIT_FAILED;
    if (!ctx.received)    return SATURN_IO_ERR_TIMEOUT;
    return SATURN_IO_OK;
}
