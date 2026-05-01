/*
 * libs/saturn-vdp2/bg — NBG1 bitmap-mode background helper.
 *
 * Configures NBG1 as a 512x256 RGB555 bitmap living in VDP2 VRAM B0
 * (0x25E40000). Each frame the lobby uploads a 320x224 sub-region for
 * the title splash or per-game backdrop. NBG1 sits at priority 5 — below
 * NBG0 text (7) and above the back-screen.
 *
 * This API is independent of the NBG0 text path in saturn_vdp2.h. The
 * caller drives saturn_vdp2_bg_init() once and then either
 * saturn_vdp2_bg_set_image() or saturn_vdp2_bg_clear() when the
 * displayed background changes (uploads are large — ~140 KB at 320x224
 * — so the lobby skips re-uploads when the image pointer is unchanged).
 */
#ifndef SATURN_VDP2_BG_H
#define SATURN_VDP2_BG_H

#include <stdint.h>
#include <saturn_base/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native NBG1 bitmap pitch in pixels (words). NBG1 is configured as a
 * 512x256 16bpp bitmap; uploads of widths < 512 leave the right-side
 * columns untouched but VDP2 only displays the visible 320x224. */
#define SATURN_VDP2_BG_PITCH      512u
#define SATURN_VDP2_BG_VIS_W      320u
#define SATURN_VDP2_BG_VIS_H      224u

saturn_result_t saturn_vdp2_bg_init     (void);
saturn_result_t saturn_vdp2_bg_set_image(const uint16_t* rgb555,
                                         uint16_t w, uint16_t h);
void            saturn_vdp2_bg_clear    (uint16_t rgb555);
void            saturn_vdp2_bg_enable   (int on);

/* True iff saturn_vdp2_bg_init() has been called and NBG1 should be
 * enabled in the auto-disp flags. The lobby's main_saturn.c checks this
 * before composing the slScrAutoDisp() argument. */
int             saturn_vdp2_bg_is_enabled(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_VDP2_BG_H */
