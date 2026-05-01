/*
 * libs/saturn-vdp2/saturn — NBG1 bitmap-mode background, real hardware.
 *
 * Configures NBG1 as a 512x256 RGB555 (COL_TYPE_32768) bitmap living at
 * VDP2 VRAM B0 (0x25E40000). The lobby uploads a 320x224 sub-region;
 * the rest of the bitmap is unused (VDP2 only rasterises what scrolls
 * into the visible window).
 *
 * Pitch math: 16bpp pitch is 512 px = 1024 bytes per row. Row N starts
 * at base + N*1024. A 320-wide upload writes 640 bytes per row and
 * leaves the remaining 384 bytes per row untouched.
 *
 * NBG1 priority is set to 5 — below NBG0 text (7) and above the back
 * screen. The actual NBG1ON bit is OR'd into the slScrAutoDisp flags by
 * the lobby's main_saturn.c (see saturn_vdp2_bg_is_enabled()), keeping
 * register sequencing explicit at the boot site rather than split
 * between init functions.
 *
 * SGL macros not present in saturn-base's sgl_defs.h are forward-
 * declared at the top of this file with the same approach the previous
 * NBG1 attempts used (slBitMapNbg1, BM_512x256). The values are taken
 * directly from SGL 3.02j SL_DEF.H.
 */

#include <saturn_vdp2/bg.h>
#include <saturn_vdp2/pal.h>
#include <saturn_vdp2.h>

#include "sgl_defs.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * SGL forward declarations not in saturn-base/include/sgl_defs.h.
 * Values from SGL 3.02j SL_DEF.H.
 * ------------------------------------------------------------------------- */

#ifndef BM_512x256
#define BM_512x256   0
#endif
#ifndef BM_512x512
#define BM_512x512   1
#endif
#ifndef BM_1024x256
#define BM_1024x256  2
#endif
#ifndef BM_1024x512
#define BM_1024x512  3
#endif

extern void slBitMapNbg1(Uint16 col_type, Uint16 size, void* vram_addr);

/* ---------------------------------------------------------------------------
 * VRAM layout
 * ------------------------------------------------------------------------- */

#define BG_VRAM_BASE_ADDR  ((void*)VDP2_VRAM_B0)   /* 0x25E40000 */
#define BG_PITCH_PX        512u
#define BG_PITCH_BYTES     (BG_PITCH_PX * 2u)      /* 16bpp */
#define BG_VIS_W           SATURN_VDP2_BG_VIS_W    /* 320 */
#define BG_VIS_H           SATURN_VDP2_BG_VIS_H    /* 224 */

static int g_inited;
static int g_enabled;

static volatile uint16_t* bg_vram(void)
{
    return (volatile uint16_t*)BG_VRAM_BASE_ADDR;
}

saturn_result_t saturn_vdp2_bg_init(void)
{
    /* Configure NBG1 as a 512x256 RGB555 bitmap at VRAM B0. The
     * caller (main_saturn.c) is expected to invoke this after
     * saturn_vdp2_init() but before slTVOn(), with NBG0 already wired
     * up. We do not enable NBG1 here — main toggles auto-disp flags. */
    slBitMapNbg1(COL_TYPE_32768, BM_512x256, BG_VRAM_BASE_ADDR);
    slPriorityNbg1(5);

    /* Clear the visible region to opaque black so an enabled-but-not-
     * yet-painted NBG1 doesn't show garbage VRAM. */
    {
        volatile uint16_t* p = bg_vram();
        uint16_t y, x;
        for (y = 0; y < BG_VIS_H; ++y) {
            volatile uint16_t* row = p + (uint32_t)y * BG_PITCH_PX;
            for (x = 0; x < BG_VIS_W; ++x) row[x] = 0x8000u;
        }
    }

    g_inited  = 1;
    g_enabled = 1;            /* default: enabled (caller can disable) */
    return SATURN_OK;
}

saturn_result_t saturn_vdp2_bg_set_image(const uint16_t* rgb555,
                                         uint16_t w, uint16_t h)
{
    volatile uint16_t* dst;
    uint16_t y, x;

    if (!g_inited)             return SATURN_ERR_NOT_READY;
    if (!rgb555)               return SATURN_ERR_INVALID;
    if (w == 0 || h == 0)      return SATURN_ERR_INVALID;
    if (w > BG_VIS_W || h > BG_VIS_H) return SATURN_ERR_INVALID;

    dst = bg_vram();
    for (y = 0; y < h; ++y) {
        const uint16_t*    src_row = rgb555 + (uint32_t)y * w;
        volatile uint16_t* dst_row = dst    + (uint32_t)y * BG_PITCH_PX;
        for (x = 0; x < w; ++x) dst_row[x] = src_row[x];
    }
    return SATURN_OK;
}

void saturn_vdp2_bg_clear(uint16_t rgb555)
{
    volatile uint16_t* dst;
    uint16_t y, x;

    if (!g_inited) return;
    dst = bg_vram();
    for (y = 0; y < BG_VIS_H; ++y) {
        volatile uint16_t* row = dst + (uint32_t)y * BG_PITCH_PX;
        for (x = 0; x < BG_VIS_W; ++x) row[x] = rgb555;
    }
}

void saturn_vdp2_bg_enable(int on)
{
    g_enabled = on ? 1 : 0;
}

int saturn_vdp2_bg_is_enabled(void)
{
    return g_inited && g_enabled;
}
