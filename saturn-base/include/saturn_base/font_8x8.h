/*
 * libs/saturn-base/font_8x8 — 8x8 pixel ASCII font shared by Saturn and
 * web. 1bpp source layout: 95 glyphs (ASCII 32..126), 8 bytes each, MSB
 * is the leftmost pixel of each row. Total 760 bytes.
 *
 * Saturn renders this through saturn-vdp2 by converting to 4bpp NBG0
 * character patterns. The web frontend ships the same byte array as a
 * JS const and stamps glyphs pixel-by-pixel onto canvas.
 */

#ifndef SATURN_BASE_FONT_8X8_H
#define SATURN_BASE_FONT_8X8_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SATURN_FONT_FIRST_CHAR  32   /* ' '  */
#define SATURN_FONT_LAST_CHAR  126   /* '~'  */
#define SATURN_FONT_CHAR_COUNT  95
#define SATURN_FONT_CHAR_W       8
#define SATURN_FONT_CHAR_H       8
#define SATURN_FONT_BYTES_1BPP  (SATURN_FONT_CHAR_COUNT * SATURN_FONT_CHAR_H)

/* 1bpp font data. Index = (ch - SATURN_FONT_FIRST_CHAR) * 8 + row.
 * Each byte: pixel mask with MSB = leftmost. */
extern const uint8_t saturn_font_8x8[SATURN_FONT_BYTES_1BPP];

/* Helper: pointer to first row of a glyph, or NULL when ch is out of
 * range. Out-of-range chars should be drawn as space by the caller. */
const uint8_t* saturn_font_glyph(int ch);

/* Convert one 1bpp row into a 4bpp big-endian-nibble row (4 bytes).
 * Foreground pixels become palette index `fg`; background is 0. Used by
 * the saturn-vdp2 shell to fill NBG0 character RAM at boot. */
void saturn_font_row_to_4bpp(uint8_t src, uint8_t fg, uint8_t out[4]);

/* Convert the full font into 4bpp character patterns. dst must be at
 * least SATURN_FONT_CHAR_COUNT * 32 bytes (8 rows * 4 bytes per row). */
void saturn_font_to_4bpp(uint8_t fg, uint8_t* dst);

#ifdef __cplusplus
}
#endif
#endif
