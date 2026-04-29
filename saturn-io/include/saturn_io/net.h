/**
 * saturn_io/net.h - High-level Saturn NetLink Networking API
 *
 * Gives any Saturn homebrew a drop-in path to online play over the
 * Sega NetLink modem. Wraps SMPC modem power, UART detection, AT
 * command probing, dial/answer connection, carrier monitoring, and
 * optional IRQ-driven RX into a single interface.
 *
 * The library is protocol-agnostic. Incoming bytes are reassembled
 * into length-prefixed frames -- the library delivers the raw payload
 * to your callback and makes no assumptions about its contents.
 * Bring your own protocol (SCP, chat, game state, whatever).
 *
 * Quick start:
 *
 *   #include <saturn_io/net.h>
 *
 *   void on_frame(const uint8_t* payload, uint16_t len, void* ctx) {
 *       // payload[0] is yours to define.
 *   }
 *
 *   void on_status(const char* msg, void* ctx) {
 *       // optional: display msg on screen
 *   }
 *
 *   int main(void) {
 *       // ... SGL init, VDP setup, etc ...
 *
 *       saturn_io_config_t cfg = SATURN_IO_DEFAULTS;
 *       cfg.dial_number = "#555#";      // whatever the bridge answers
 *       cfg.on_frame    = on_frame;
 *       cfg.on_status   = on_status;
 *
 *       saturn_io_init(&cfg);
 *
 *       if (saturn_io_connect() != SATURN_IO_OK) {
 *           // handle error or go offline
 *       }
 *
 *       while (1) {
 *           slSynch();
 *           saturn_io_poll();
 *           // ... game logic, rendering ...
 *       }
 *   }
 *
 * The library handles:
 *   - SMPC power-on for the NetLink modem
 *   - Multi-address UART detection (7 known hardware variants)
 *   - Auto-baud modem probing (9600, 19200, 4800, 2400) or pinned baud
 *   - AT initialization (ATZ, ATE0, ATX3, ATV1)
 *   - Dial-out or answer-mode connection
 *   - Length-prefixed frame reassembly from UART byte stream
 *   - DCD-based carrier loss detection
 *   - Optional automatic reconnection
 *   - Optional TX ring buffer (non-blocking sends)
 *   - Optional heartbeat pings
 *   - Optional per-poll byte/frame caps
 *   - Pluggable transport (UART by default, or caller-supplied)
 *   - Connection statistics tracking
 *
 * Dependencies:
 *   - saturn_io/uart.h       (UART driver -- inlined)
 *   - saturn_io/modem.h      (AT command layer -- inlined)
 *   - saturn_io/framing.h    (length-prefixed frame reassembly -- inlined)
 *   - saturn_io/transport.h  (transport abstraction -- optional, forward-declared here)
 *
 * Thread safety: None. Call all functions from the same context (main loop).
 */

#ifndef SATURN_IO_NET_H
#define SATURN_IO_NET_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations -- callers may pass these pointers without
 * needing the matching headers on their own include path. */
struct saturn_uart16550;
typedef struct saturn_uart16550 saturn_uart16550_t;

struct saturn_io_transport;
typedef struct saturn_io_transport saturn_io_transport_t;

/*============================================================================
 * Connection States
 *============================================================================*/

typedef enum {
    SATURN_IO_STATE_IDLE = 0,      /* Not initialized or shut down */
    SATURN_IO_STATE_DETECTING,     /* Scanning for modem hardware */
    SATURN_IO_STATE_PROBING,       /* Sending AT commands to find baud rate */
    SATURN_IO_STATE_CONNECTING,    /* Dialing or waiting for incoming call */
    SATURN_IO_STATE_CONNECTED,     /* Link established, exchanging data */
    SATURN_IO_STATE_DISCONNECTED,  /* Carrier lost, link down */
    SATURN_IO_STATE_RECONNECTING,  /* Attempting automatic reconnection */
    SATURN_IO_STATE_ERROR          /* Unrecoverable error */
} saturn_io_state_t;

/*============================================================================
 * Status Codes
 *============================================================================*/

typedef enum {
    SATURN_IO_OK = 0,              /* Success */
    SATURN_IO_ERR_NO_MODEM,        /* No modem hardware detected */
    SATURN_IO_ERR_NO_RESPONSE,     /* Modem detected but not responding to AT */
    SATURN_IO_ERR_INIT_FAILED,     /* AT initialization sequence failed */
    SATURN_IO_ERR_NO_CARRIER,      /* Dial completed but no carrier */
    SATURN_IO_ERR_BUSY,            /* Remote end busy */
    SATURN_IO_ERR_NO_DIALTONE,     /* No dial tone from phone line */
    SATURN_IO_ERR_NO_ANSWER,       /* Remote end did not answer */
    SATURN_IO_ERR_TIMEOUT,         /* Operation timed out */
    SATURN_IO_ERR_CARRIER_LOST,    /* Carrier dropped during session */
    SATURN_IO_ERR_NOT_CONNECTED,   /* Operation requires active connection */
    SATURN_IO_ERR_SEND_FAILED,     /* UART transmit failed (timeout) */
    SATURN_IO_ERR_INVALID_CONFIG,  /* Bad configuration */
    SATURN_IO_ERR_ALREADY_INIT,    /* saturn_io_init() called twice */
    SATURN_IO_ERR_WOULDBLOCK,      /* TX buffer full (non-blocking send) */
    SATURN_IO_ERR_INTERNAL         /* Library invariant violated */
} saturn_io_status_t;

/*============================================================================
 * Connection Statistics
 *============================================================================*/

typedef struct {
    uint32_t bytes_sent;            /* Total bytes transmitted */
    uint32_t bytes_received;        /* Total bytes received from UART */
    uint32_t frames_sent;           /* Frames sent (saturn_io_send calls) */
    uint32_t frames_received;       /* Complete frames received */
    uint32_t rx_frames_dropped;     /* Frames with invalid length (dropped on RX) */
    uint32_t tx_frames_dropped;     /* Frames dropped because TX buffer was full */
    uint32_t tx_buffer_peak;        /* High-water mark for TX buffer occupancy */
    uint32_t heartbeats_sent;       /* Heartbeat frames emitted */
    uint32_t polls;                 /* Number of saturn_io_poll() calls */
    uint32_t carrier_losses;        /* Times DCD dropped */
    uint32_t reconnections;         /* Successful reconnections */
    uint32_t connected_frames;      /* Frames spent in connected state */
    uint32_t baud_rate;             /* Detected baud rate (e.g. 9600) */
    uint32_t irq_overflows;         /* Bytes lost due to ring buffer full (IRQ mode) */
} saturn_io_stats_t;

/*============================================================================
 * Callbacks
 *============================================================================*/

/**
 * Called when a complete length-prefixed frame arrives.
 *
 * The payload is the raw bytes inside the frame -- the library makes
 * no assumption about the first byte or any of the contents. Your
 * protocol lives here.
 *
 * @param payload  Frame payload
 * @param len      Payload length in bytes (1..SATURN_IO_MAX_PAYLOAD)
 * @param user     User context pointer from config
 */
typedef void (*saturn_io_frame_fn)(const uint8_t* payload, uint16_t len,
                                       void* user);

/**
 * Called when the connection state changes.
 *
 * @param old_state  Previous state
 * @param new_state  New state
 * @param status     Status code explaining the transition
 * @param user       User context pointer from config
 */
typedef void (*saturn_io_state_fn)(saturn_io_state_t old_state,
                                       saturn_io_state_t new_state,
                                       saturn_io_status_t status,
                                       void* user);

/**
 * Called with human-readable status messages during connection setup.
 * Useful for displaying progress on screen (e.g. "Probing modem...",
 * "Dialing...", "Connected!").
 *
 * @param message  Null-terminated status string
 * @param user     User context pointer from config
 */
typedef void (*saturn_io_status_fn)(const char* message, void* user);

/*============================================================================
 * Configuration
 *============================================================================*/

/** Connection mode. */
typedef enum {
    SATURN_IO_MODE_DIAL = 0,   /* Dial out to a number (default) */
    SATURN_IO_MODE_ANSWER      /* Wait for incoming call (ATA) */
} saturn_io_mode_t;

/**
 * Less-common configuration knobs.
 *
 * All fields zero-default to the library's baseline behavior
 * (auto-probe baud, no auto-reconnect, no TX buffer, no heartbeat,
 * no per-poll caps). Most games leave this struct at zero and set
 * only the top-level fields.
 */
typedef struct {
    /* Reconnect */
    bool     auto_reconnect;         /* Reconnect on carrier loss */
    int      max_reconnect_attempts; /* 0 = unlimited */

    /* DCD monitoring */
    bool     monitor_dcd;            /* Check DCD each poll (library default: true) */

    /* Interrupt-driven receive (opt-in, default: false = polling)
     *
     * When true, installs an ISR on SCU External Interrupt 12 (vector 0x5C)
     * that drains the UART FIFO into an internal ring buffer as bytes arrive.
     * saturn_io_poll() then reads from the ring buffer instead of polling
     * the UART directly.
     *
     * Requires: Saturn hardware (silently ignored on non-Saturn builds). */
    bool     use_irq;

    /* Per-poll caps */
    uint16_t max_bytes_per_poll;     /* Max bytes drained per poll (0 = unlimited) */
    uint16_t max_frames_per_poll;    /* Max frames delivered per poll (0 = unlimited) */

    /* Baud control. 0 = auto-probe at standard rates;
     * nonzero = pinned baud (use the SATURN_IO_MODEM_BAUD_* divisors). */
    uint16_t fixed_baud_divisor;

    /* Heartbeat. When nonzero the library emits a single-byte ping
     * frame (payload 0x00) every N poll-seconds, and signals a
     * SATURN_IO_ERR_TIMEOUT if no RX bytes arrive for 2*N seconds.
     * Units are "seconds at 60 Hz poll"; scale to your poll rate. */
    uint32_t heartbeat_secs;

    /* TX buffering. 0 = synchronous UART writes (legacy behavior).
     * Nonzero = library copies sends into a ring buffer (up to
     * SATURN_IO_TX_BUFFER_MAX) and drains on poll. Sends that
     * don't fit return SATURN_IO_ERR_WOULDBLOCK. */
    uint32_t tx_buffer_size;
} saturn_io_advanced_t;

/**
 * Top-level configuration structure.
 *
 * Initialize with SATURN_IO_DEFAULTS and then override individual
 * fields. The nested `advanced` block zero-initializes to the library's
 * default behavior.
 */
typedef struct {
    /* Connection */
    const char*           dial_number;       /* Phone number to dial (mode=DIAL) */
    saturn_io_mode_t  mode;              /* DIAL or ANSWER */
    uint32_t              dial_timeout_secs; /* Dial/answer timeout in seconds */

    /* Callbacks (on_frame is required) */
    saturn_io_frame_fn   on_frame;     /* REQUIRED: complete frame received */
    saturn_io_state_fn   on_state;     /* Connection state changed */
    saturn_io_status_fn  on_status;    /* Status message for display */
    void*                    user;         /* Context pointer for callbacks */

    /* Optional transport override. NULL = default UART transport (detect,
     * probe, dial). Non-NULL = the library skips modem setup and talks
     * to the supplied transport as if already connected. Useful for
     * emulator-over-TCP, DreamPi-over-UDP, captured-file replay, or
     * CI tests that want to swap the link layer without touching the
     * rest of the library. */
    const saturn_io_transport_t* transport;

    /* Less-common knobs (zero-default). */
    saturn_io_advanced_t advanced;
} saturn_io_config_t;

/**
 * Default configuration values.
 * Usage: saturn_io_config_t cfg = SATURN_IO_DEFAULTS;
 *
 * `dial_number` intentionally left NULL so saturn_io_init() rejects
 * unconfigured callers with SATURN_IO_ERR_INVALID_CONFIG. Every
 * caller must supply a real dial number (e.g. "#555#") before init
 * (unless they also supply a transport override, which bypasses dial).
 */
#define SATURN_IO_DEFAULTS { \
    .dial_number       = 0,                       \
    .mode              = SATURN_IO_MODE_DIAL, \
    .dial_timeout_secs = 60,                      \
    .on_frame          = 0,                       \
    .on_state          = 0,                       \
    .on_status         = 0,                       \
    .user              = 0,                       \
    .transport         = 0,                       \
    .advanced = {                                 \
        .auto_reconnect         = false,          \
        .max_reconnect_attempts = 3,              \
        .monitor_dcd            = true,           \
        .use_irq                = false,          \
        .max_bytes_per_poll     = 0,              \
        .max_frames_per_poll    = 0,              \
        .fixed_baud_divisor     = 0,              \
        .heartbeat_secs         = 0,              \
        .tx_buffer_size         = 0,              \
    }                                             \
}

/*============================================================================
 * Core API
 *============================================================================*/

/**
 * Initialize the networking subsystem.
 *
 * Must be called before any other saturn_io_* function. Required
 * config fields:
 *   - on_frame (callback)
 *   - dial_number when mode == DIAL and no transport override is given.
 *     Must not be NULL, empty, or the historical placeholder "0000000".
 *
 * @param config  Configuration (copied internally).
 * @return SATURN_IO_OK on success; SATURN_IO_ERR_INVALID_CONFIG
 *         if the dial number is missing/placeholder or on_frame is NULL;
 *         SATURN_IO_ERR_ALREADY_INIT if called twice.
 */
saturn_io_status_t saturn_io_init(const saturn_io_config_t* config);

/**
 * Establish a connection (blocking).
 *
 * Performs the full connection sequence:
 *   1. Scan known UART addresses for modem hardware
 *   2. Probe modem with AT commands at multiple baud rates
 *   3. Initialize modem (ATZ, ATE0, ATX3, ATV1)
 *   4. Dial out (or wait for answer, depending on mode)
 *
 * When a transport override is supplied the above steps are skipped and
 * the library immediately enters CONNECTED state using that transport.
 *
 * Status messages are reported via on_status callback. This function
 * blocks until the connection is established or fails.
 *
 * @return SATURN_IO_OK if connected, or an error code
 */
saturn_io_status_t saturn_io_connect(void);

/**
 * Begin a non-blocking connect.
 *
 * Cooperative state-machine variant of saturn_io_connect(). Call
 * once, then invoke saturn_io_connect_tick() each frame until it
 * returns non-IN_PROGRESS. Useful inside frame-locked game loops
 * (e.g. Jo Engine's jo_core_run) that must render while dialing.
 *
 * The blocking saturn_io_connect() remains available and is the
 * known-good fallback if the non-blocking variant misbehaves.
 *
 * @return SATURN_IO_OK if the connect was armed, or an error code
 *         (ERR_INVALID_CONFIG if not initialized).
 */
saturn_io_status_t saturn_io_connect_start(void);

/** Result of a single non-blocking connect tick. */
typedef enum {
    SATURN_IO_TICK_IN_PROGRESS = 0,       /* Keep calling next frame */
    SATURN_IO_TICK_OK,                    /* Connection established */
    SATURN_IO_TICK_ERR_NO_MODEM,
    SATURN_IO_TICK_ERR_NO_RESPONSE,
    SATURN_IO_TICK_ERR_INIT_FAILED,
    SATURN_IO_TICK_ERR_NO_CARRIER,
    SATURN_IO_TICK_ERR_BUSY,
    SATURN_IO_TICK_ERR_NO_DIALTONE,
    SATURN_IO_TICK_ERR_NO_ANSWER,
    SATURN_IO_TICK_ERR_TIMEOUT,
    SATURN_IO_TICK_ERR_INVALID_CONFIG,
    SATURN_IO_TICK_ERR_INTERNAL
} saturn_io_connect_tick_result_t;

/**
 * Advance the non-blocking connect state machine one step.
 *
 * Safe to call every frame. Performs a bounded amount of work per
 * call (usually one AT exchange or one settle-step). Transitions to
 * CONNECTED return SATURN_IO_TICK_OK exactly once; subsequent
 * calls continue to return OK until a new connect_start() is invoked.
 */
saturn_io_connect_tick_result_t saturn_io_connect_tick(void);

/**
 * Process incoming data (non-blocking, call every frame).
 *
 * Reads available UART bytes, feeds them to the frame receiver, and
 * invokes on_frame for each complete length-prefixed frame. Also
 * checks DCD if monitor_dcd is enabled, drains a pending TX buffer,
 * emits heartbeats if configured, and initiates reconnection if
 * auto_reconnect is enabled and carrier was lost.
 *
 * Safe to call when not connected (returns immediately).
 *
 * @return SATURN_IO_OK normally, or SATURN_IO_ERR_CARRIER_LOST
 *         if carrier dropped and auto_reconnect is disabled
 */
saturn_io_status_t saturn_io_poll(void);

/**
 * Send a payload wrapped in a length-prefixed frame.
 *
 * Wire format: [LEN_HI:u8][LEN_LO:u8][payload...]
 *
 * The library makes no assumptions about the payload contents. Use
 * this for any protocol message your game defines.
 *
 * If a TX buffer is configured (advanced.tx_buffer_size > 0), this
 * copies into the buffer and returns immediately -- the bytes are
 * drained by subsequent calls to saturn_io_poll(). If the buffer
 * is full, returns SATURN_IO_ERR_WOULDBLOCK. Otherwise sends
 * synchronously.
 *
 * @param payload  Payload bytes
 * @param len      Payload length (1..SATURN_IO_MAX_PAYLOAD)
 * @return SATURN_IO_OK on success
 */
saturn_io_status_t saturn_io_send(const uint8_t* payload, uint16_t len);

/**
 * Disconnect (hang up modem).
 *
 * Sends escape sequence and ATH0. The modem remains powered on, so
 * saturn_io_connect() can be called again without re-init.
 * State transitions to DISCONNECTED.
 *
 * @return SATURN_IO_OK on success
 */
saturn_io_status_t saturn_io_disconnect(void);

/**
 * Full shutdown: disconnect, power off modem, release resources.
 *
 * After this call, saturn_io_init() must be called again before
 * any other networking functions.
 */
void saturn_io_shutdown(void);

/*============================================================================
 * State & Statistics
 *============================================================================*/

/**
 * Get the current connection state.
 */
saturn_io_state_t saturn_io_get_state(void);

/**
 * Check if currently connected.
 */
bool saturn_io_is_connected(void);

/**
 * Get connection statistics.
 */
saturn_io_stats_t saturn_io_get_stats(void);

/**
 * Reset all statistics to zero (baud rate is preserved).
 */
void saturn_io_reset_stats(void);

/**
 * Human-readable name for a connection state.
 */
const char* saturn_io_state_name(saturn_io_state_t state);

/**
 * Human-readable description for a status code.
 */
const char* saturn_io_status_name(saturn_io_status_t status);

/**
 * Bytes buffered in the IRQ ring buffer (0 if use_irq is false).
 * Useful for diagnostics and back-pressure decisions.
 */
uint16_t saturn_io_irq_pending(void);

/**
 * Copy the most recent modem response text into the supplied buffer.
 *
 * Returns the number of bytes written (excluding the null terminator).
 * Useful for displaying AT errors to the user during connect.
 *
 * @param buf  Destination (null-terminated on return)
 * @param len  Buffer length; must be >= 1
 * @return bytes written (0 if no response has been captured yet)
 */
int saturn_io_last_modem_response(char* buf, int len);

/*============================================================================
 * Advanced / Low-Level Access
 *============================================================================*/

/**
 * Get a pointer to the underlying UART instance.
 *
 * Returns NULL if not initialized, no modem was detected, or a non-UART
 * transport override is active. Callers who need direct register access
 * (loopback tests, custom probes) can use the returned pointer without
 * including <saturn_io/uart.h> on their include path -- `net.h`
 * forward-declares the UART type.
 *
 * WARNING: Modifying UART state while connected may break the link.
 */
const saturn_uart16550_t* saturn_io_get_uart(void);

/**
 * Manually trigger a reconnection attempt.
 *
 * Useful when auto_reconnect is disabled and the application wants
 * to control reconnection timing.
 *
 * @return SATURN_IO_OK if reconnected, or an error code
 */
saturn_io_status_t saturn_io_reconnect(void);

/**
 * Send an AT command to the modem (only usable when disconnected).
 *
 * @param cmd       AT command string (e.g. "ATI3")
 * @param response  Buffer for response
 * @param buf_len   Response buffer size
 * @return SATURN_IO_OK if modem responded with OK
 */
saturn_io_status_t saturn_io_at_command(const char* cmd,
                                                char* response, int buf_len);

#endif /* SATURN_IO_NET_H */
