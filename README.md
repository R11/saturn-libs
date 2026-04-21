# saturn-online

A small, reusable Saturn library for getting a homebrew game onto the
internet via the Sega NetLink modem.

It bundles the bottom layers every NetLink project has to re-write:

- **UART 16550 driver** for the NetLink's A-bus modem, including the
  SMPC `NEON`/`NEOFF` power commands and the undocumented
  `0x2582503D` post-access quirk required on real hardware.
- **AT modem layer** (`ATZ`/`ATE0`/`ATX3`/`ATV1`, dial, hangup,
  auto-baud probing at 9600/19200/4800/2400, response parsing).
- **Byte-stream transport abstraction** (`saturn_online_transport_t`
  function pointers) so games that want to swap the UART for another
  backend later have a clean seam.  Supply a transport in the config
  to skip modem detect/probe/dial entirely.
- **Length-prefixed frame reassembly** (`[LEN_HI][LEN_LO][payload]`,
  max 128-byte payloads, no assumptions about payload contents).
- **Python bridge** (`tools/bridge.py`) that answers a USB modem (or
  accepts a TCP connection in `--mock` mode) and relays bytes to your
  game server.
- **Hello example** (`examples/hello/`) that dials, sends "hello",
  and prints any echo it receives.

It is intentionally protocol-agnostic: the library delivers raw framed
payloads to your callback and transmits raw framed payloads from your
send calls.  Whatever protocol your game wants to ride on top (SCP,
rollback inputs, chat, save-state sync, you name it) lives entirely
in your consumer code.

## Three-step integration

### 1. Add the include path and source

In your game's `Makefile`:

```make
LIB_SATURN_ONLINE = /path/to/retro/saturn/libs/saturn-online
CCFLAGS += -I$(LIB_SATURN_ONLINE)/include
CCFLAGS += -I$(LIB_SATURN_ONLINE)/src     # for saturn_online_internal.h
SRCS    += $(LIB_SATURN_ONLINE)/src/net.c

# Optional extras (add only what you use):
SRCS    += $(LIB_SATURN_ONLINE)/src/connect_async.c   # non-blocking connect
SRCS    += $(LIB_SATURN_ONLINE)/src/matchmaking.c     # matchmake helper
```

No static archive to build; just let your existing SH-2 toolchain
compile the library sources alongside your app objects (see
`examples/hello/Makefile` for a working reference).

### 2. Hook up your callback

```c
#include <saturn_online/net.h>

static void on_frame(const uint8_t* payload, uint16_t len, void* user)
{
    /* payload[0] is yours.  Dispatch to your protocol however you like. */
}
```

### 3. Init, connect, send, poll

```c
saturn_online_config_t cfg = SATURN_ONLINE_DEFAULTS;
cfg.dial_number = "#555#";            /* whatever your bridge answers --
                                         the placeholder "0000000" is
                                         now rejected by init() */
cfg.on_frame    = on_frame;

saturn_online_init(&cfg);             /* powers on modem, sets state */
saturn_online_connect();              /* blocks: detect / probe / dial */

/* Send a frame -- library prepends the [LEN_HI][LEN_LO] header. */
static const uint8_t msg[] = { 'h', 'i' };
saturn_online_send(msg, sizeof(msg));

while (1) {
    slSynch();
    saturn_online_poll();             /* call every frame */
}
```

For frame-locked engines (Jo Engine, any game wanting to render during
dial), the non-blocking variant drives the connect state machine from
the main loop:

```c
saturn_online_connect_start();
while (1) {
    slSynch();
    saturn_online_connect_tick_result_t t = saturn_online_connect_tick();
    if (t == SATURN_ONLINE_TICK_OK) break;
    if (t != SATURN_ONLINE_TICK_IN_PROGRESS) { /* handle error */ break; }
    /* ... render "connecting..." ... */
}
```

That's the whole API surface you need for a basic online game.  See
`include/saturn_online/net.h` for the full API (state reporting,
stats, DCD monitoring, IRQ-driven RX, manual reconnect, TX buffering,
heartbeat pings, advanced AT command access) and
`include/saturn_online/matchmaking.h` for the pre-game matchmaking
helper.

## End-to-end testing without hardware

```bash
# Terminal 1 -- TCP echo server on the default port.
python3 tools/echo_server.py

# Terminal 2 -- bridge in mock mode, relaying mock port 2337 to the echo.
python3 tools/bridge.py --mock --server 127.0.0.1:4821

# Terminal 3 -- point a Saturn emulator's NetLink at 127.0.0.1:2337
# and boot examples/hello/build/hello.iso.  "hello" goes out, comes back,
# your on_frame fires.
```

## Building the hello example

```bash
# From retro/saturn/libs/saturn-online/:
make example
```

The `examples/hello/Makefile` targets the same SH-2 toolchain +
SGL layout as `saturn-tools`' `saturn/apps/canvas/Makefile`.  If you
don't have an SH-2 GCC locally, use saturn-tools' Docker SDK
(`retro/saturn/tools/saturn-tools/saturn/sdk/docker-saturn-build.sh`)
and point it at `retro/saturn/libs/saturn-online/examples/hello/`.

## Layout

```
saturn-online/
  include/saturn_online/
    uart.h            # 16550 driver + SMPC power + post-access quirk
    modem.h           # AT commands / probe / dial / hangup
    transport.h       # saturn_online_transport_t function-pointer interface
    framing.h         # generic [LEN_HI][LEN_LO][payload] reassembly
    net.h             # public API (init/connect/poll/send/...)
    matchmaking.h     # dial-server / post-game-id / opponent round-trip
  src/
    net.c             # main library TU
    connect_async.c   # non-blocking connect state machine
    matchmaking.c     # saturn_online_matchmake()
    saturn_online_internal.h  # private shared header (do not install)
  tools/
    bridge.py         # modem-to-TCP relay, with --mock/--direct/--server
    echo_server.py    # minimal TCP echo for round-trip tests
    test_bridge.py    # pytest suite (no hardware needed)
  tests/
    host/             # host-buildable C unit tests (no Saturn needed)
    test_host_binaries.py  # pytest glue around the host binaries
  examples/hello/
    main.c            # dial + send hello + print echo
    Makefile          # SH-2 ROM build
  Makefile            # library-only sanity build + `make example`
  README.md
```

## What this library is NOT

- **Not a protocol library.**  No SCP, no chat framing, no rollback
  input bookkeeping.  Bring your own; byte 0 of the payload is yours.
- **Not a DreamPi client.**  A DreamPi-targeting transport fits cleanly
  on top of the `saturn_online_transport_t` shape (and the config's
  `transport` override is wired for exactly this), but this first cut
  ships the UART + Python-bridge path only.  DreamPi integration lives
  in `retro/saturn/apps/netlink/` and is out of scope here.
- **Not a Sega XMP SDK wrapper.**  The Hitachi-COFF / GCC-ELF toolchain
  mismatch that blocks XMP is a separate, deferred problem.  This
  library talks to the 16550 directly.

## Layer

L1 Saturn-specific.  Uses SH-2 register access, SMPC commands, and
SGL.  There are no cross-platform shims or mock builds of the C code
-- the mock story lives entirely on the PC side in
`tools/bridge.py --mock`.
