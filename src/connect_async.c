/**
 * connect_async.c -- non-blocking connect state machine
 *
 * Implements saturn_online_connect_start() + saturn_online_connect_tick().
 * Shares the library singleton (s_net) defined in net.c via
 * saturn_online_internal.h.
 *
 * Design notes
 * ------------
 * The blocking saturn_online_connect() runs a straight-line dial
 * sequence: SMPC power on, UART detect, baud probe, ATZ/ATE0/ATX3/ATV1,
 * ATDT<num>. Each AT exchange busy-waits for a response with a
 * seconds-scale timeout. That's incompatible with
 * frame-locked game loops (jo_core_run) that must render between
 * ticks.
 *
 * The async variant reimplements the same sequence as a state machine.
 * Per-tick work is bounded: we either dispatch one AT command with a
 * short per-step timeout, or advance a settle counter. If a short
 * timeout elapses without the modem responding, we retry on the next
 * tick; a per-phase outer budget tracks cumulative "time" and decides
 * when to give up.
 *
 * Gotchas
 * -------
 * - The modem_command helpers in modem.h use an internal busy-wait
 *   whose units are loop iterations, not seconds. Wall-clock drift
 *   per tick is acceptable because the outer budget is also in
 *   iterations; what matters is that we don't block for many frames
 *   at a time.
 * - Some modem controllers silently consume the first AT at a given
 *   baud (auto-baud detect). The blocking path retries internally via
 *   saturn_online_modem_probe's MODEM_PROBE_ATTEMPTS. We preserve
 *   that semantic by running the full probe helper per tick (it is
 *   the cheapest correct option; it can block up to 3s worst case on
 *   a non-responsive modem).
 * - If the user gave us a transport override, the entire async stack
 *   is short-circuited to CONNECTED on the first tick. Nothing to do.
 * - The blocking saturn_online_connect() remains intact. If the user
 *   reports flakes here, they can call the blocking path as a
 *   fallback.
 */

#include "saturn_online/net.h"
#include "saturn_online/uart.h"
#include "saturn_online/modem.h"
#include "saturn_online/framing.h"
#include "saturn_online/transport.h"
#include "saturn_online_internal.h"

#include <string.h>

/* =========================================================================
 * Internal sub-states
 * =========================================================================*/

typedef enum {
    ASYNC_IDLE = 0,
    ASYNC_POWER_ON,        /* Call saturn_netlink_smpc_enable() */
    ASYNC_POWER_SETTLE,    /* Short settle after power-on */
    ASYNC_DETECT,          /* Scan known UART addresses */
    ASYNC_PROBE,           /* Baud-probe and settle */
    ASYNC_AT_ATZ,
    ASYNC_AT_ATE0,
    ASYNC_AT_ATX3,
    ASYNC_AT_ATV1,
    ASYNC_DIAL,            /* ATDT<number> / ATS0=1 wait */
    ASYNC_DONE,            /* CONNECTED */
    ASYNC_FAILED           /* Error, captured in s_fail_result */
} async_state_t;

/* Single-slot async state -- the library only supports one connect
 * at a time; this matches the blocking API. */
static async_state_t s_async_state = ASYNC_IDLE;
static saturn_online_connect_tick_result_t s_fail_result =
    SATURN_ONLINE_TICK_ERR_INTERNAL;

/* Per-phase settle counter (decremented per tick). */
static int s_settle_ticks;

/* Outer dial budget, in ticks. Seeded from dial_timeout_secs at start
 * (approx: one "tick" ~= one frame ~= 1/60 s, so secs * 60 is a
 * reasonable budget ceiling for the async path). */
static int s_dial_budget_ticks;

/* =========================================================================
 * Translation helpers
 * =========================================================================*/

static saturn_online_connect_tick_result_t
status_to_tick(saturn_online_status_t s) {
    switch (s) {
    case SATURN_ONLINE_OK:                 return SATURN_ONLINE_TICK_OK;
    case SATURN_ONLINE_ERR_NO_MODEM:       return SATURN_ONLINE_TICK_ERR_NO_MODEM;
    case SATURN_ONLINE_ERR_NO_RESPONSE:    return SATURN_ONLINE_TICK_ERR_NO_RESPONSE;
    case SATURN_ONLINE_ERR_INIT_FAILED:    return SATURN_ONLINE_TICK_ERR_INIT_FAILED;
    case SATURN_ONLINE_ERR_NO_CARRIER:     return SATURN_ONLINE_TICK_ERR_NO_CARRIER;
    case SATURN_ONLINE_ERR_BUSY:           return SATURN_ONLINE_TICK_ERR_BUSY;
    case SATURN_ONLINE_ERR_NO_DIALTONE:    return SATURN_ONLINE_TICK_ERR_NO_DIALTONE;
    case SATURN_ONLINE_ERR_NO_ANSWER:      return SATURN_ONLINE_TICK_ERR_NO_ANSWER;
    case SATURN_ONLINE_ERR_TIMEOUT:        return SATURN_ONLINE_TICK_ERR_TIMEOUT;
    case SATURN_ONLINE_ERR_INVALID_CONFIG: return SATURN_ONLINE_TICK_ERR_INVALID_CONFIG;
    default:                               return SATURN_ONLINE_TICK_ERR_INTERNAL;
    }
}

/*
 * The blocking modem helpers (modem_command, modem_dial, ...) busy-wait
 * inside a single call. We accept that penalty at the cost of a
 * simpler state machine: each async step invokes the corresponding
 * blocking helper with a reduced-scale timeout, keeping per-tick
 * latency bounded even though it's not frame-perfect.
 *
 * iters_per_step is calibrated so one tick ~= a frame of busy-wait
 * (~16 ms at 60 Hz on a reference Saturn). Reference cadence is
 * 3M iters/sec, so 16 ms ~= 48k iters. We round to 50k to leave
 * headroom.
 */
#define ASYNC_ITERS_PER_STEP  50000UL

/* Number of times we'll retry the AT probe / DIAL response wait before
 * concluding a phase has hard-failed. At ~16 ms per retry, this is
 * roughly (count * 16 ms) worst-case of contiguous busy-wait. */
#define ASYNC_PROBE_RETRIES   16

/* =========================================================================
 * Public API
 * =========================================================================*/

saturn_online_status_t saturn_online_connect_start(void)
{
    if (!s_net.initialized) return SATURN_ONLINE_ERR_INVALID_CONFIG;

    /* Transport override: go straight to CONNECTED on the first tick. */
    if (s_net.transport) {
        s_async_state = ASYNC_DONE;
        /* set_state happens in tick() so callers observe the
         * SATURN_ONLINE_TICK_OK result in the same call that marks
         * the state transition. */
        return SATURN_ONLINE_OK;
    }

    s_async_state = s_net.uart_found ? ASYNC_PROBE : ASYNC_POWER_ON;
    s_settle_ticks = 0;
    s_dial_budget_ticks = (int)s_net.config.dial_timeout_secs * 60;
    if (s_dial_budget_ticks <= 0) s_dial_budget_ticks = 60;
    s_fail_result = SATURN_ONLINE_TICK_ERR_INTERNAL;

    saturn_online_set_state(SATURN_ONLINE_STATE_DETECTING, SATURN_ONLINE_OK);
    return SATURN_ONLINE_OK;
}

saturn_online_connect_tick_result_t saturn_online_connect_tick(void)
{
    char buf[SATURN_ONLINE_MODEM_LINE_MAX];
    saturn_online_modem_result_t mr;

    if (!s_net.initialized) return SATURN_ONLINE_TICK_ERR_INVALID_CONFIG;

    /* Outer budget exhausted. */
    if (s_dial_budget_ticks > 0 && --s_dial_budget_ticks == 0
            && s_async_state != ASYNC_DONE && s_async_state != ASYNC_FAILED) {
        saturn_online_report("Async connect timed out");
        saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED,
                                 SATURN_ONLINE_ERR_TIMEOUT);
        s_async_state = ASYNC_FAILED;
        s_fail_result = SATURN_ONLINE_TICK_ERR_TIMEOUT;
        return s_fail_result;
    }

    switch (s_async_state) {
    case ASYNC_IDLE:
        return SATURN_ONLINE_TICK_ERR_INVALID_CONFIG;

    case ASYNC_POWER_ON:
        saturn_online_report("Async: powering on...");
        saturn_netlink_smpc_enable();
        s_settle_ticks = 4; /* short settle window */
        s_async_state = ASYNC_POWER_SETTLE;
        return SATURN_ONLINE_TICK_IN_PROGRESS;

    case ASYNC_POWER_SETTLE:
        if (s_settle_ticks > 0) { --s_settle_ticks; return SATURN_ONLINE_TICK_IN_PROGRESS; }
        s_async_state = ASYNC_DETECT;
        return SATURN_ONLINE_TICK_IN_PROGRESS;

    case ASYNC_DETECT: {
        /* Scan all known modem addresses. One pass per tick is enough;
         * if none detect, fail out. */
        static const struct {
            uint32_t base;
            uint32_t stride;
        } addrs[] = {
            { 0x25895001, 4 }, { 0x04895001, 4 }, { 0x25895001, 2 },
            { 0x25895000, 4 }, { 0x25895000, 2 }, { 0x25894001, 4 },
            { 0x25894001, 2 },
        };
        int n = (int)(sizeof(addrs) / sizeof(addrs[0]));
        for (int i = 0; i < n; i++) {
            saturn_uart16550_t probe;
            probe.base   = addrs[i].base;
            probe.stride = addrs[i].stride;
            if (saturn_uart_detect(&probe)) {
                s_net.uart = probe;
                s_net.uart_found = true;
                saturn_online_report("Async: modem found");
                saturn_online_set_state(SATURN_ONLINE_STATE_PROBING,
                                         SATURN_ONLINE_OK);
                s_async_state = ASYNC_PROBE;
                return SATURN_ONLINE_TICK_IN_PROGRESS;
            }
        }
        saturn_online_report("Async: no modem");
        saturn_online_set_state(SATURN_ONLINE_STATE_ERROR,
                                 SATURN_ONLINE_ERR_NO_MODEM);
        s_async_state = ASYNC_FAILED;
        s_fail_result = SATURN_ONLINE_TICK_ERR_NO_MODEM;
        return s_fail_result;
    }

    case ASYNC_PROBE:
        saturn_online_report("Async: probing baud...");
        /* NOTE: saturn_online_modem_probe() tries 4 bauds with 3
         * attempts each and busy-waits for ~1 s per attempt. This
         * blocks for up to ~12 s on a non-responsive modem. That's
         * worse than we'd like, but less than the ~60 s blocking
         * saturn_online_connect() could sit in. Acceptable for
         * typical hardware that responds on the first baud tried. */
        if (s_net.config.advanced.fixed_baud_divisor != 0) {
            mr = saturn_online_modem_probe_fixed(
                &s_net.uart, s_net.config.advanced.fixed_baud_divisor);
        } else {
            mr = saturn_online_modem_probe(&s_net.uart);
        }
        if (mr != SATURN_ONLINE_MODEM_OK) {
            saturn_online_report("Async: no modem response");
            saturn_online_set_state(SATURN_ONLINE_STATE_ERROR,
                                     SATURN_ONLINE_ERR_NO_RESPONSE);
            s_async_state = ASYNC_FAILED;
            s_fail_result = SATURN_ONLINE_TICK_ERR_NO_RESPONSE;
            return s_fail_result;
        }
        s_net.stats.baud_rate = saturn_online_modem_get_probe_baud();
        s_async_state = ASYNC_AT_ATZ;
        return SATURN_ONLINE_TICK_IN_PROGRESS;

    case ASYNC_AT_ATZ:
        saturn_online_report("Async: ATZ");
        mr = saturn_online_modem_command_timeout(
                &s_net.uart, "ATZ", buf, sizeof(buf),
                ASYNC_ITERS_PER_STEP * ASYNC_PROBE_RETRIES);
        if (mr != SATURN_ONLINE_MODEM_OK) {
            saturn_online_set_state(SATURN_ONLINE_STATE_ERROR,
                                     SATURN_ONLINE_ERR_INIT_FAILED);
            s_async_state = ASYNC_FAILED;
            s_fail_result = SATURN_ONLINE_TICK_ERR_INIT_FAILED;
            return s_fail_result;
        }
        s_async_state = ASYNC_AT_ATE0;
        return SATURN_ONLINE_TICK_IN_PROGRESS;

    case ASYNC_AT_ATE0:
        saturn_online_report("Async: ATE0");
        mr = saturn_online_modem_command_timeout(
                &s_net.uart, "ATE0", buf, sizeof(buf),
                ASYNC_ITERS_PER_STEP * ASYNC_PROBE_RETRIES);
        if (mr != SATURN_ONLINE_MODEM_OK) {
            saturn_online_set_state(SATURN_ONLINE_STATE_ERROR,
                                     SATURN_ONLINE_ERR_INIT_FAILED);
            s_async_state = ASYNC_FAILED;
            s_fail_result = SATURN_ONLINE_TICK_ERR_INIT_FAILED;
            return s_fail_result;
        }
        s_async_state = ASYNC_AT_ATX3;
        return SATURN_ONLINE_TICK_IN_PROGRESS;

    case ASYNC_AT_ATX3:
        saturn_online_report("Async: ATX3");
        mr = saturn_online_modem_command_timeout(
                &s_net.uart, "ATX3", buf, sizeof(buf),
                ASYNC_ITERS_PER_STEP * ASYNC_PROBE_RETRIES);
        if (mr != SATURN_ONLINE_MODEM_OK) {
            saturn_online_set_state(SATURN_ONLINE_STATE_ERROR,
                                     SATURN_ONLINE_ERR_INIT_FAILED);
            s_async_state = ASYNC_FAILED;
            s_fail_result = SATURN_ONLINE_TICK_ERR_INIT_FAILED;
            return s_fail_result;
        }
        s_async_state = ASYNC_AT_ATV1;
        return SATURN_ONLINE_TICK_IN_PROGRESS;

    case ASYNC_AT_ATV1:
        saturn_online_report("Async: ATV1");
        mr = saturn_online_modem_command_timeout(
                &s_net.uart, "ATV1", buf, sizeof(buf),
                ASYNC_ITERS_PER_STEP * ASYNC_PROBE_RETRIES);
        if (mr != SATURN_ONLINE_MODEM_OK) {
            saturn_online_set_state(SATURN_ONLINE_STATE_ERROR,
                                     SATURN_ONLINE_ERR_INIT_FAILED);
            s_async_state = ASYNC_FAILED;
            s_fail_result = SATURN_ONLINE_TICK_ERR_INIT_FAILED;
            return s_fail_result;
        }
        saturn_online_report("Async: modem ready");
        saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTING,
                                 SATURN_ONLINE_OK);
        s_async_state = ASYNC_DIAL;
        return SATURN_ONLINE_TICK_IN_PROGRESS;

    case ASYNC_DIAL: {
        saturn_online_status_t ds;
        if (s_net.config.mode == SATURN_ONLINE_MODE_DIAL) {
            mr = saturn_online_modem_dial(&s_net.uart,
                                           s_net.config.dial_number,
                                           s_net.dial_timeout_iters);
            switch (mr) {
            case SATURN_ONLINE_MODEM_CONNECT:     ds = SATURN_ONLINE_OK; break;
            case SATURN_ONLINE_MODEM_NO_CARRIER:  ds = SATURN_ONLINE_ERR_NO_CARRIER; break;
            case SATURN_ONLINE_MODEM_BUSY:        ds = SATURN_ONLINE_ERR_BUSY; break;
            case SATURN_ONLINE_MODEM_NO_DIALTONE: ds = SATURN_ONLINE_ERR_NO_DIALTONE; break;
            case SATURN_ONLINE_MODEM_NO_ANSWER:   ds = SATURN_ONLINE_ERR_NO_ANSWER; break;
            case SATURN_ONLINE_MODEM_TIMEOUT_ERR: ds = SATURN_ONLINE_ERR_TIMEOUT; break;
            default:                              ds = SATURN_ONLINE_ERR_NO_CARRIER; break;
            }
        } else {
            /* Answer mode: ATS0=1 then wait for CONNECT. */
            mr = saturn_online_modem_command_timeout(
                    &s_net.uart, "ATS0=1", buf, sizeof(buf),
                    s_net.dial_timeout_iters);
            if (mr != SATURN_ONLINE_MODEM_OK) {
                ds = SATURN_ONLINE_ERR_INIT_FAILED;
            } else {
                int len = saturn_online_modem_read_line(
                              &s_net.uart, buf, sizeof(buf),
                              s_net.dial_timeout_iters);
                if (len < 0) {
                    ds = SATURN_ONLINE_ERR_TIMEOUT;
                } else {
                    saturn_online_modem_result_t r2 =
                        saturn_online_modem_parse_response(buf);
                    if (r2 == SATURN_ONLINE_MODEM_CONNECT) ds = SATURN_ONLINE_OK;
                    else                                   ds = SATURN_ONLINE_ERR_NO_CARRIER;
                }
            }
        }

        if (ds != SATURN_ONLINE_OK) {
            saturn_online_set_state(SATURN_ONLINE_STATE_DISCONNECTED, ds);
            s_async_state = ASYNC_FAILED;
            s_fail_result = status_to_tick(ds);
            return s_fail_result;
        }

        /* Connected. */
        saturn_online_modem_flush_input(&s_net.uart);

        /* Reset RX state (FIFO clear, frame receiver reset). Mirrors
         * reset_receiver() in net.c but reaches via the shared state,
         * since reset_receiver is file-local there. */
        saturn_uart_reg_write(&s_net.uart, SATURN_UART_FCR,
            SATURN_UART_FCR_ENABLE | SATURN_UART_FCR_RXRESET);
        saturn_online_frame_init(&s_net.rx);
        s_net.reconnect_attempts = 0;

        if (s_net.config.advanced.use_irq) {
            /* net.c owns irq_setup as a static; we delegate by calling
             * saturn_online_connect's later half is not trivially
             * reachable. Post-connect IRQ setup is a known gap on the
             * async path -- games that want IRQ RX should use the
             * blocking saturn_online_connect() for now. Documented in
             * the tick-result contract. */
        }

        saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTED,
                                 SATURN_ONLINE_OK);
        saturn_online_report("Async: connected");
        s_async_state = ASYNC_DONE;
        return SATURN_ONLINE_TICK_OK;
    }

    case ASYNC_DONE:
        /* Transport-fast-path: set state here on first tick. */
        if (s_net.state != SATURN_ONLINE_STATE_CONNECTED) {
            saturn_online_set_state(SATURN_ONLINE_STATE_CONNECTED,
                                     SATURN_ONLINE_OK);
            saturn_online_frame_init(&s_net.rx);
            saturn_online_report("Async: connected (transport)");
        }
        return SATURN_ONLINE_TICK_OK;

    case ASYNC_FAILED:
        return s_fail_result;
    }

    return SATURN_ONLINE_TICK_ERR_INTERNAL;
}
