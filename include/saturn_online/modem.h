/**
 * saturn_online/modem.h - Basic AT modem commands for NetLink
 *
 * Built on saturn_online/uart.h — sends AT commands and parses responses.
 */

#ifndef SATURN_ONLINE_MODEM_H
#define SATURN_ONLINE_MODEM_H

#include "uart.h"
#include <string.h>

#define MODEM_TIMEOUT       2000000   /* Standard command timeout */
#define MODEM_TIMEOUT_LONG  5000000   /* Extended timeout for reset */
#define MODEM_LINE_MAX      128
#define MODEM_GUARD_TIME    200000    /* Guard time for +++ escape */

#define MODEM_BAUD_9600       12        /* Divisor for 9600 baud */
#define MODEM_BAUD_19200       6        /* Divisor for 19200 baud */
#define MODEM_BAUD_2400       48        /* Divisor for 2400 baud */
#define MODEM_BAUD_4800       24        /* Divisor for 4800 baud */
#define MODEM_SETTLE_CYCLES   2000000   /* L39 post-init settle (~700ms) */
#define MODEM_PROBE_TIMEOUT   3000000   /* AT probe timeout (~1s) */
#define MODEM_PROBE_ATTEMPTS  3         /* AT attempts per baud rate */

typedef enum {
    MODEM_OK = 0,
    MODEM_ERROR,
    MODEM_TIMEOUT_ERR,
    MODEM_CONNECT,
    MODEM_NO_CARRIER,
    MODEM_BUSY,
    MODEM_NO_DIALTONE,
    MODEM_NO_ANSWER,
    MODEM_RING,
    MODEM_UNKNOWN
} modem_result_t;

/* Last received response for debugging */
static char modem_last_response[MODEM_LINE_MAX];
static int modem_last_response_len;

/* Baud rate divisor that succeeded during last probe (0 = none) */
static uint16_t modem_probe_baud_divisor;

/**
 * Read a line from modem until CR/LF or timeout
 * @return number of characters read, or -1 on timeout
 */
static inline int modem_read_line(const saturn_uart16550_t* uart,
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
static inline modem_result_t modem_parse_response(const char* response) {
    int len = (int)strlen(response);
    if (len >= MODEM_LINE_MAX) len = MODEM_LINE_MAX - 1;
    memcpy(modem_last_response, response, len);
    modem_last_response[len] = '\0';
    modem_last_response_len = len;

    /* Text responses (ATV1 mode) */
    if (strstr(response, "OK"))          return MODEM_OK;
    if (strstr(response, "ERROR"))       return MODEM_ERROR;
    if (strstr(response, "CONNECT"))     return MODEM_CONNECT;
    if (strstr(response, "NO CARRIER"))  return MODEM_NO_CARRIER;
    if (strstr(response, "BUSY"))        return MODEM_BUSY;
    if (strstr(response, "NO DIALTONE")) return MODEM_NO_DIALTONE;
    if (strstr(response, "NO ANSWER"))   return MODEM_NO_ANSWER;
    if (strstr(response, "RING"))        return MODEM_RING;

    /* Numeric responses (ATV0 mode) */
    if (len == 1) {
        switch (response[0]) {
            case '0': return MODEM_OK;
            case '1': return MODEM_CONNECT;
            case '2': return MODEM_RING;
            case '3': return MODEM_NO_CARRIER;
            case '4': return MODEM_ERROR;
            case '6': return MODEM_NO_DIALTONE;
            case '7': return MODEM_BUSY;
            case '8': return MODEM_NO_ANSWER;
        }
    }

    return MODEM_UNKNOWN;
}

/**
 * Get last response string for debugging.
 */
static inline const char* modem_get_last_response(void) {
    return modem_last_response;
}

/**
 * Flush any pending input from modem.
 */
static inline void modem_flush_input(const saturn_uart16550_t* uart) {
    saturn_uart_flush_rx(uart);
}

/**
 * Send escape sequence to return to command mode.
 */
static inline void modem_escape_to_command(const saturn_uart16550_t* uart) {
    for (volatile uint32_t i = 0; i < MODEM_GUARD_TIME; i++);
    saturn_uart_puts(uart, "+++");
    for (volatile uint32_t i = 0; i < MODEM_GUARD_TIME; i++);
}

/**
 * Send AT command and wait for response with caller-supplied timeout.
 */
static inline modem_result_t modem_command_timeout(const saturn_uart16550_t* uart,
                                                    const char* cmd,
                                                    char* response_buf, int buf_len,
                                                    uint32_t timeout) {
    saturn_uart_puts(uart, cmd);
    saturn_uart_puts(uart, "\r");

    while (1) {
        int len = modem_read_line(uart, response_buf, buf_len, timeout);
        if (len < 0) return MODEM_TIMEOUT_ERR;
        if (len == 0) continue;

        modem_result_t result = modem_parse_response(response_buf);
        if (result != MODEM_UNKNOWN) return result;
    }
}

/**
 * Send AT command and wait for response (standard timeout).
 */
static inline modem_result_t modem_command(const saturn_uart16550_t* uart,
                                            const char* cmd,
                                            char* response_buf, int buf_len) {
    return modem_command_timeout(uart, cmd, response_buf, buf_len, MODEM_TIMEOUT);
}

/**
 * Initialize modem with standard settings.
 */
static inline modem_result_t modem_init(const saturn_uart16550_t* uart) {
    char buf[MODEM_LINE_MAX];

    if (modem_command(uart, "ATZ", buf, sizeof(buf)) != MODEM_OK)
        return MODEM_ERROR;
    if (modem_command(uart, "ATE0", buf, sizeof(buf)) != MODEM_OK)
        return MODEM_ERROR;
    if (modem_command(uart, "ATX3", buf, sizeof(buf)) != MODEM_OK)
        return MODEM_ERROR;
    if (modem_command(uart, "ATV1", buf, sizeof(buf)) != MODEM_OK)
        return MODEM_ERROR;

    return MODEM_OK;
}

/**
 * Try a single AT probe at the current UART settings.
 * Sends AT\r and checks for OK (handles echo on first line).
 * Returns MODEM_OK if modem responds, MODEM_TIMEOUT_ERR otherwise.
 */
static inline modem_result_t modem_try_at(const saturn_uart16550_t* uart) {
    char buf[MODEM_LINE_MAX];
    int len;

    saturn_uart_flush_rx(uart);

    /* Send bare CR first to clear any partial command in the modem's
     * input buffer, then send the actual AT command */
    saturn_uart_puts(uart, "\r");
    for (volatile uint32_t d = 0; d < 50000; d++);
    saturn_uart_flush_rx(uart);
    saturn_uart_puts(uart, "AT\r");

    len = modem_read_line(uart, buf, sizeof(buf), MODEM_PROBE_TIMEOUT);
    if (len < 0) return MODEM_TIMEOUT_ERR;

    if (modem_parse_response(buf) == MODEM_OK)
        return MODEM_OK;

    /* First line might be echo ("AT") — try second line */
    len = modem_read_line(uart, buf, sizeof(buf), MODEM_PROBE_TIMEOUT);
    if (len < 0) return MODEM_TIMEOUT_ERR;

    if (modem_parse_response(buf) == MODEM_OK)
        return MODEM_OK;

    return MODEM_TIMEOUT_ERR;
}

/**
 * Probe modem: try AT command multiple times at multiple baud rates.
 * Encapsulates the full wake-up sequence needed after SMPC power-on.
 *
 * The first AT at each baud rate may be consumed by the L39's auto-baud
 * detector without generating a response, so we retry up to
 * MODEM_PROBE_ATTEMPTS times before moving to the next baud rate.
 *
 * Returns MODEM_OK if modem responds, MODEM_TIMEOUT_ERR otherwise.
 */
static inline modem_result_t modem_probe(const saturn_uart16550_t* uart) {
    static const uint16_t baud_rates[] = {
        MODEM_BAUD_9600,    /* Most common default */
        MODEM_BAUD_19200,   /* Some Japanese modems (HSS-0127) */
        MODEM_BAUD_4800,
        MODEM_BAUD_2400,
    };
    int num_rates = (int)(sizeof(baud_rates) / sizeof(baud_rates[0]));
    int b, attempt;

    modem_probe_baud_divisor = 0;

    for (b = 0; b < num_rates; b++) {
        saturn_uart_init(uart, baud_rates[b]);

        /* L39 settle — controller boots from EEPROM after SMPC power-on */
        for (volatile uint32_t d = 0; d < MODEM_SETTLE_CYCLES; d++);

        for (attempt = 0; attempt < MODEM_PROBE_ATTEMPTS; attempt++) {
            if (modem_try_at(uart) == MODEM_OK) {
                modem_probe_baud_divisor = baud_rates[b];
                return MODEM_OK;
            }
        }
    }

    return MODEM_TIMEOUT_ERR;
}

/**
 * Get the baud rate (as actual rate, not divisor) from the last successful probe.
 * Returns 0 if no probe has succeeded.
 */
static inline uint32_t modem_get_probe_baud(void) {
    if (modem_probe_baud_divisor == 0) return 0;
    /* Clock = 1.8432 MHz, divisor = clock / (16 * baud) */
    return 1843200UL / (16UL * modem_probe_baud_divisor);
}

/**
 * Dial a number with caller-supplied timeout.
 * The timeout must be long enough for the modem to connect (~30s typical).
 */
static inline modem_result_t modem_dial(const saturn_uart16550_t* uart,
                                         const char* number,
                                         uint32_t timeout) {
    char cmd[64];
    char buf[MODEM_LINE_MAX];
    strcpy(cmd, "ATDT");
    strcat(cmd, number);
    return modem_command_timeout(uart, cmd, buf, sizeof(buf), timeout);
}

/**
 * Hang up.
 */
static inline modem_result_t modem_hangup(const saturn_uart16550_t* uart) {
    char buf[MODEM_LINE_MAX];
    modem_escape_to_command(uart);
    return modem_command(uart, "ATH0", buf, sizeof(buf));
}

#endif /* SATURN_ONLINE_MODEM_H */
