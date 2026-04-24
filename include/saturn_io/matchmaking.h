/**
 * saturn_io/matchmaking.h - simple matchmaking helper
 *
 * Wraps the "dial the matchmaking server, send my game id, get the
 * opponent's dial number back, hang up" pattern that every online
 * Saturn game otherwise re-implements.
 *
 * This is synchronous and blocks until the round-trip completes. It
 * is intended as a pre-game step (main menu -> "find opponent" ->
 * lobby). For in-game matchmaking, games should run their own
 * protocol on top of saturn_io_send/on_frame.
 *
 * Wire protocol
 * -------------
 * All multi-byte fields big-endian.
 *
 * Request frame (Saturn -> matchmaking server):
 *   [u16 game_id][u8 name_len][name_len bytes of username]
 *
 * Response frame (server -> Saturn):
 *   [u8 opponent_id][u8 dial_len][dial_len bytes of opponent dial number]
 *   [u8 extra_len ][extra_len bytes of server-defined match data]
 *
 *   opponent_id is 0 if no match was found, non-zero otherwise.
 *   extra_len may be 0.
 *
 * Both frames are wrapped in saturn_io's length-prefixed framing,
 * so the entire frame appears once on each side via on_frame.
 *
 * This helper owns the connection lifecycle itself: it calls
 * saturn_io_init(), saturn_io_connect(), exchange, and
 * saturn_io_shutdown(). Do not hold a separate saturn_io
 * connection open while calling it.
 */

#ifndef SATURN_IO_MATCHMAKING_H
#define SATURN_IO_MATCHMAKING_H

#include <stdint.h>
#include "saturn_io/net.h"
/* saturn_io_transport_t is forward-declared in net.h; pull in the
 * full struct only when callers actually want the definition. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t     game_id;       /* server-assigned game identifier */
    const char*  username;      /* optional, may be NULL or "" */
    uint32_t     timeout_secs;  /* total budget; 0 defaults to 30 */

    /* Optional transport override. Same semantics as
     * saturn_io_config_t::transport: NULL = real UART dial,
     * non-NULL = skip dial and run the exchange over the supplied
     * transport (useful for emulator-over-TCP, captured-file replay,
     * and host-side tests). */
    const saturn_io_transport_t* transport;
} saturn_io_matchmake_opts_t;

typedef struct {
    char     opponent_dial[32]; /* null-terminated; "" if no match */
    uint8_t  opponent_id;       /* 0 = no match */
    uint8_t  match_data_len;    /* bytes in match_data (0..64) */
    uint8_t  match_data[64];    /* server-defined extras */
} saturn_io_matchmake_result_t;

/**
 * Dial the matchmaking server, post (game_id, username), wait for the
 * opponent response, and hang up.
 *
 * @param server_dial  Phone number to dial for the matchmaking server
 * @param opts         Match options; may not be NULL
 * @param result       Filled on success; opaque on error
 * @return SATURN_IO_OK on success (including "no match" results
 *         -- check result->opponent_id), or an error code.
 *
 * Caller MUST NOT be inside an active saturn_io session when
 * calling this -- the helper owns the session for the round-trip.
 */
saturn_io_status_t saturn_io_matchmake(
    const char* server_dial,
    const saturn_io_matchmake_opts_t* opts,
    saturn_io_matchmake_result_t* result);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_IO_MATCHMAKING_H */
