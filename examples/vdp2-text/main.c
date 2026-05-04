/*
 * examples/vdp2-text - print "hello, saturn" on NBG0 cell-mode.
 *
 * Smallest possible saturn-vdp2 consumer: init the lib, clear, print
 * one string, end_frame; repeat. The Saturn PAL flushes through SGL
 * at end_frame.
 */

#include <sgl_defs.h>

#include <saturn_vdp2.h>
#include <saturn_vdp2/saturn.h>

int main(void)
{
    slInitSystem(TV_320x224, NULL, 1);
    slInitSynch();

    saturn_vdp2_register_saturn_pal();
    saturn_vdp2_init();

    while (1) {
        slSynch();

        saturn_vdp2_begin_frame();
        saturn_vdp2_clear(/*rgb555*/ 0x0000);
        saturn_vdp2_print(/*col*/ 13, /*row*/ 13, /*palette*/ 1,
                          "hello, saturn");
        saturn_vdp2_end_frame();
    }

    return 0;
}
