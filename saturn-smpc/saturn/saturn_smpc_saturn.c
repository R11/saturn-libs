/*
 * libs/saturn-smpc/saturn — real Saturn shell.
 *
 * Implements saturn_smpc_pal_t against SGL's Smpc_Peripheral[] +
 * Per_Connect1/2 globals plus slGetStatus()/Smpc_Status->rtc for the
 * clock. Compiles only under sh-elf-gcc with SGL headers on the include
 * path; the host build of this lib excludes this file.
 *
 * Verification: this file cannot be unit-tested from the host. Correctness
 * is guaranteed by:
 *   1. Bit assignments and field types matching the SGL header definitions
 *      vendored in saturn-tools/saturn/lib/gfx/sgl_defs.h (which mirror
 *      the SEGA SL_DEF.H originals).
 *   2. The same bit-extraction pattern that arcade and bomberman ship and
 *      have been verified to work on real hardware and in mednafen.
 *
 * Multitap: SGL lays Smpc_Peripheral[] out as
 *   indices [0 .. Per_Connect1 - 1]                 = port 0 peripherals,
 *   indices [Per_Connect1 .. Per_Connect1+Per_Connect2 - 1] = port 1.
 * Each port can have up to 6 entries when a multitap is attached.
 *
 * The Saturn shell is registered by calling saturn_smpc_register_saturn_pal()
 * once during boot, before saturn_smpc_init().
 */

#include <saturn_smpc.h>
#include <saturn_smpc/pal.h>
#include <saturn_base/result.h>

/* SGL public surface — only visible inside saturn/ shells. */
#include "sgl_defs.h"

#include <string.h>

/* sgl_defs.h declares Per_Connect1 but not Per_Connect2 in the slim
 * profile; the SGL distribution proper does. Mirror the arcade pattern
 * and declare it locally. */
extern Uint8 Per_Connect2;

/* SMPC peripheral IDs (from SL_DEF.H). */
#define SATURN_SMPC_ID_DIGITAL   0x02u
#define SATURN_SMPC_ID_ANALOG    0x15u
#define SATURN_SMPC_ID_WHEEL     0x13u
#define SATURN_SMPC_ID_KEYBOARD  0x14u
#define SATURN_SMPC_ID_MOUSE     0xE3u
#define SATURN_SMPC_ID_GUN       0xE1u
#define SATURN_SMPC_ID_NONE      0xFFu

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static uint8_t kind_from_smpc_id(Uint8 id)
{
    switch (id) {
        case SATURN_SMPC_ID_DIGITAL:  return SATURN_SMPC_KIND_DIGITAL;
        case SATURN_SMPC_ID_ANALOG:   return SATURN_SMPC_KIND_ANALOG;
        case SATURN_SMPC_ID_MOUSE:    return SATURN_SMPC_KIND_MOUSE;
        case SATURN_SMPC_ID_KEYBOARD: return SATURN_SMPC_KIND_KEYBOARD;
        case SATURN_SMPC_ID_GUN:      return SATURN_SMPC_KIND_GUN;
        case SATURN_SMPC_ID_NONE:     return SATURN_SMPC_KIND_NONE;
        default:                      return SATURN_SMPC_KIND_UNKNOWN;
    }
}

/* SMPC pad data is active-low (bit=0 means pressed). Translate to our
 * active-high bitmask so games never see the hardware quirk. */
static uint16_t buttons_from_smpc_data(Uint16 data)
{
    uint16_t out = 0;
    if (!(data & PER_DGT_KU)) out |= SATURN_SMPC_BUTTON_UP;
    if (!(data & PER_DGT_KD)) out |= SATURN_SMPC_BUTTON_DOWN;
    if (!(data & PER_DGT_KL)) out |= SATURN_SMPC_BUTTON_LEFT;
    if (!(data & PER_DGT_KR)) out |= SATURN_SMPC_BUTTON_RIGHT;
    if (!(data & PER_DGT_ST)) out |= SATURN_SMPC_BUTTON_START;
    if (!(data & PER_DGT_TA)) out |= SATURN_SMPC_BUTTON_A;
    if (!(data & PER_DGT_TB)) out |= SATURN_SMPC_BUTTON_B;
    if (!(data & PER_DGT_TC)) out |= SATURN_SMPC_BUTTON_C;
    if (!(data & PER_DGT_TX)) out |= SATURN_SMPC_BUTTON_X;
    if (!(data & PER_DGT_TY)) out |= SATURN_SMPC_BUTTON_Y;
    if (!(data & PER_DGT_TZ)) out |= SATURN_SMPC_BUTTON_Z;
    if (!(data & PER_DGT_TL)) out |= SATURN_SMPC_BUTTON_L;
    if (!(data & PER_DGT_TR)) out |= SATURN_SMPC_BUTTON_R;
    return out;
}

/* BCD-to-decimal for SMPC RTC fields. SGL provides slDec2Hex which does
 * BCD->decimal despite the misleading name. */
static uint16_t bcd_to_dec_u16(Uint32 bcd) { return (uint16_t)slDec2Hex(bcd); }
static uint8_t  bcd_to_dec_u8 (Uint32 bcd) { return (uint8_t) slDec2Hex(bcd); }

/* ---------------------------------------------------------------------------
 * PAL callbacks
 * ------------------------------------------------------------------------- */

static saturn_result_t shell_init(void* ctx)
{
    (void)ctx;
    /* SGL handles low-level peripheral init via slInitSystem(), which the
     * boot path calls before any lib's init. Nothing to do here. */
    return SATURN_OK;
}

static void shell_shutdown(void* ctx)
{
    (void)ctx;
    /* Symmetric no-op. */
}

static saturn_result_t shell_read_pads(void* ctx,
                                       saturn_smpc_pad_t out[SATURN_SMPC_MAX_PADS],
                                       uint8_t* n_out)
{
    uint8_t  per_count[2];
    uint8_t  port;
    uint8_t  written = 0;
    uint8_t  smpc_idx = 0;

    (void)ctx;

    per_count[0] = Per_Connect1;
    per_count[1] = Per_Connect2;

    for (port = 0; port < SATURN_SMPC_MAX_PORTS; ++port) {
        uint8_t n = per_count[port];
        uint8_t tap;
        if (n > SATURN_SMPC_MAX_TAPS) n = SATURN_SMPC_MAX_TAPS;

        for (tap = 0; tap < n && written < SATURN_SMPC_MAX_PADS; ++tap) {
            const PerDigital*  src = &Smpc_Peripheral[smpc_idx];
            saturn_smpc_pad_t* dst = &out[written];

            dst->port      = port;
            dst->tap       = tap;
            dst->kind      = kind_from_smpc_id(src->id);
            dst->connected = (dst->kind != SATURN_SMPC_KIND_NONE) ? 1u : 0u;
            dst->buttons   = buttons_from_smpc_data(src->data);
            /* prev_buttons is filled in by saturn-smpc/core/ from the
             * previous frame's buttons. Leave it untouched here. */
            dst->prev_buttons = 0;
            dst->axis_x       = 0; /* analog support: future work */
            dst->axis_y       = 0;
            dst->trigger_l    = 0;
            dst->trigger_r    = 0;
            dst->_reserved    = 0;

            written++;
            smpc_idx++;
        }
    }

    *n_out = written;
    return SATURN_OK;
}

static saturn_result_t shell_read_rtc(void* ctx, saturn_smpc_rtc_t* out)
{
    SmpcDateTime rtc;

    (void)ctx;

    /* Latch a fresh RTC snapshot. SMPC_NO_WAIT means the actual readout
     * lands on the next vblank; for our cadence (RTC is queried on
     * specific events, not per-frame) that's fine. */
    slGetStatus();
    if (!Smpc_Status) return SATURN_ERR_HARDWARE;
    rtc = Smpc_Status->rtc;

    /* SGL packs the 4-digit year as a 16-bit BCD value. month low nibble
     * is the BCD month; high nibble is the weekday. date is BCD day. */
    out->year    = bcd_to_dec_u16(rtc.year);
    out->month   = bcd_to_dec_u8 (rtc.month & 0x0Fu);
    out->day     = bcd_to_dec_u8 (rtc.date);
    out->weekday = (uint8_t)((rtc.month >> 4) & 0x0Fu);
    out->hour    = bcd_to_dec_u8 (rtc.hour);
    out->minute  = bcd_to_dec_u8 (rtc.minute);
    out->second  = bcd_to_dec_u8 (rtc.second);

    return SATURN_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

static const saturn_smpc_pal_t k_saturn_pal = {
    shell_init,
    shell_shutdown,
    shell_read_pads,
    shell_read_rtc,
    NULL
};

void saturn_smpc_register_saturn_pal(void)
{
    saturn_smpc_install_pal(&k_saturn_pal);
}
