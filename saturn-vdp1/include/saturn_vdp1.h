/*
 * libs/saturn-vdp1 — minimal VDP1 surface for the lobby project.
 *
 * What this exposes:
 *   - Init/shutdown of the VDP1 plane.
 *   - Per-frame command list: begin -> N submits -> end.
 *   - Single primitive: filled coloured rectangle. v1 snake needs nothing
 *     more; sprites, polygons, gouraud, distorted are future work.
 *
 * The platform shell (saturn/ or web/) installs a PAL whose flush callback
 * walks the command list and draws to the actual surface (VDP1 hardware
 * via SGL, or HTML canvas via emscripten).
 *
 * Apps never call SGL or canvas directly; they go through this API.
 */

#ifndef SATURN_VDP1_H
#define SATURN_VDP1_H

#include <stdint.h>
#include <stddef.h>
#include <saturn_base/result.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SATURN_VDP1_MAX_QUADS    512u

/* Saturn-native colour: RGB555 with bit 15 set for opaque pixels.
 * Helpers below pack/unpack at the lib level so games never write the
 * bit pattern themselves. */
typedef uint16_t saturn_vdp1_color_t;

static inline saturn_vdp1_color_t saturn_vdp1_rgb(uint8_t r, uint8_t g, uint8_t b) {
    /* r/g/b are 0..255; saturate to 5 bits each. */
    uint16_t r5 = (uint16_t)((r >> 3) & 0x1F);
    uint16_t g5 = (uint16_t)((g >> 3) & 0x1F);
    uint16_t b5 = (uint16_t)((b >> 3) & 0x1F);
    return (uint16_t)(0x8000u | (b5 << 10) | (g5 << 5) | r5);
}

typedef struct saturn_vdp1_quad {
    int16_t              x, y;     /* upper-left in screen coords */
    uint16_t             w, h;     /* width / height in pixels */
    saturn_vdp1_color_t  color;
} saturn_vdp1_quad_t;

/* Lifecycle. */
saturn_result_t saturn_vdp1_init    (uint16_t screen_w, uint16_t screen_h);
void            saturn_vdp1_shutdown(void);

/* Belt-and-braces main-loop hook. On Saturn, callers should invoke this
 * once per frame immediately after slSynch() to re-patch VDP1 slot 3
 * (END -> LOCAL_COORD) and re-write sprite/NBG priority registers from
 * the main thread, in addition to the vblank callback that does the
 * same work. SGL's per-frame slot-3 rewrite plus the small
 * callback->VRAM-copy timing window mean a single defence point isn't
 * always enough. On non-Saturn shells this is a no-op. Mirrors the
 * arcade lib's saturn_vdp1_patch_now() entry point. */
void            saturn_vdp1_patch_now(void);

/* Frame contract. begin_frame resets the list; submit_* append; end_frame
 * hands the list to the PAL's flush callback. */
saturn_result_t saturn_vdp1_begin_frame (void);
saturn_result_t saturn_vdp1_submit_quad (int16_t x, int16_t y,
                                         uint16_t w, uint16_t h,
                                         saturn_vdp1_color_t color);
saturn_result_t saturn_vdp1_end_frame   (void);

/* Read-side accessors so platform shells can walk the list inside their
 * flush callback (the PAL passes a pointer + count for you, but unit
 * tests can also peek at the buffer without going through PAL). */
uint16_t                   saturn_vdp1_quad_count(void);
const saturn_vdp1_quad_t*  saturn_vdp1_quads     (void);

uint16_t saturn_vdp1_screen_width (void);
uint16_t saturn_vdp1_screen_height(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_VDP1_H */
