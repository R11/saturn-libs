/*
 * libs/saturn-vdp1/pal.h — platform abstraction.
 *
 * Platform shells (saturn/, web/, host) install a PAL whose flush_quads
 * callback consumes the per-frame command list and draws it.
 */

#ifndef SATURN_VDP1_PAL_H
#define SATURN_VDP1_PAL_H

#include <stdint.h>
#include <saturn_base/result.h>
#include <saturn_vdp1.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct saturn_vdp1_pal {
    saturn_result_t (*init)    (void* ctx, uint16_t w, uint16_t h);
    void            (*shutdown)(void* ctx);
    /* Called once per end_frame() with the full quad list. */
    saturn_result_t (*flush)   (void* ctx,
                                const saturn_vdp1_quad_t* quads,
                                uint16_t n);
    void* ctx;
} saturn_vdp1_pal_t;

void saturn_vdp1_install_pal(const saturn_vdp1_pal_t* pal);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_VDP1_PAL_H */
