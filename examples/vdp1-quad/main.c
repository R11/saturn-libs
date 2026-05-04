/*
 * examples/vdp1-quad - submit one filled quad per frame.
 *
 * Smallest possible saturn-vdp1 consumer: init, register the Saturn
 * PAL, and push a coloured rectangle every frame.
 */

#include <sgl_defs.h>

#include <saturn_vdp1.h>
#include <saturn_vdp1/saturn.h>

int main(void)
{
    slInitSystem(TV_320x224, NULL, 1);
    slInitSynch();

    saturn_vdp1_register_saturn_pal();
    saturn_vdp1_init(320, 224);

    while (1) {
        slSynch();
        saturn_vdp1_patch_now();

        saturn_vdp1_begin_frame();
        saturn_vdp1_submit_quad(/*x*/ 120, /*y*/ 80,
                                /*w*/  80, /*h*/ 64,
                                saturn_vdp1_rgb(255, 64, 32));
        saturn_vdp1_end_frame();
    }

    return 0;
}
