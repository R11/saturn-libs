/*
 * libs/saturn-smpc — peripherals and clock.
 *
 * Public surface for reading the Saturn's SMPC (System Manager &
 * Peripheral Control). Hides the underlying register protocol, multitap
 * routing, and active-low button quirk; presents a flat 12-slot pad table
 * and a portable button bitmask.
 *
 * Lifecycle:
 *   - Platform shell installs a PAL via saturn_smpc_install_pal().
 *   - Consumer calls saturn_smpc_init() once at startup.
 *   - Consumer calls saturn_smpc_poll() once per frame; this updates the
 *     internal pad table.
 *   - Consumer reads pads via saturn_smpc_pad_get() / pad_by_addr() and
 *     queries buttons via the held/pressed/released helpers.
 *
 * No platform headers leak through. The Saturn shell installs a PAL that
 * reads Smpc_Peripheral[N] internally.
 */

#ifndef SATURN_SMPC_H
#define SATURN_SMPC_H

#include <stdint.h>
#include <stddef.h>

#include <saturn_base/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Capacity. Two SMPC ports x up to 6 multitap slots = 12 peripherals.
 * ------------------------------------------------------------------------- */

#define SATURN_SMPC_MAX_PADS    12u
#define SATURN_SMPC_MAX_PORTS    2u
#define SATURN_SMPC_MAX_TAPS     6u

/* ---------------------------------------------------------------------------
 * Peripheral kinds. Future-proofs for keyboard, mouse, light gun.
 * ------------------------------------------------------------------------- */

enum {
    SATURN_SMPC_KIND_NONE     = 0,
    SATURN_SMPC_KIND_DIGITAL  = 1, /* standard control pad */
    SATURN_SMPC_KIND_ANALOG   = 2, /* 3D control pad with analog stick */
    SATURN_SMPC_KIND_MOUSE    = 3,
    SATURN_SMPC_KIND_KEYBOARD = 4,
    SATURN_SMPC_KIND_GUN      = 5,
    SATURN_SMPC_KIND_UNKNOWN  = 0xFF
};

/* ---------------------------------------------------------------------------
 * Button bitmask (active-high). The lib hides the SMPC's active-low layout.
 * ------------------------------------------------------------------------- */

enum {
    SATURN_SMPC_BUTTON_RIGHT  = 0x0001,
    SATURN_SMPC_BUTTON_LEFT   = 0x0002,
    SATURN_SMPC_BUTTON_DOWN   = 0x0004,
    SATURN_SMPC_BUTTON_UP     = 0x0008,
    SATURN_SMPC_BUTTON_START  = 0x0010,
    SATURN_SMPC_BUTTON_A      = 0x0020,
    SATURN_SMPC_BUTTON_B      = 0x0040,
    SATURN_SMPC_BUTTON_C      = 0x0080,
    SATURN_SMPC_BUTTON_X      = 0x0100,
    SATURN_SMPC_BUTTON_Y      = 0x0200,
    SATURN_SMPC_BUTTON_Z      = 0x0400,
    SATURN_SMPC_BUTTON_L      = 0x0800,
    SATURN_SMPC_BUTTON_R      = 0x1000
};

/* ---------------------------------------------------------------------------
 * Pad state.
 *
 * The struct is plain data; consumers may read fields directly. The lib
 * owns the buffer — pointers returned by saturn_smpc_pad_get() are valid
 * only until the next saturn_smpc_poll().
 * ------------------------------------------------------------------------- */

typedef struct saturn_smpc_pad {
    uint8_t  port;          /* 0 or 1 */
    uint8_t  tap;           /* 0..5; 0 means direct-attached on the port */
    uint8_t  connected;     /* 1 if this slot has a peripheral right now */
    uint8_t  kind;          /* SATURN_SMPC_KIND_* */
    uint16_t buttons;       /* current frame, active-high bitmask */
    uint16_t prev_buttons;  /* previous frame, used by edge detection */
    int8_t   axis_x;        /* analog stick X (-127..127); 0 for digital */
    int8_t   axis_y;
    uint8_t  trigger_l;     /* analog L trigger (0..255) */
    uint8_t  trigger_r;
    uint16_t _reserved;     /* keep struct size stable for static_assert */
} saturn_smpc_pad_t;

/* ---------------------------------------------------------------------------
 * Real-time clock readout.
 * ------------------------------------------------------------------------- */

typedef struct saturn_smpc_rtc {
    uint16_t year;          /* e.g. 2026 */
    uint8_t  month;         /* 1..12 */
    uint8_t  day;           /* 1..31 */
    uint8_t  weekday;       /* 0=Sun..6=Sat */
    uint8_t  hour;          /* 0..23 */
    uint8_t  minute;        /* 0..59 */
    uint8_t  second;        /* 0..59 */
} saturn_smpc_rtc_t;

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/* Initialise the lib. Requires a PAL to have been installed first.
 * Calls into PAL.init(). Returns SATURN_ERR_NOT_READY if no PAL. */
saturn_result_t saturn_smpc_init(void);

/* Tear down. Idempotent. */
void saturn_smpc_shutdown(void);

/* Read the current state of every peripheral. Updates the internal pad
 * table and the prev_buttons history used by edge detection. Call once
 * per frame. */
saturn_result_t saturn_smpc_poll(void);

/* ---------------------------------------------------------------------------
 * Pad queries
 * ------------------------------------------------------------------------- */

/* Number of peripherals currently connected (0..12). */
uint8_t saturn_smpc_pad_count(void);

/* Read pad by flat index 0..11. Returns NULL if index is out of range
 * regardless of whether that slot is connected. */
const saturn_smpc_pad_t* saturn_smpc_pad_get(uint8_t flat_index);

/* Read pad by physical address (port 0/1, tap 0..5). Returns NULL if no
 * such address exists in the table. */
const saturn_smpc_pad_t* saturn_smpc_pad_by_addr(uint8_t port, uint8_t tap);

/* ---------------------------------------------------------------------------
 * Button helpers (portable, host-testable)
 * ------------------------------------------------------------------------- */

/* True if any bit in mask is set in this frame. */
int saturn_smpc_button_held    (const saturn_smpc_pad_t* p, uint16_t mask);

/* True if any bit in mask transitioned 0 -> 1 between prev_buttons and
 * buttons (newly pressed this frame). */
int saturn_smpc_button_pressed (const saturn_smpc_pad_t* p, uint16_t mask);

/* True if any bit in mask transitioned 1 -> 0 (newly released this frame). */
int saturn_smpc_button_released(const saturn_smpc_pad_t* p, uint16_t mask);

/* ---------------------------------------------------------------------------
 * Connect / disconnect events
 *
 * Each returns 1 exactly once on the first poll where the underlying flag
 * flipped from the previous poll. Subsequent polls without further changes
 * return 0.
 * ------------------------------------------------------------------------- */

int saturn_smpc_pad_just_connected   (uint8_t flat_index);
int saturn_smpc_pad_just_disconnected(uint8_t flat_index);

/* ---------------------------------------------------------------------------
 * Real-time clock
 *
 * Reads the SMPC RTC into out. Validates fields (month 1..12, day 1..31,
 * etc.) and returns SATURN_ERR_HARDWARE if the readout is malformed.
 * ------------------------------------------------------------------------- */

saturn_result_t saturn_smpc_rtc_read(saturn_smpc_rtc_t* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURN_SMPC_H */
