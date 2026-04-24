/**
 * saturn_io/modem.h - Basic AT modem commands for NetLink
 *
 * Built on saturn_io/uart.h -- sends AT commands and parses responses.
 *
 * Prefix convention (post-Phase-1): all AT-layer identifiers now use
 * `saturn_io_modem_*` / `SATURN_IO_MODEM_*`. The older `modem_*`
 * / `MODEM_*` names are gone. `saturn_uart_*` and `saturn_netlink_smpc_*`
 * keep their hardware-level names.
 *
 * Per-file-scope state (last response, last baud divisor) has been moved
 * out of this header and into `src/net.c`. Callers that need the last
 * modem response from outside net.c should use
 * `saturn_io_last_modem_response()` (in `net.h`).
 */

#ifndef SATURN_IO_MODEM_H
#define SATURN_IO_MODEM_H

#include "uart.h"
#include <string.h>

#define SATURN_IO_MODEM_TIMEOUT       2000000   /* Standard command timeout */
#define SATURN_IO_MODEM_TIMEOUT_LONG  5000000   /* Extended timeout for reset */
#define SATURN_IO_MODEM_LINE_MAX      128
#define SATURN_IO_MODEM_GUARD_TIME    200000    /* Guard time for +++ escape */

#define SATURN_IO_MODEM_BAUD_9600       12        /* Divisor for 9600 baud */
#define SATURN_IO_MODEM_BAUD_19200       6        /* Divisor for 19200 baud */
#define SATURN_IO_MODEM_BAUD_2400       48        /* Divisor for 2400 baud */
#define SATURN_IO_MODEM_BAUD_4800       24        /* Divisor for 4800 baud */
#define SATURN_IO_MODEM_SETTLE_CYCLES   2000000   /* L39 post-init settle (~700ms) */
#define SATURN_IO_MODEM_PROBE_TIMEOUT   3000000   /* AT probe timeout (~1s) */
#define SATURN_IO_MODEM_PROBE_ATTEMPTS  3         /* AT attempts per baud rate */

typedef enum {
    SATURN_IO_MODEM_OK = 0,
    SATURN_IO_MODEM_ERROR,
    SATURN_IO_MODEM_TIMEOUT_ERR,
    SATURN_IO_MODEM_CONNECT,
    SATURN_IO_MODEM_NO_CARRIER,
    SATURN_IO_MODEM_BUSY,
    SATURN_IO_MODEM_NO_DIALTONE,
    SATURN_IO_MODEM_NO_ANSWER,
    SATURN_IO_MODEM_RING,
    SATURN_IO_MODEM_UNKNOWN
} saturn_io_modem_result_t;

/**
 * Capture the last parsed response for diagnostics.
 *
 * Implemented in net.c so the storage is a single translation-unit-scoped
 * variable rather than a static in a header. Declared here so the inline
 * parser can feed it.
 */
void saturn_io_modem_record_response(const char* response, int len);

/**
 * Record the baud divisor that passed the last probe (0 = none).
 */
void saturn_io_modem_record_probe_divisor(uint16_t divisor);

/**
 * Read the baud divisor recorded by the last probe (0 = none).
 */
uint16_t saturn_io_modem_get_probe_divisor(void);

/**
 * Read a line from modem until CR/LF or timeout
 * @return number of characters read, or -1 on timeout
 */
static inline int saturn_io_modem_read_line(const saturn_uart16550_t* uart,
                                   char* buf, int max_len, uint32_t timeout) {
    int idx = 0;
    while (idx < max_len - 1) {
        int c = saturn_uart_getc_timeout(uart, timeout);
        if (c < 0) {
            buf[idx] = '\0';
            return (idx > 0) ? idx : -1;
        }
        if (c == '\r' || c == '\n') {
            if (idx > 0) {  /* Ignore leading CR/LF */
                buf[idx] = '\0';
                return idx;
            }
        } else {
            buf[idx++] = (char)c;
        }
    }
    buf[idx] = '\0';
    return idx;
}

/**
 * Parse modem response string to result code.
 * Supports both text (ATV1) and numeric (ATV0) response modes.
 */
static inline saturn_io_modem_result_t
saturn_io_modem_parse_response(const char* response) {
    int len = (int)strlen(response);
    if (len >= SATURN_IO_MODEM_LINE_MAX) len = SATURN_IO_MODEM_LINE_MAX - 1;
    saturn_io_modem_record_response(response, len);

    /* Text responses (ATV1 mode) */
    if (strstr(response, "OK"))          return SATURN_IO_MODEM_OK;
    if (strstr(response, "ERROR"))       return SATURN_IO_MODEM_ERROR;
    if (strstr(response, "CONNECT"))     return SATURN_IO_MODEM_CONNECT;
    if (strstr(response, "NO CARRIER"))  return SATURN_IO_MODEM_NO_CARRIER;
    if (strstr(response, "BUSY"))        return SATURN_IO_MODEM_BUSY;
    if (strstr(response, "NO DIALTONE")) return SATURN_IO_MODEM_NO_DIALTONE;
    if (strstr(response, "NO ANSWER"))   return SATURN_IO_MODEM_NO_ANSWER;
    if (strstr(response, "RING"))        return SATURN_IO_MODEM_RING;

    /* Numeric responses (ATV0 mode) */
    if (len == 1) {
        switch (response[0]) {
            case '0': return SATURN_IO_MODEM_OK;
            case '1': return SATURN_IO_MODEM_CONNECT;
            case '2': return SATURN_IO_MODEM_RING;
            case '3': return SATURN_IO_MODEM_NO_CARRIER;
            case '4': return SATURN_IO_MODEM_ERROR;
            case '6': return SATURN_IO_MODEM_NO_DIALTONE;
            case '7': return SATURN_IO_MODEM_BUSY;
            case '8': return SATURN_IO_MODEM_NO_ANSWER;
        }
    }

    return SATURN_IO_MODEM_UNKNOWN;
}

/**
 * Flush any pending input from modem.
 */
static inline void saturn_io_modem_flush_input(const saturn_uart16550_t* uart) {
    saturn_uart_flush_rx(uart);
}

/**
 * Send escape sequence to return to command mode.
 */
static inline void saturn_io_modem_escape_to_command(const saturn_uart16550_t* uart) {
    for (volatile uint32_t i = 0; i < SATURN_IO_MODEM_GUARD_TIME; i++);
    saturn_uart_puts(uart, "+++");
    for (volatile uint32_t i = 0; i < SATURN_IO_MODEM_GUARD_TIME; i++);
}

/**
 * Send AT command and wait for response with caller-supplied timeout.
 */
static inline saturn_io_modem_result_t
saturn_io_modem_command_timeout(const saturn_uart16550_t* uart,
                                     const char* cmd,
                                     char* response_buf, int buf_len,
                                     uint32_t timeout) {
    saturn_uart_puts(uart, cmd);
    saturn_uart_puts(uart, "\r");

    while (1) {
        int len = saturn_io_modem_read_line(uart, response_buf, buf_len, timeout);
        if (len < 0) return SATURN_IO_MODEM_TIMEOUT_ERR;
        if (len == 0) continue;

        saturn_io_modem_result_t result =
            saturn_io_modem_parse_response(response_buf);
        if (result != SATURN_IO_MODEM_UNKNOWN) return result;
    }
}

/**
 * Send AT command and wait for response (standard timeout).
 */
static inline saturn_io_modem_result_t
saturn_io_modem_command(const saturn_uart16550_t* uart,
                             const char* cmd,
                             char* response_buf, int buf_len) {
    return saturn_io_modem_command_timeout(uart, cmd, response_buf, buf_len,
                                                SATURN_IO_MODEM_TIMEOUT);
}

/**
 * Initialize modem with standard settings.
 */
static inline saturn_io_modem_result_t
saturn_io_modem_init(const saturn_uart16550_t* uart) {
    char buf[SATURN_IO_MODEM_LINE_MAX];

    if (saturn_io_modem_command(uart, "ATZ", buf, sizeof(buf))
            != SATURN_IO_MODEM_OK)
        return SATURN_IO_MODEM_ERROR;
    if (saturn_io_modem_command(uart, "ATE0", buf, sizeof(buf))
            != SATURN_IO_MODEM_OK)
        return SATURN_IO_MODEM_ERROR;
    if (saturn_io_modem_command(uart, "ATX3", buf, sizeof(buf))
            != SATURN_IO_MODEM_OK)
        return SATURN_IO_MODEM_ERROR;
    if (saturn_io_modem_command(uart, "ATV1", buf, sizeof(buf))
            != SATURN_IO_MODEM_OK)
        return SATURN_IO_MODEM_ERROR;

    return SATURN_IO_MODEM_OK;
}

/**
 * Try a single AT probe at the current UART settings.
 * Sends AT\r and checks for OK (handles echo on first line).
 * Returns SATURN_IO_MODEM_OK on response, SATURN_IO_MODEM_TIMEOUT_ERR otherwise.
 */
static inline saturn_io_modem_result_t
saturn_io_modem_try_at(const saturn_uart16550_t* uart) {
    char buf[SATURN_IO_MODEM_LINE_MAX];
    int len;

    saturn_uart_flush_rx(uart);

    /* Send bare CR first to clear any partial command in the modem's
     * input buffer, then send the actual AT command */
    saturn_uart_puts(uart, "\r");
    for (volatile uint32_t d = 0; d < 50000; d++);
    saturn_uart_flush_rx(uart);
    saturn_uart_puts(uart, "AT\r");

    len = saturn_io_modem_read_line(uart, buf, sizeof(buf),
                                         SATURN_IO_MODEM_PROBE_TIMEOUT);
    if (len < 0) return SATURN_IO_MODEM_TIMEOUT_ERR;

    if (saturn_io_modem_parse_response(buf) == SATURN_IO_MODEM_OK)
        return SATURN_IO_MODEM_OK;

    /* First line might be echo ("AT") -- try second line */
    len = saturn_io_modem_read_line(uart, buf, sizeof(buf),
                                         SATURN_IO_MODEM_PROBE_TIMEOUT);
    if (len < 0) return SATURN_IO_MODEM_TIMEOUT_ERR;

    if (saturn_io_modem_parse_response(buf) == SATURN_IO_MODEM_OK)
        return SATURN_IO_MODEM_OK;

    return SATURN_IO_MODEM_TIMEOUT_ERR;
}

/**
 * Probe modem: try AT command multiple times at multiple baud rates.
 * Encapsulates the full wake-up sequence needed after SMPC power-on.
 *
 * The first AT at each baud rate may be consumed by the L39's auto-baud
 * detector without generating a response, so we retry up to
 * SATURN_IO_MODEM_PROBE_ATTEMPTS times before moving to the next baud.
 *
 * Returns SATURN_IO_MODEM_OK on response, SATURN_IO_MODEM_TIMEOUT_ERR otherwise.
 */
static inline saturn_io_modem_result_t
saturn_io_modem_probe(const saturn_uart16550_t* uart) {
    static const uint16_t baud_rates[] = {
        SATURN_IO_MODEM_BAUD_9600,    /* Most common default */
        SATURN_IO_MODEM_BAUD_19200,   /* Some Japanese modems (HSS-0127) */
        SATURN_IO_MODEM_BAUD_4800,
        SATURN_IO_MODEM_BAUD_2400,
    };
    int num_rates = (int)(sizeof(baud_rates) / sizeof(baud_rates[0]));
    int b, attempt;

    saturn_io_modem_record_probe_divisor(0);

    for (b = 0; b < num_rates; b++) {
        saturn_uart_init(uart, baud_rates[b]);

        /* L39 settle -- controller boots from EEPROM after SMPC power-on */
        for (volatile uint32_t d = 0; d < SATURN_IO_MODEM_SETTLE_CYCLES; d++);

        for (attempt = 0; attempt < SATURN_IO_MODEM_PROBE_ATTEMPTS; attempt++) {
            if (saturn_io_modem_try_at(uart) == SATURN_IO_MODEM_OK) {
                saturn_io_modem_record_probe_divisor(baud_rates[b]);
                return SATURN_IO_MODEM_OK;
            }
        }
    }

    return SATURN_IO_MODEM_TIMEOUT_ERR;
}

/**
 * Try a single AT probe at a specific baud rate (no retries).
 * Useful when the baud is known in advance (pinned via advanced config).
 */
static inline saturn_io_modem_result_t
saturn_io_modem_probe_fixed(const saturn_uart16550_t* uart,
                                 uint16_t baud_divisor) {
    int attempt;

    saturn_io_modem_record_probe_divisor(0);
    if (baud_divisor == 0) return SATURN_IO_MODEM_TIMEOUT_ERR;

    saturn_uart_init(uart, baud_divisor);

    for (volatile uint32_t d = 0; d < SATURN_IO_MODEM_SETTLE_CYCLES; d++);

    for (attempt = 0; attempt < SATURN_IO_MODEM_PROBE_ATTEMPTS; attempt++) {
        if (saturn_io_modem_try_at(uart) == SATURN_IO_MODEM_OK) {
            saturn_io_modem_record_probe_divisor(baud_divisor);
            return SATURN_IO_MODEM_OK;
        }
    }
    return SATURN_IO_MODEM_TIMEOUT_ERR;
}

/**
 * Get the baud rate (as actual rate, not divisor) from the last successful probe.
 * Returns 0 if no probe has succeeded.
 */
static inline uint32_t saturn_io_modem_get_probe_baud(void) {
    uint16_t div = saturn_io_modem_get_probe_divisor();
    if (div == 0) return 0;
    /* Clock = 1.8432 MHz, divisor = clock / (16 * baud) */
    return 1843200UL / (16UL * div);
}

/**
 * Dial a number with caller-supplied timeout.
 * The timeout must be long enough for the modem to connect (~30s typical).
 */
static inline saturn_io_modem_result_t
saturn_io_modem_dial(const saturn_uart16550_t* uart,
                          const char* number,
                          uint32_t timeout) {
    char cmd[64];
    char buf[SATURN_IO_MODEM_LINE_MAX];
    strcpy(cmd, "ATDT");
    strcat(cmd, number);
    return saturn_io_modem_command_timeout(uart, cmd, buf, sizeof(buf), timeout);
}

/**
 * Hang up.
 */
static inline saturn_io_modem_result_t
saturn_io_modem_hangup(const saturn_uart16550_t* uart) {
    char buf[SATURN_IO_MODEM_LINE_MAX];
    saturn_io_modem_escape_to_command(uart);
    return saturn_io_modem_command(uart, "ATH0", buf, sizeof(buf));
}

#endif /* SATURN_IO_MODEM_H */
