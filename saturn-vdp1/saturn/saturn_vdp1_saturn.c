/*
 * libs/saturn-vdp1/saturn — direct VDP1 polygon command rendering.
 *
 * Bypasses SGL's slPutPolygon (3D pipeline, perspective, sort, cull) and
 * writes hardware-format command words directly into VDP1 VRAM. Designed
 * for 2D filled quads where 1 pixel = 1 unit and there is no camera.
 *
 * Architecture (mirrors arcade/lib/saturn/vdp1/saturn_vdp1.c):
 *
 *   SGL writes 4 system commands every vblank in slots 0..3:
 *     slot 0  System Clip
 *     slot 1  User Clip
 *     slot 2  Local Coordinate
 *     slot 3  END
 *
 *   We never modify slots 0..2. After SGL builds slot 3 (END), our
 *   slSynchFunction()-registered callback overwrites slot 3 with a
 *   fresh LOCAL_COORD(0, 0) command. VDP1 then walks past slot 3 into
 *   our user region.
 *
 *   sat_flush() runs on the main thread during the display period and
 *   writes polygon commands DIRECTLY to VDP1 VRAM at slots 4..4+N-1,
 *   followed by an END at slot 4+N. There is no CPU-side staging and
 *   no vblank-time copy step. This is the timing arcade and bomberman
 *   use; it works because:
 *     - SGL only touches slots 0..3 during its vblank rebuild;
 *       slots 4+ are ours and persist across frames.
 *     - VDP1's auto-draw is triggered by slot 3 transitioning from END
 *       to LOCAL_COORD; until that happens, VDP1 does not read our
 *       user region. So writing slots 4+ during the display period
 *       races nothing — the previous frame's auto-draw already
 *       completed long before we get here.
 *
 *   Write ordering inside sat_flush mirrors arcade's
 *   prewrite_vdp1_vram() exactly (arcade/lib/saturn/vdp1/
 *   saturn_vdp1.c:409-465): walk slots LOW→HIGH, writing each polygon
 *   in turn, and write the END marker at slot 4+N LAST. VDP1 in SGL's
 *   auto-draw mode reads the command list once per frame at vblank-
 *   in, not continuously during the display period, so display-period
 *   writes do not race the GPU walk. An earlier END-first / reverse-
 *   walk attempt (Pass 3) was reverted because it fixed a theoretical
 *   race that does not actually occur and exposed a real window where
 *   the new END terminated the list before the new quads were
 *   written, leaving stale quads visible at the tail.
 *
 *   The previous double-buffered "stage in CPU buffer, copy in vblank
 *   callback" design was wrong: by the time the vblank callback ran,
 *   VDP1 had already begun rasterising the next frame, and our
 *   commands either arrived late or torn. Removed.
 *
 * Caller contract: this lib registers a slSynchFunction() callback in
 * sat_init(). The caller MUST have called slInitSynch() before calling
 * saturn_vdp1_init() — otherwise the callback never fires, slot 3 is
 * never patched, and VDP1 halts at SGL's END every frame. The lobby's
 * main_saturn.c does this in arcade order.
 *
 * Caller contract (cont.): main loop should call
 * saturn_vdp1_patch_now() once per frame immediately after slSynch().
 * It re-patches slot 3 and re-writes the sprite/NBG priority registers
 * from the main thread, in addition to the vblank callback. This
 * belt-and-braces pattern matches arcade and closes a small timing
 * window where SGL can rewrite slot 3 between the callback returning
 * and our user commands actually drawing.
 *
 * Coordinate system: vertex (x, y) is screen pixels with origin at
 * top-left of the visible 320x224 area. No matrix, no perspective.
 *
 * Cannot be unit-tested from the host. Verified by:
 *   - VDP1 command-word layout matches the Saturn hardware spec
 *     vendored in saturn-tools' SL_DEF.H.
 *   - Slot patching pattern is the same one shipping in arcade and
 *     bomberman; we re-derive it here rather than lift code.
 *   - Sprite priority + EWDR refreshed each vblank so the lobby's NBG0
 *     text overlays our VDP1 quads correctly.
 */

#include <saturn_vdp1.h>
#include <saturn_vdp1/pal.h>

#include "sgl_defs.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * VDP1 register / VRAM addresses
 * ------------------------------------------------------------------------- */

#define VDP1_VRAM           0x25C00000u
#define VDP1_EWDR           0x25D00006u
#define VDP1_EDSR           0x25D00010u            /* Draw End Status Register */
#define VDP1_EDSR_CEF       0x0002u                /* Current frame End Flag */
#define VDP1_CMD_BYTES      0x20u                  /* 32-byte command */
#define VDP1_USER_FIRST     4u                     /* first slot we own */
/* Gouraud shading tables sit at VDP1 VRAM offset 0x2100, which is
 * slot 264 (each command is 0x20 bytes, 0x2100 / 0x20 = 264). Reserving
 * slots 4..259 for user commands plus slot 260 for the END marker
 * leaves the Gouraud region untouched. Mirrors arcade's
 * SATURN_VDP1_SLOT_CAP = 260 at arcade/lib/saturn/vdp1/saturn_vdp1.h:72.
 * Note: SATURN_VDP1_MAX_QUADS in the public header is the CPU-side
 * per-frame quad cap (independent of this VRAM slot cap). */
#define VDP1_SLOT_CAP       260u

/* VDP2 sprite/scroll-plane priority registers. Setting these from the
 * vblank callback ensures our priorities survive SGL's per-frame
 * register refresh. */
#define VDP2_PRISA          0x25F800F0u
#define VDP2_PRISB          0x25F800F2u
#define VDP2_PRISC          0x25F800F4u
#define VDP2_PRISD          0x25F800F6u
#define VDP2_PRINA          0x25F800F8u   /* NBG0/NBG1 priority */

/* Command type words (CTRL field). */
#define CTRL_POLYGON        0x0004u
#define CTRL_LOCAL_COORD    0x000Au
#define CTRL_END            0x8000u

/* CMDPMOD bits for our untextured RGB-direct quads. */
#define PMOD_ECD_DISABLE    0x0080u
#define PMOD_SPD_OPAQUE     0x0040u
#define PMOD_CMOD_RGB       (5u << 3)              /* 32K direct color */
#define RECT_PMOD           (PMOD_ECD_DISABLE | PMOD_SPD_OPAQUE | PMOD_CMOD_RGB)

/* ---------------------------------------------------------------------------
 * Hardware command struct (32 bytes, packed in the order VDP1 reads).
 * ------------------------------------------------------------------------- */

typedef struct vdp1_hw_cmd {
    Uint16  ctrl;          /* +0x00 */
    Uint16  link;          /* +0x02 */
    Uint16  pmod;          /* +0x04 */
    Uint16  colr;          /* +0x06 */
    Uint16  srca;          /* +0x08 */
    Uint16  size;          /* +0x0A */
    Sint16  xa, ya;        /* +0x0C */
    Sint16  xb, yb;        /* +0x10 */
    Sint16  xc, yc;        /* +0x14 */
    Sint16  xd, yd;        /* +0x18 */
    Uint16  grda;          /* +0x1C */
    Uint16  reserved;      /* +0x1E */
} vdp1_hw_cmd_t;

/* ---------------------------------------------------------------------------
 * Internal state
 *
 * No CPU-side staging. sat_flush() encodes each polygon and writes it
 * straight to VDP1 VRAM. g_have_frame becomes 1 after the first
 * successful flush so that patch_slot3() knows it's safe to redirect
 * VDP1 past SGL's END.
 * ------------------------------------------------------------------------- */

static int g_have_frame;     /* set once we have a real list */

/* Sprite priority refreshed each vblank to keep our quads under NBG0
 * text. NBG0 is at priority 7 (set by saturn-vdp2). 0x0404 = SPR0/1
 * priority 4. */
static const Uint16  k_prisa = 0x0404;       /* SPR0/1 priority 4 */
static const Uint16  k_prina = 0x0007;       /* NBG0 priority 7 */
static const Uint16  k_ewdr  = 0x0000;       /* transparent VDP1 background */

/* ---------------------------------------------------------------------------
 * Encoders
 * ------------------------------------------------------------------------- */

static void encode_polygon(vdp1_hw_cmd_t* cmd,
                           int16_t x, int16_t y,
                           uint16_t w, uint16_t h,
                           uint16_t rgb555)
{
    /* VDP1 polygon vertices use exclusive end coordinates: a quad at
     * (0,0) with size (w,h) has vertices at (0,0)-(w,0)-(w,h)-(0,h)
     * and rasterises pixels (0..w-1, 0..h-1). Matches arcade's
     * convention. */
    int16_t x1 = x;
    int16_t y1 = y;
    int16_t x2 = (int16_t)(x + (int16_t)w);
    int16_t y2 = (int16_t)(y + (int16_t)h);

    memset(cmd, 0, sizeof(*cmd));
    cmd->ctrl = CTRL_POLYGON;
    cmd->pmod = RECT_PMOD;
    cmd->colr = rgb555;
    cmd->xa = x1; cmd->ya = y1;
    cmd->xb = x2; cmd->yb = y1;
    cmd->xc = x2; cmd->yc = y2;
    cmd->xd = x1; cmd->yd = y2;
}

/* ---------------------------------------------------------------------------
 * VRAM writes
 * ------------------------------------------------------------------------- */

static void vram_write_cmd(uint32_t slot, const vdp1_hw_cmd_t* src)
{
    volatile vdp1_hw_cmd_t* dst = (volatile vdp1_hw_cmd_t*)
        (uintptr_t)(VDP1_VRAM + slot * VDP1_CMD_BYTES);

    /* Field-by-field write keeps the volatile semantics explicit. */
    dst->ctrl = src->ctrl; dst->link = src->link;
    dst->pmod = src->pmod; dst->colr = src->colr;
    dst->srca = src->srca; dst->size = src->size;
    dst->xa = src->xa; dst->ya = src->ya;
    dst->xb = src->xb; dst->yb = src->yb;
    dst->xc = src->xc; dst->yc = src->yc;
    dst->xd = src->xd; dst->yd = src->yd;
    dst->grda = src->grda; dst->reserved = 0;
}

static void vram_write_end(uint32_t slot)
{
    volatile vdp1_hw_cmd_t* dst = (volatile vdp1_hw_cmd_t*)
        (uintptr_t)(VDP1_VRAM + slot * VDP1_CMD_BYTES);
    /* Zero everything except CTRL so the GPU treats it as a clean END. */
    dst->link = 0; dst->pmod = 0; dst->colr = 0;
    dst->srca = 0; dst->size = 0;
    dst->xa = 0; dst->ya = 0; dst->xb = 0; dst->yb = 0;
    dst->xc = 0; dst->yc = 0; dst->xd = 0; dst->yd = 0;
    dst->grda = 0; dst->reserved = 0;
    dst->ctrl = (Uint16)CTRL_END;
}

/* Slot 3 patch: replace SGL's END with LOCAL_COORD(0, 0) so VDP1
 * keeps walking into slot 4. SGL rebuilds slot 3 every vblank, so we
 * have to do this every vblank. */
static void patch_slot3(void)
{
    volatile vdp1_hw_cmd_t* slot3 = (volatile vdp1_hw_cmd_t*)
        (uintptr_t)(VDP1_VRAM + 3 * VDP1_CMD_BYTES);

    if (!g_have_frame) return;     /* nothing yet — leave SGL's END alone */

    /* Set xa/ya before flipping CTRL (data-before-trigger). */
    slot3->xa = 0;
    slot3->ya = 0;
    slot3->ctrl = (Uint16)CTRL_LOCAL_COORD;
}

static void write_priority_regs(void)
{
    *(volatile Uint16*)(uintptr_t)VDP2_PRISA = k_prisa;
    *(volatile Uint16*)(uintptr_t)VDP2_PRISB = k_prisa;
    *(volatile Uint16*)(uintptr_t)VDP2_PRISC = k_prisa;
    *(volatile Uint16*)(uintptr_t)VDP2_PRISD = k_prisa;
    *(volatile Uint16*)(uintptr_t)VDP2_PRINA = k_prina;
    *(volatile Uint16*)(uintptr_t)VDP1_EWDR  = k_ewdr;
}

/* Vblank callback: re-patch slot 3 and refresh priority/EWDR registers.
 * No VRAM copy step — sat_flush already wrote the user commands during
 * the display period. */
static void vblank_callback(void)
{
    patch_slot3();
    write_priority_regs();
}

/* ---------------------------------------------------------------------------
 * PAL callbacks
 * ------------------------------------------------------------------------- */

static saturn_result_t sat_init(void* ctx, uint16_t w, uint16_t h)
{
    (void)ctx; (void)w; (void)h;

    /* Sprite mode: type 0 (palette + RGB select via MSB), priority 4. */
    slSpriteColMode(SPR_PAL_RGB);
    slSpriteType(0);
    slPrioritySpr0(4);

    /* Hand SGL a vblank callback that patches slot 3 + refreshes our
     * sprite-priority and EWDR registers. */
    slSynchFunction(vblank_callback);

    g_have_frame = 0;
    return SATURN_OK;
}

static void sat_shutdown(void* ctx)
{
    (void)ctx;
    /* slSynchFunction(NULL) would disable the callback, but the lib's
     * lifecycle is whole-program so we leave it installed. */
}

static saturn_result_t sat_flush(void* ctx,
                                 const saturn_vdp1_quad_t* quads,
                                 uint16_t n)
{
    uint16_t i;
    vdp1_hw_cmd_t cmd;

    (void)ctx;
    if (n > VDP1_SLOT_CAP - VDP1_USER_FIRST - 1) {
        n = (uint16_t)(VDP1_SLOT_CAP - VDP1_USER_FIRST - 1);
    }

    /* Wait for VDP1 to finish drawing the previous frame before writing
     * commands. Without this, when game logic completes faster than VDP1
     * can render the previous frame's commands (the "transfer-over"
     * boundary documented in PROGRAM2.txt around 20+ commands), our
     * writes to slot N race VDP1's read of slot N — VDP1 sees stale
     * data from the previous frame's higher-indexed slots. Symptom: the
     * cell the head was in last frame flickers as snake length grows.
     *
     * Coup uses this exact pattern (coup-saturn/pal/saturn/saturn_vdp1.c
     * :347-353). EDSR.CEF=1 means the current frame's draw is complete
     * and VDP1 is idle; we can write VRAM without contention. Typically
     * VDP1 finishes well before game logic does, so this rarely spins. */
    while (!(*(volatile Uint16*)(uintptr_t)VDP1_EDSR & VDP1_EDSR_CEF)) {
        /* spin */
    }

    /* Walk LOW→HIGH, write END LAST. Mirrors arcade's
     * prewrite_vdp1_vram() at arcade/lib/saturn/vdp1/saturn_vdp1.c:
     * 409-465. VDP1 in auto-draw reads the command list once per frame
     * at vblank, so display-period writes do not race the GPU walk. */
    for (i = 0; i < n; ++i) {
        encode_polygon(&cmd,
                       quads[i].x, quads[i].y,
                       quads[i].w, quads[i].h,
                       quads[i].color);
        vram_write_cmd(VDP1_USER_FIRST + i, &cmd);
    }
    vram_write_end(VDP1_USER_FIRST + n);

    /* Now that real commands are sitting in VRAM, slot 3 can safely
     * be patched from END to LOCAL_COORD. */
    g_have_frame = 1;
    return SATURN_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

static const saturn_vdp1_pal_t k_pal = {
    sat_init, sat_shutdown, sat_flush, NULL
};

void saturn_vdp1_register_saturn_pal(void)
{
    saturn_vdp1_install_pal(&k_pal);
}

/* Public belt-and-braces hook: callable from the main loop right after
 * slSynch(). Mirrors arcade/lib/saturn/vdp1/saturn_vdp1.c:556-574. */
void saturn_vdp1_patch_now(void)
{
    patch_slot3();
    write_priority_regs();
}
