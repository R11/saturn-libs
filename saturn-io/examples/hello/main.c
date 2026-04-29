/**
 * hello online - minimal saturn-io example
 *
 * Connects via NetLink, sends a "hello" payload, prints any echo
 * it receives.  Requires a PC-side listener such as:
 *
 *     python3 tools/echo_server.py
 *     python3 tools/bridge.py --mock --server 127.0.0.1:4821
 *
 * Point your emulator's NetLink at 127.0.0.1:2337 (the bridge mock
 * port) and run this ROM.  You should see "hello" land in the echo
 * server log and the echo bytes come back into the received frame.
 *
 * The library is protocol-agnostic -- this example defines no header
 * format at all.  Byte 0 of the payload is the letter 'h' of "hello".
 */

#include <saturn_io/net.h>

#include <sl_def.h>   /* SGL prototypes: slSynch, slPrint, slInitSystem, etc. */
#include <string.h>

/* Display helpers -- the library doesn't do any text; we use SGL slPrint
 * against the VDP2 scroll plane set up by the SGL init. */
static int g_line;

static void put(const char* s)
{
    slPrint((char*)s, slLocate(2, g_line));
    if (++g_line > 27) g_line = 0;
}

/* ----------------------------------------------------------------------- */

static void on_status(const char* msg, void* user)
{
    (void)user;
    put(msg);
}

static void on_frame(const uint8_t* payload, uint16_t len, void* user)
{
    (void)user;
    /* Treat the frame as ASCII for display.  Real games would interpret
     * payload[0] as their own protocol opcode. */
    char buf[SATURN_IO_MAX_PAYLOAD + 8];
    int n = (len > SATURN_IO_MAX_PAYLOAD) ? SATURN_IO_MAX_PAYLOAD : len;
    int i;
    int pos = 0;
    buf[pos++] = 'R';
    buf[pos++] = 'X';
    buf[pos++] = ':';
    buf[pos++] = ' ';
    for (i = 0; i < n && pos < (int)sizeof(buf) - 1; i++) {
        uint8_t b = payload[i];
        buf[pos++] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
    }
    buf[pos] = '\0';
    put(buf);
}

/* ----------------------------------------------------------------------- */

void ss_main(void)
{
    saturn_io_config_t cfg = SATURN_IO_DEFAULTS;
    cfg.dial_number  = "#555#";     /* bridge answers whatever we dial; the
                                       previous "0000000" placeholder is now
                                       rejected by saturn_io_init */
    cfg.on_frame     = on_frame;
    cfg.on_status    = on_status;

    slInitSystem(TV_320x224, NULL, 1);
    slPrintHZ(0, NULL);
    put("saturn-io hello");

    if (saturn_io_init(&cfg) != SATURN_IO_OK) {
        put("init failed");
        while (1) slSynch();
    }

    if (saturn_io_connect() != SATURN_IO_OK) {
        put("connect failed");
        while (1) slSynch();
    }

    /* Fire off the hello once. */
    {
        static const uint8_t hello[] = { 'h', 'e', 'l', 'l', 'o' };
        saturn_io_send(hello, (uint16_t)sizeof(hello));
        put("sent: hello");
    }

    /* Poll forever. */
    while (1) {
        slSynch();
        saturn_io_poll();
    }
}
