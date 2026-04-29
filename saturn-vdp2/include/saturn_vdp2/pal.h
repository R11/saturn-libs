#ifndef SATURN_VDP2_PAL_H
#define SATURN_VDP2_PAL_H

#include <saturn_base/result.h>
#include <saturn_vdp2.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct saturn_vdp2_pal {
    saturn_result_t (*init)    (void* ctx);
    void            (*shutdown)(void* ctx);
    saturn_result_t (*flush)   (void* ctx,
                                uint16_t bg_color,
                                const saturn_vdp2_text_t* texts,
                                uint16_t n);
    void* ctx;
} saturn_vdp2_pal_t;

void saturn_vdp2_install_pal(const saturn_vdp2_pal_t* pal);

#ifdef __cplusplus
}
#endif
#endif
