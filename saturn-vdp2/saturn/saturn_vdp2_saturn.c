/*
 * libs/saturn-vdp2/saturn — NBG0 cell-mode text + back-screen colour.
 *
 * Sets up NBG0 once at boot:
 *   - converts saturn_font_8x8 (libs/saturn-base) into 4bpp character
 *     patterns and copies them into VDP2 VRAM A0
 *   - writes a 16-colour palette into CRAM, with palette banks 0..3
 *     used for our text colour roles (default / highlight / dim / etc.)
 *   - configures NBG0 with slCharNbg0 / slPageNbg0 / slPlaneNbg0 /
 *     slMapNbg0
 *
 * NBG0 is enabled by the caller via slScrAutoDisp(NBG0ON) after init,
 * not from inside this lib — see the lobby's main_saturn.c, which
 * follows the arcade-mirror boot order (slTVOff → init libs →
 * slScrAutoDisp(NBG0ON) + slPriorityNbg0(7) → slTVOn). Doing the
 * autoDisp / priority calls from main keeps register sequencing
 * explicit at the boot site.
 *
 * Per flush:
 *   - writes the back-screen colour
 *   - dirty-cell update on the pattern name table: only cells that
 *     were stamped LAST frame and aren't being stamped THIS frame are
 *     zeroed, then this frame's cells are stamped. The full 8 KB PNT
 *     is never bulk-wiped during the display period — VDP2 reads
 *     those words live for visible scanlines, and a wholesale wipe
 *     races VDP2 fetches and produces the strobe / drop-out we were
 *     seeing. Reference pattern: arcade/lib/saturn/vdp2/saturn_vdp2.c
 *     saturn_rect_layer_clear.
 *
 * Single source of truth for the font: libs/saturn-base/font_8x8.c.
 */

#include <saturn_vdp2.h>
#include <saturn_vdp2/pal.h>

#include "sgl_defs.h"
#include <saturn_base/font_8x8.h>

#include <string.h>

/* ---------------------------------------------------------------------------
 * VRAM layout (NBG0 territory)
 *
 * VRAM_A0 (0x25E00000):
 *   0x00000 .. 0x00BDF   font character patterns (95 chars * 32 bytes)
 *   0x04000 .. 0x05FFF   pattern name table (1 page = 64x64 cells, 1-word)
 *
 * Each 4bpp 8x8 character = 32 bytes. Patterns fit in the first ~3KB.
 * The pattern name table needs 64*64*2 = 8192 bytes; placing it at
 * +0x4000 leaves room ahead of it for future char-data growth.
 * ------------------------------------------------------------------------- */

#define NBG0_CHAR_BASE   ((void*)(VDP2_VRAM_A0 + 0x00000))
#define NBG0_PNT_BASE    ((void*)(VDP2_VRAM_A0 + 0x04000))
#define NBG0_PNT_WORDS   (64u * 64u)

/* Palette banks for text colour. Each bank is 16 CRAM entries; the
 * pattern name's high nibble selects which bank the 4bpp pixel indices
 * map into. We use entries 1..N within each bank for the font foreground
 * colour; entry 0 is transparent (background shows through). */
#define PAL_BANK_DEFAULT   0u   /* white */
#define PAL_BANK_HIGHLIGHT 1u   /* warm yellow */
#define PAL_BANK_DIM       2u   /* dim grey */
#define PAL_BANK_GAME      3u   /* per-game accent */

/* Dirty-cell tracking. g_dirty_idx[] holds the PNT word indices stamped
 * by the last sat_flush(); g_dirty_n is its length. On the next flush
 * we walk this list and zero any cell that isn't stamped again this
 * frame, then stamp the new frame's cells. The CPU-side g_pnt_shadow[]
 * mirrors what we believe is in VDP2 VRAM right now, so the diff is a
 * straight equality test per cell. */
static Uint16 g_pnt_shadow[NBG0_PNT_WORDS];
static Uint16 g_dirty_idx[NBG0_PNT_WORDS];
static Uint32 g_dirty_n;

static volatile Uint16* cram_ptr(void)
{
    return (volatile Uint16*)VDP2_COLRAM;
}

static void install_palette(void)
{
    volatile Uint16* cram = cram_ptr();
    int i;

    /* Initialise all 64 entries we touch to transparent. */
    for (i = 0; i < 64; ++i) cram[i] = 0;

    /* Bank 0 — default text (white-ish on dark). */
    cram[PAL_BANK_DEFAULT * 16 + 0]  = 0;                  /* transparent */
    cram[PAL_BANK_DEFAULT * 16 + 15] = C_RGB(28, 30, 31);  /* near white */

    /* Bank 1 — highlight (warm yellow). */
    cram[PAL_BANK_HIGHLIGHT * 16 + 0]  = 0;
    cram[PAL_BANK_HIGHLIGHT * 16 + 15] = C_RGB(31, 28, 12);

    /* Bank 2 — dim (muted grey). */
    cram[PAL_BANK_DIM * 16 + 0]  = 0;
    cram[PAL_BANK_DIM * 16 + 15] = C_RGB(14, 16, 20);

    /* Bank 3 — game-specific accent (currently a soft cyan). */
    cram[PAL_BANK_GAME * 16 + 0]  = 0;
    cram[PAL_BANK_GAME * 16 + 15] = C_RGB(12, 28, 31);
}

static void install_font_chars(void)
{
    /* Render with palette index 15 as foreground. The pattern name
     * entry's palette-bank field then re-routes those pixels to whichever
     * bank the text is using. */
    saturn_font_to_4bpp(15, (uint8_t*)NBG0_CHAR_BASE);
}

/* One-shot zero of the entire PNT — used only at init, when NBG0 is
 * disabled (slTVOff() is in effect at the boot site). Never call this
 * during the display period. */
static void initial_clear_pnt(void)
{
    volatile Uint16* pnt = (volatile Uint16*)NBG0_PNT_BASE;
    Uint32 i;
    for (i = 0; i < NBG0_PNT_WORDS; ++i) {
        pnt[i] = 0;
        g_pnt_shadow[i] = 0;
    }
    g_dirty_n = 0;
}

static Uint16 pnt_entry(Uint16 char_index, Uint8 palette_bank)
{
    /* PNB_1WORD | CN_10BIT layout:
     *   bits 0..9   = character index (0..1023)
     *   bits 10,11  = V/H flip (we don't use)
     *   bits 12..15 = palette bank select (combined with the 4bpp pixel
     *                 to produce the final 8-bit colour index in CRAM)
     */
    return (Uint16)(((palette_bank & 0x0Fu) << 12) | (char_index & 0x3FFu));
}

/* ---------------------------------------------------------------------------
 * PAL callbacks
 * ------------------------------------------------------------------------- */

static saturn_result_t sat_init(void* ctx)
{
    (void)ctx;

    /* Build CRAM and character data BEFORE the caller enables NBG0.
     * Caller is responsible for slTVOff() around init and the
     * slScrAutoDisp(NBG0ON) + slPriorityNbg0(7) + slTVOn() sequence
     * after — see the lobby's main_saturn.c. */
    install_palette();
    install_font_chars();
    initial_clear_pnt();

    /* NBG0 in 16-colour, 8x8-cell mode. Pattern data at NBG0_CHAR_BASE,
     * pattern name table at NBG0_PNT_BASE, single 1x1 page plane. */
    slCharNbg0(COL_TYPE_16, CHAR_SIZE_1x1);
    slPageNbg0(NBG0_CHAR_BASE, NULL, PNB_1WORD | CN_10BIT);
    slPlaneNbg0(PL_SIZE_1x1);
    slMapNbg0(NBG0_PNT_BASE, NBG0_PNT_BASE, NBG0_PNT_BASE, NBG0_PNT_BASE);

    /* NBG0 priority + auto-display are intentionally NOT set here.
     * main_saturn.c calls slPriorityNbg0(7) and slScrAutoDisp(NBG0ON)
     * at the right point in the arcade-mirror boot order, after libs
     * are initialised and before slTVOn(). */

    return SATURN_OK;
}

static void sat_shutdown(void* ctx) { (void)ctx; }

static saturn_result_t sat_flush(void* ctx,
                                 uint16_t bg_color,
                                 const saturn_vdp2_text_t* texts,
                                 uint16_t n)
{
    volatile Uint16* pnt = (volatile Uint16*)NBG0_PNT_BASE;
    /* Per-frame staging so we can diff against the previous frame.
     * Indexed by PNT word index (row*64 + col), value is the desired
     * pnt_entry for this frame, or 0 for "blank". 8 KB on stack would
     * be fine on Saturn but we keep it static to avoid stack churn. */
    static Uint16 g_frame_buf[NBG0_PNT_WORDS];
    static Uint16 g_frame_idx[NBG0_PNT_WORDS];
    Uint32  frame_n = 0;
    Uint32  k;
    uint16_t i;
    uint8_t  c;

    (void)ctx;

    /* Set back-screen colour (visible behind NBG0's transparent pixels). */
    slBack1ColSet(BACK_CRAM, bg_color);

    /* Stage this frame's PNT writes into g_frame_buf. We only mark
     * indices we touch (in g_frame_idx) so the union of last-frame +
     * this-frame stays small. */
    for (i = 0; i < n; ++i) {
        const saturn_vdp2_text_t* t = &texts[i];
        Uint8  bank = (Uint8)(t->palette & 0x0Fu);
        Uint16 base = (Uint16)(t->row * 64u + t->col);
        for (c = 0; c < t->len; ++c) {
            int ch  = (unsigned char)t->text[c];
            int idx = ch - SATURN_FONT_FIRST_CHAR;
            Uint16 cell;
            Uint16 pos = (Uint16)(base + c);
            if (idx < 0 || idx >= SATURN_FONT_CHAR_COUNT) idx = 0; /* space */
            cell = pnt_entry((Uint16)idx, bank);

            /* If this position was already stamped earlier this frame
             * (e.g. two overlapping texts), keep the later value — same
             * effect as the old bulk-clear-then-stamp. */
            if (g_frame_buf[pos] == 0) {
                g_frame_idx[frame_n++] = pos;
            }
            g_frame_buf[pos] = cell;
        }
    }

    /* Walk last frame's stamps: any cell not present in this frame
     * (g_frame_buf == 0 there) needs zeroing in VRAM + shadow. */
    for (k = 0; k < g_dirty_n; ++k) {
        Uint16 pos = g_dirty_idx[k];
        if (g_frame_buf[pos] == 0 && g_pnt_shadow[pos] != 0) {
            pnt[pos] = 0;
            g_pnt_shadow[pos] = 0;
        }
    }

    /* Stamp this frame's cells: write only the ones whose value
     * actually changed vs the shadow. */
    for (k = 0; k < frame_n; ++k) {
        Uint16 pos  = g_frame_idx[k];
        Uint16 cell = g_frame_buf[pos];
        if (g_pnt_shadow[pos] != cell) {
            pnt[pos] = cell;
            g_pnt_shadow[pos] = cell;
        }
    }

    /* Hand the staged frame to next flush as "last frame's dirty list"
     * and reset g_frame_buf for those indices. */
    g_dirty_n = frame_n;
    for (k = 0; k < frame_n; ++k) {
        Uint16 pos = g_frame_idx[k];
        g_dirty_idx[k] = pos;
        g_frame_buf[pos] = 0;          /* reset for next frame's staging */
    }

    return SATURN_OK;
}

/* ---------------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------------- */

static const saturn_vdp2_pal_t k_pal = {
    sat_init, sat_shutdown, sat_flush, NULL
};

void saturn_vdp2_register_saturn_pal(void)
{
    saturn_vdp2_install_pal(&k_pal);
}
