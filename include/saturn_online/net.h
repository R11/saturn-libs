/**
 * saturn_online/net.h - High-level Saturn NetLink Networking API
 *
 * Gives any Saturn homebrew a drop-in path to online play over the
 * Sega NetLink modem. Wraps SMPC modem power, UART detection, AT
 * command probing, dial/answer connection, carrier monitoring, and
 * optional IRQ-driven RX into a single interface.
 *
 * The library is protocol-agnostic. Incoming bytes are reassembled
 * into length-prefixed frames — the library delivers the raw payload
 * to your callback and makes no assumptions about its contents.
 * Bring your own protocol (SCP, chat, game state, whatever).
 *
 * Quick start:
 *
 *   #include <saturn_online/net.h>
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
 *       saturn_online_config_t cfg = SATURN_ONLINE_DEFAULTS;
 *       cfg.dial_number = "0000000";   // whatever the bridge answers
 *       cfg.on_frame    = on_frame;
 *       cfg.on_status   = on_status;
 *
 *       saturn_online_init(&cfg);
 *
 *       if (saturn_online_connect() != SATURN_ONLINE_OK) {
 *           // handle error or go offline
 *       }
 *
 *       while (1) {
 *           slSynch();
 *           saturn_online_poll();
 *           // ... game logic, rendering ...
 *       }
 *   }
 *
 * The library handles:
 *   - SMPC power-on for the NetLink modem
 *   - Multi-address UART detection (7 known hardware variants)
 *   - Auto-baud modem probing (9600, 19200, 4800, 2400)
 *   - AT initialization (ATZ, ATE0, ATX3, ATV1)
 *   - Dial-out or answer-mode connection
 *   - Length-prefixed frame reassembly from UART byte stream
 *   - DCD-based carrier loss detection
 *   - Optional automatic reconnection
 *   - Connection statistics tracking
 *
 * Dependencies:
 *   - saturn_online/uart.h   (UART driver — inlined)
 *   - saturn_online/modem.h  (AT command layer — inlined)
 *   - saturn_online/framing.h (length-prefixed frame reassembly — inlined)
 *
 * Thread safety: None. Call all functions from the same context (main loop).
 */

#ifndef SATURN_ONLINE_NET_H
#define SATURN_ONLINE_NET_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Connection States
 *============================================================================*/

typedef enum {
    SATURN_ONLINE_STATE_IDLE = 0,      /* Not initialized or shut down */
    SATURN_ONLINE_STATE_DETECTING,     /* Scanning for modem hardware */
    SATURN_ONLINE_STATE_PROBING,       /* Sending AT commands to find baud rate */
    SATURN_ONLINE_STATE_INITIALIZING,  /* Running modem init sequence */
    SATURN_ONLINE_STATE_CONNECTING,    /* Dialing or waiting for incoming call */
    SATURN_ONLINE_STATE_CONNECTED,     /* Link established, exchanging data */
    SATURN_ONLINE_STATE_DISCONNECTED,  /* Carrier lost, link down */
    SATURN_ONLINE_STATE_RECONNECTING,  /* Attempting automatic reconnection */
    SATURN_ONLINE_STATE_ERROR          /* Unrecoverable error */
} saturn_online_state_t;

/*============================================================================
 * Status Codes
 *============================================================================*/

typedef enum {
    SATURN_ONLINE_OK = 0,              /* Success */
    SATURN_ONLINE_ERR_NO_MODEM,        /* No modem hardware detected */
    SATURN_ONLINE_ERR_NO_RESPONSE,     /* Modem detected but not responding to AT */
    SATURN_ONLINE_ERR_INIT_FAILED,     /* AT initialization sequence failed */
    SATURN_ONLINE_ERR_NO_CARRIER,      /* Dial completed but no carrier */
    SATURN_ONLINE_ERR_BUSY,            /* Remote end busy */
    SATURN_ONLINE_ERR_NO_DIALTONE,     /* No dial tone from phone line */
    SATURN_ONLINE_ERR_NO_ANSWER,       /* Remote end did not answer */
    SATURN_ONLINE_ERR_TIMEOUT,         /* Operation timed out */
    SATURN_ONLINE_ERR_CARRIER_LOST,    /* Carrier dropped during session */
    SATURN_ONLINE_ERR_NOT_CONNECTED,   /* Operation requires active connection */
    SATURN_ONLINE_ERR_SEND_FAILED,     /* UART transmit failed (timeout) */
    SATURN_ONLINE_ERR_INVALID_CONFIG,  /* Bad configuration */
    SATURN_ONLINE_ERR_ALREADY_INIT     /* saturn_online_init() called twice */
} saturn_online_status_t;

/*============================================================================
 * Connection Statistics
 *============================================================================*/

typedef struct {
    uint32_t bytes_sent;            /* Total bytes transmitted */
    uint32_t bytes_received;        /* Total bytes received from UART */
    uint32_t frames_sent;           /* Frames sent (saturn_online_send calls) */
    uint32_t frames_received;       /* Complete frames received */
    uint32_t frames_dropped;        /* Frames with invalid length (dropped) */
    uint32_t polls;                 /* Number of saturn_online_poll() calls */
    uint32_t carrier_losses;        /* Times DCD dropped */
    uint32_t reconnections;         /* Successful reconnections */
    uint32_t connected_frames;      /* Frames spent in connected state */
    uint32_t baud_rate;             /* Detected baud rate (e.g. 9600) */
    uint32_t irq_overflows;         /* Bytes lost due to ring buffer full (IRQ mode) */
} saturn_online_stats_t;

/*============================================================================
 * Callbacks
 *============================================================================*/

/**
 * Called when a complete length-prefixed frame arrives.
 *
 * The payload is the raw bytes inside the frame — the library makes
 * no assumption about the first byte or any of the contents. Your
 * protocol lives here.
 *
 * @param payload  Frame payload
 * @param len      Payload length in bytes (1..SATURN_ONLINE_MAX_PAYLOAD)
 * @param user     User context pointer from config
 */
typedef void (*saturn_online_frame_fn)(const uint8_t* payload, uint16_t len,
                                       void* user);

/**
 * Called when the connection state changes.
 *
 * @param old_state  Previous state
 * @param new_state  New state
 * @param status     Status code explaining the transition
 * @param user       User context pointer from config
 */
typedef void (*saturn_online_state_fn)(saturn_online_state_t old_state,
                                       saturn_online_state_t new_state,
                                       saturn_online_status_t status,
                                       void* user);

/**
 * Called with human-readable status messages during connection setup.
 * Useful for displaying progress on screen (e.g. "Probing modem...",
 * "Dialing...", "Connected!").
 *
 * @param message  Null-terminated status string
 * @param user     User context pointer from config
 */
typedef void (*saturn_online_status_fn)(const char* message, void* user);

/*============================================================================
 * Configuration
 *============================================================================*/

/** Connection mode. */
typedef enum {
    SATURN_ONLINE_MODE_DIAL = 0,   /* Dial out to a number (default) */
    SATURN_ONLINE_MODE_ANSWER      /* Wait for incoming call (ATA) */
} saturn_online_mode_t;

/**
 * Configuration structure. Initialize with SATURN_ONLINE_DEFAULTS and then
 * override individual fields.
 */
typedef struct {
    /* Connection */
    const char*           dial_number;    /* Phone number to dial (mode=DIAL) */
    saturn_online_mode_t  mode;           /* DIAL or ANSWER */
    uint32_t              dial_timeout;   /* Dial/answer timeout in loop iterations */
    bool                  auto_reconnect; /* Reconnect on carrier loss */
    int                   max_reconnect_attempts; /* 0 = unlimited */

    /* DCD monitoring */
    bool                  monitor_dcd;    /* Check DCD each poll (default: true) */

    /* Interrupt-driven receive (opt-in, default: false = polling)
     *
     * When true, installs an ISR on SCU External Interrupt 12 (vector 0x5C)
     * that drains the UART FIFO into an internal ring buffer as bytes arrive.
     * saturn_online_poll() then reads from the ring buffer instead of polling
     * the UART directly.
     *
     * Benefits: no dropped bytes during long renders or VDP operations.
     * Requires: Saturn hardware (silently ignored on non-Saturn builds).
     *
     * When false (default), saturn_online_poll() polls the UART directly.
     * This is the original behavior and always works. */
    bool                  use_irq;

    /* Frame processing */
    int                   max_bytes_per_poll; /* Max UART bytes to read per poll
                                                (0 = unlimited, drain FIFO) */

    /* Callbacks (all optional except on_frame) */
    saturn_online_frame_fn   on_frame;     /* REQUIRED: complete frame received */
    saturn_online_state_fn   on_state;     /* Connection state changed */
    saturn_online_status_fn  on_status;    /* Status message for display */
    void*                    user;         /* Context pointer for callbacks */
} saturn_online_config_t;

/**
 * Default configuration values.
 * Usage: saturn_online_config_t cfg = SATURN_ONLINE_DEFAULTS;
 */
#define SATURN_ONLINE_DEFAULTS { \
    .dial_number            = "0000000",   /* placeholder — override me */ \
    .mode                   = SATURN_ONLINE_MODE_DIAL, \
    .dial_timeout           = 180000000,   /* ~60s */ \
    .auto_reconnect         = false,       \
    .max_reconnect_attempts = 3,           \
    .monitor_dcd            = true,        \
    .use_irq                = false,       \
    .max_bytes_per_poll     = 0,           \
    .on_frame               = 0,           \
    .on_state               = 0,           \
    .on_status              = 0,           \
    .user                   = 0            \
}

/*============================================================================
 * Core API
 *============================================================================*/

/**
 * Initialize the networking subsystem.
 *
 * Must be called before any other saturn_online_* function. on_frame is
 * required; all other callbacks are optional.
 *
 * @param config  Configuration (copied internally).
 * @return SATURN_ONLINE_OK on success
 */
saturn_online_status_t saturn_online_init(const saturn_online_config_t* config);

/**
 * Establish a connection (blocking).
 *
 * Performs the full connection sequence:
 *   1. Scan known UART addresses for modem hardware
 *   2. Probe modem with AT commands at multiple baud rates
 *   3. Initialize modem (ATZ, ATE0, ATX3, ATV1)
 *   4. Dial out (or wait for answer, depending on mode)
 *
 * Status messages are reported via on_status callback. This function
 * blocks until the connection is established or fails.
 *
 * @return SATURN_ONLINE_OK if connected, or an error code
 */
saturn_online_status_t saturn_online_connect(void);

/**
 * Process incoming data (non-blocking, call every frame).
 *
 * Reads available UART bytes, feeds them to the frame receiver, and
 * invokes on_frame for each complete length-prefixed frame. Also
 * checks DCD if monitor_dcd is enabled, and initiates reconnection
 * if auto_reconnect is enabled and carrier was lost.
 *
 * Safe to call when not connected (returns immediately).
 *
 * @return SATURN_ONLINE_OK normally, or SATURN_ONLINE_ERR_CARRIER_LOST
 *         if carrier dropped and auto_reconnect is disabled
 */
saturn_online_status_t saturn_online_poll(void);

/**
 * Send a payload wrapped in a length-prefixed frame.
 *
 * Wire format: [LEN_HI:u8][LEN_LO:u8][payload...]
 *
 * The library makes no assumptions about the payload contents. Use
 * this for any protocol message your game defines.
 *
 * @param payload  Payload bytes
 * @param len      Payload length (1..SATURN_ONLINE_MAX_PAYLOAD)
 * @return SATURN_ONLINE_OK on success
 */
saturn_online_status_t saturn_online_send(const uint8_t* payload, uint16_t len);

/**
 * Send raw bytes over UART without framing.
 *
 * Use this if you're building frames yourself or need to emit
 * non-framed bytes (e.g. a sync preamble). Most callers want
 * saturn_online_send() instead.
 *
 * @param data  Bytes to send
 * @param len   Number of bytes
 * @return SATURN_ONLINE_OK on success
 */
saturn_online_status_t saturn_online_send_raw(const uint8_t* data, uint16_t len);

/**
 * Disconnect (hang up modem).
 *
 * Sends escape sequence and ATH0. The modem remains powered on, so
 * saturn_online_connect() can be called again without re-init.
 * State transitions to DISCONNECTED.
 *
 * @return SATURN_ONLINE_OK on success
 */
saturn_online_status_t saturn_online_disconnect(void);

/**
 * Full shutdown: disconnect, power off modem, release resources.
 *
 * After this call, saturn_online_init() must be called again before
 * any other networking functions.
 */
void saturn_online_shutdown(void);

/*============================================================================
 * State & Statistics
 *============================================================================*/

/**
 * Get the current connection state.
 */
saturn_online_state_t saturn_online_get_state(void);

/**
 * Check if currently connected.
 */
bool saturn_online_is_connected(void);

/**
 * Get connection statistics.
 */
saturn_online_stats_t saturn_online_get_stats(void);

/**
 * Reset all statistics to zero (baud rate is preserved).
 */
void saturn_online_reset_stats(void);

/**
 * Human-readable name for a connection state.
 */
const char* saturn_online_state_name(saturn_online_state_t state);

/**
 * Human-readable description for a status code.
 */
const char* saturn_online_status_name(saturn_online_status_t status);

/**
 * Bytes buffered in the IRQ ring buffer (0 if use_irq is false).
 * Useful for diagnostics and back-pressure decisions.
 */
uint16_t saturn_online_irq_pending(void);

/*============================================================================
 * Advanced / Low-Level Access
 *============================================================================*/

/**
 * Get a pointer to the underlying UART instance (saturn_uart16550_t*).
 *
 * Returned as const void* so callers don't need to include uart.h just
 * to pass this pointer around. Cast to `const saturn_uart16550_t*` for
 * direct register access (loopback tests, custom probes, etc.).
 * Returns NULL if not initialized or no modem was detected.
 *
 * WARNING: Modifying UART state while connected may break the link.
 */
const void* saturn_online_get_uart(void);

/**
 * Manually trigger a reconnection attempt.
 *
 * Useful when auto_reconnect is disabled and the application wants
 * to control reconnection timing.
 *
 * @return SATURN_ONLINE_OK if reconnected, or an error code
 */
saturn_online_status_t saturn_online_reconnect(void);

/**
 * Send an AT command to the modem (only usable when disconnected).
 *
 * @param cmd       AT command string (e.g. "ATI3")
 * @param response  Buffer for response
 * @param buf_len   Response buffer size
 * @return SATURN_ONLINE_OK if modem responded with OK
 */
saturn_online_status_t saturn_online_at_command(const char* cmd,
                                                char* response, int buf_len);

#endif /* SATURN_ONLINE_NET_H */
