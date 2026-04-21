/*
 * test_matchmaking.c -- host-side unit test
 *
 * Spins up a TCP connection (the caller supplies a port via argv[1])
 * and exercises the saturn_online_matchmake() round-trip against a
 * mock Python matchmaking server (see tests/test_host_binaries.py).
 *
 * Protocol (mirrors include/saturn_online/matchmaking.h):
 *   REQUEST : [u16 game_id BE][u8 name_len][name_len bytes]
 *   RESPONSE: [u8 opponent_id][u8 dial_len][dial bytes]
 *             [u8 extra_len][extra bytes]
 * Both wrapped in saturn_online's length-prefixed framing.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "saturn_online/net.h"
#include "saturn_online/matchmaking.h"
#include "saturn_online/transport.h"

#include "tcp_transport.h"

static int expect_i(const char* label, int got, int want) {
    int ok = (got == want);
    printf("%-50s : %s (got=%d, want=%d)\n", label,
           ok ? "PASS" : "FAIL", got, want);
    return ok ? 0 : 1;
}

static int expect_str(const char* label, const char* got, const char* want) {
    int ok = (strcmp(got, want) == 0);
    printf("%-50s : %s (got=\"%s\", want=\"%s\")\n", label,
           ok ? "PASS" : "FAIL", got, want);
    return ok ? 0 : 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <tcp_port>\n", argv[0]);
        return 2;
    }
    int port = atoi(argv[1]);

    tcp_transport_ctx_t tc;
    if (tcp_xport_open(&tc, port) != 0) {
        fprintf(stderr, "failed to connect to 127.0.0.1:%d\n", port);
        return 3;
    }

    saturn_online_transport_t transport;
    tcp_xport_fill(&transport, &tc);

    int failures = 0;

    saturn_online_matchmake_opts_t opts = {0};
    opts.game_id       = 0x1337;
    opts.username      = "saturn-player-1";
    opts.timeout_secs  = 5;
    opts.transport     = &transport;

    saturn_online_matchmake_result_t result;
    saturn_online_status_t s = saturn_online_matchmake(
        NULL, /* server_dial ignored when transport supplied */
        &opts, &result);

    failures += expect_i("matchmake returns OK",
                         (int)s, SATURN_ONLINE_OK);
    failures += expect_i("opponent_id == 42",
                         result.opponent_id, 42);
    failures += expect_str("opponent_dial correct",
                           result.opponent_dial, "#123#");
    failures += expect_i("match_data_len == 3",
                         result.match_data_len, 3);
    if (result.match_data_len == 3) {
        failures += expect_i("match_data[0]", result.match_data[0], 'A');
        failures += expect_i("match_data[1]", result.match_data[1], 'B');
        failures += expect_i("match_data[2]", result.match_data[2], 'C');
    }

    tcp_xport_close(&tc);

    if (failures == 0) {
        printf("\nAll checks passed.\n");
        return 0;
    }
    printf("\n%d check(s) failed.\n", failures);
    return 1;
}
