/*
 * libs/saturn-vdp2 — minimal VDP2 surface for the lobby project.
 *
 * What this exposes:
 *   - Init/shutdown
 *   - Clear background to a colour
 *   - Print short ASCII text strings at (col, row) on NBG0
 *   - Begin/end frame mirrors vdp1
 *
 * v1 needs nothing more. NBG1/2/3, RBG layers, scroll, char/cell tables,
 * line scroll, etc. are future work.
 */

#ifndef SATURN_VDP2_H
#define SATURN_VDP2_H

#include <stdint.h>
#include <stddef.h>
#include <saturn_base/result.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SATURN_VDP2_MAX_TEXTS         64u
#define SATURN_VDP2_MAX_TEXT_LEN      40u
#define SATURN_VDP2_TEXT_COLS         40u
#define SATURN_VDP2_TEXT_ROWS         28u

typedef struct saturn_vdp2_text {
    uint8_t  col;       /* 0..SATURN_VDP2_TEXT_COLS-1 */
    uint8_t  row;       /* 0..SATURN_VDP2_TEXT_ROWS-1 */
    uint8_t  palette;   /* 0..15; lib provides default ramp */
    uint8_t  len;       /* number of glyphs */
    char     text[SATURN_VDP2_MAX_TEXT_LEN];
} saturn_vdp2_text_t;

saturn_result_t saturn_vdp2_init     (void);
void            saturn_vdp2_shutdown (void);

saturn_result_t saturn_vdp2_begin_frame(void);
saturn_result_t saturn_vdp2_clear      (uint16_t background_color);
saturn_result_t saturn_vdp2_print      (uint8_t col, uint8_t row,
                                        uint8_t palette, const char* s);
saturn_result_t saturn_vdp2_end_frame  (void);

uint16_t                   saturn_vdp2_text_count(void);
const saturn_vdp2_text_t*  saturn_vdp2_texts     (void);
uint16_t                   saturn_vdp2_clear_color(void);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_VDP2_H */
