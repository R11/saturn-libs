/*
 * libs/saturn-app/widgets/keyboard — on-screen keyboard widget.
 *
 * Renders into a lobby_scene_t.texts[] slice. Configurable W×H grid of
 * cells. The default 10×4 layout fits the full glyph set:
 *   A B C D E F G H I J
 *   K L M N O P Q R S T
 *   U V W X Y Z 0 1 2 3
 *   4 5 6 7 8 9 _ . DEL OK
 *                  ^^^   (last two cells are the special tokens)
 *
 * The widget exposes its in-progress buffer plus committed/cancelled
 * flags. The owning state polls those and drives transitions.
 *
 * Bindings (edge-triggered):
 *   D-pad LEFT/RIGHT/UP/DOWN  — move cursor with wrap
 *   A                         — type glyph / activate DEL or OK
 *   B                         — backspace (always)
 *   START                     — same as activating OK
 *   C                         — cancel (only if allow_cancel)
 */

#ifndef SATURN_APP_WIDGETS_KEYBOARD_H
#define SATURN_APP_WIDGETS_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include <saturn_app/game.h>     /* lobby_input_t */
#include <saturn_app/scene.h>    /* lobby_scene_t */

#ifdef __cplusplus
extern "C" {
#endif

#define SAPP_KBD_BUF_CAP   11u   /* incl. NUL; max 10 visible chars */

typedef struct {
    uint8_t origin_col;
    uint8_t origin_row;
    uint8_t cols;
    uint8_t rows;
    uint8_t cell_w;        /* per-cell stride in screen cols (>=3 for "[X]") */
    uint8_t cell_h;        /* per-cell stride in screen rows */
    uint8_t cap_chars;     /* ≤ SAPP_KBD_BUF_CAP-1 */
    bool    allow_cancel;
} sapp_kbd_layout_t;

typedef struct {
    char    buffer[SAPP_KBD_BUF_CAP];   /* null-terminated */
    uint8_t length;
    uint8_t cur_x, cur_y;               /* cursor cell within layout grid */
    bool    committed;
    bool    cancelled;
    bool    allow_cancel;
    /* Layout config — copied from sapp_kbd_layout_t at init. */
    uint8_t origin_col, origin_row, cols, rows;
    uint8_t cell_w, cell_h;
    uint8_t cap_chars;
} sapp_kbd_t;

/* Default layout: 10×4, full-screen friendly. Stride 3×2 leaves a row of
 * vertical breathing room between keyboard rows. Origin (col 5, row 14)
 * → grid spans cols 5..34, rows 14..21 on a 40×28 screen, with the prompt
 * at row 12 and buffer at row 13. cap_chars=10. */
#define SAPP_KBD_LAYOUT_FULL(allow_cancel_)                        \
    ((sapp_kbd_layout_t){                                          \
        /* origin_col   */ 5,                                      \
        /* origin_row   */ 14,                                     \
        /* cols         */ 10,                                     \
        /* rows         */  4,                                     \
        /* cell_w       */  3,                                     \
        /* cell_h       */  2,                                     \
        /* cap_chars    */ 10,                                     \
        /* allow_cancel */ (allow_cancel_)                         \
    })

/* Right-half layout: re-grid the alphabet as 5 cols × 8 rows so it fits
 * inside cols 20..39 of the screen alongside the lobby panel on the left.
 * 5×3 = 15 cols wide, 8×2 = 16 rows tall. */
#define SAPP_KBD_LAYOUT_RIGHT_HALF(allow_cancel_)                  \
    ((sapp_kbd_layout_t){                                          \
        /* origin_col   */ 22,                                     \
        /* origin_row   */  8,                                     \
        /* cols         */  5,                                     \
        /* rows         */  8,                                     \
        /* cell_w       */  3,                                     \
        /* cell_h       */  2,                                     \
        /* cap_chars    */ 10,                                     \
        /* allow_cancel */ (allow_cancel_)                         \
    })

/* Backward-compat alias for the original default layout name. New callers
 * should pick FULL or RIGHT_HALF explicitly. */
#define SAPP_KBD_DEFAULT_LAYOUT(allow_cancel_) SAPP_KBD_LAYOUT_FULL(allow_cancel_)

/* Init the widget. `initial` is copied (truncated) into the buffer so a
 * caller can pre-populate (e.g. existing name when editing). NULL or ""
 * leaves the buffer empty. The cursor starts at (0,0); committed/cancelled
 * start false. */
void sapp_kbd_init(sapp_kbd_t* k,
                   const char* initial,
                   sapp_kbd_layout_t layout);

/* Consume one frame of input. `now` is the current button bitmask;
 * `prev` is the previous frame's bitmask. Edge-triggered: only buttons
 * present in `now & ~prev` are acted on. */
void sapp_kbd_input(sapp_kbd_t* k,
                    lobby_input_t now,
                    lobby_input_t prev);

/* Append the keyboard's display (prompt, current buffer, glyph grid with
 * cursor highlight) to the scene's text array. The prompt is centered on
 * the layout columns at row (origin_row - 2); the buffer is shown at
 * (origin_row - 1). One scene text entry is added per glyph cell. */
void sapp_kbd_render(const sapp_kbd_t* k,
                     lobby_scene_t* scene,
                     const char* prompt);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURN_APP_WIDGETS_KEYBOARD_H */
