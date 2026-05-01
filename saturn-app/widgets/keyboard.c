/*
 * libs/saturn-app/widgets/keyboard — implementation.
 *
 * Glyph set is row-major across the configured grid. The last two cells
 * are always the DEL and OK tokens regardless of grid size; everything
 * before them is the printable glyph alphabet. Any cell past the end of
 * the alphabet (and before DEL/OK) renders blank and behaves as a no-op.
 *
 * The default 10×4 layout fits the canonical alphabet exactly:
 *   row 0:  A B C D E F G H I J
 *   row 1:  K L M N O P Q R S T
 *   row 2:  U V W X Y Z 0 1 2 3
 *   row 3:  4 5 6 7 8 9 _   DEL OK   (one trailing blank before DEL/OK)
 */

#include <saturn_app/widgets/keyboard.h>

#include <string.h>

/* Mirror saturn-smpc button bits (also mirrored in saturn_app_core.c) so
 * this widget has no library dependency on saturn-smpc. */
#define KBD_BTN_RIGHT   0x0001
#define KBD_BTN_LEFT    0x0002
#define KBD_BTN_DOWN    0x0004
#define KBD_BTN_UP      0x0008
#define KBD_BTN_START   0x0010
#define KBD_BTN_A       0x0020
#define KBD_BTN_B       0x0040
#define KBD_BTN_C       0x0080

/* Palette indices reused from saturn_app_core.c. PAL_DEFAULT = normal,
 * PAL_HIGHLIGHT = cursor cell. */
#define KBD_PAL_DEFAULT    0
#define KBD_PAL_HIGHLIGHT  1
#define KBD_PAL_DIM        2

/* Printable glyph alphabet, in cell order (left-to-right, top-to-bottom).
 * 26 letters + 10 digits + underscore = 37. */
static const char k_alphabet[] =
    "ABCDEFGHIJ"
    "KLMNOPQRST"
    "UVWXYZ0123"
    "456789_";

#define KBD_ALPHABET_LEN  (sizeof(k_alphabet) - 1)   /* 37 */

/* Cell-token discrimination. */
typedef enum {
    KBD_CELL_BLANK = 0,
    KBD_CELL_GLYPH,
    KBD_CELL_DEL,
    KBD_CELL_OK
} kbd_cell_kind_t;

/* Compute the total number of cells in the layout grid. */
static unsigned kbd_total_cells(const sapp_kbd_t* k) {
    return (unsigned)k->cols * (unsigned)k->rows;
}

/* Cell index from (cur_x, cur_y). */
static unsigned kbd_cell_index(const sapp_kbd_t* k, uint8_t x, uint8_t y) {
    return (unsigned)y * (unsigned)k->cols + (unsigned)x;
}

/* Classify a cell by its index in the layout. */
static kbd_cell_kind_t kbd_cell_at(const sapp_kbd_t* k, unsigned idx,
                                   char* out_glyph) {
    unsigned total = kbd_total_cells(k);
    if (out_glyph) *out_glyph = ' ';
    if (idx >= total) return KBD_CELL_BLANK;
    if (idx == total - 1) return KBD_CELL_OK;
    if (idx == total - 2) return KBD_CELL_DEL;
    if (idx < KBD_ALPHABET_LEN) {
        if (out_glyph) *out_glyph = k_alphabet[idx];
        return KBD_CELL_GLYPH;
    }
    return KBD_CELL_BLANK;
}

/* Edge-trigger helper. */
static int kbd_pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask) {
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

static void kbd_append(sapp_kbd_t* k, char c) {
    if (k->length >= k->cap_chars) return;
    if (k->length >= SAPP_KBD_BUF_CAP - 1) return;
    k->buffer[k->length++] = c;
    k->buffer[k->length] = '\0';
}

static void kbd_backspace(sapp_kbd_t* k) {
    if (k->length == 0) return;
    k->length--;
    k->buffer[k->length] = '\0';
}

static void kbd_try_commit(sapp_kbd_t* k) {
    if (k->length > 0) k->committed = true;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void sapp_kbd_init(sapp_kbd_t* k,
                   const char* initial,
                   sapp_kbd_layout_t layout)
{
    if (!k) return;
    memset(k, 0, sizeof(*k));

    k->origin_col   = layout.origin_col;
    k->origin_row   = layout.origin_row;
    k->cols         = layout.cols ? layout.cols : 1;
    k->rows         = layout.rows ? layout.rows : 1;
    k->cell_w       = layout.cell_w ? layout.cell_w : 3;   /* "[X]" is 3 wide */
    k->cell_h       = layout.cell_h ? layout.cell_h : 1;
    if (k->cell_w < 3) k->cell_w = 3;                       /* don't overlap */
    k->cap_chars    = layout.cap_chars;
    k->allow_cancel = layout.allow_cancel;

    if (k->cap_chars == 0 || k->cap_chars > SAPP_KBD_BUF_CAP - 1) {
        k->cap_chars = SAPP_KBD_BUF_CAP - 1;
    }

    if (initial) {
        size_t i = 0;
        while (i < k->cap_chars && initial[i] != '\0') {
            k->buffer[i] = initial[i];
            i++;
        }
        k->length = (uint8_t)i;
    }
    k->buffer[k->length] = '\0';
}

void sapp_kbd_input(sapp_kbd_t* k,
                    lobby_input_t now,
                    lobby_input_t prev)
{
    if (!k) return;
    if (k->committed || k->cancelled) return;

    /* Movement with wrap. */
    if (kbd_pressed(now, prev, KBD_BTN_LEFT)) {
        k->cur_x = (k->cur_x == 0) ? (uint8_t)(k->cols - 1) : (uint8_t)(k->cur_x - 1);
    }
    if (kbd_pressed(now, prev, KBD_BTN_RIGHT)) {
        k->cur_x = (uint8_t)((k->cur_x + 1) % k->cols);
    }
    if (kbd_pressed(now, prev, KBD_BTN_UP)) {
        k->cur_y = (k->cur_y == 0) ? (uint8_t)(k->rows - 1) : (uint8_t)(k->cur_y - 1);
    }
    if (kbd_pressed(now, prev, KBD_BTN_DOWN)) {
        k->cur_y = (uint8_t)((k->cur_y + 1) % k->rows);
    }

    /* B = backspace, always. */
    if (kbd_pressed(now, prev, KBD_BTN_B)) {
        kbd_backspace(k);
    }

    /* START = OK. */
    if (kbd_pressed(now, prev, KBD_BTN_START)) {
        kbd_try_commit(k);
    }

    /* C = cancel (gated). */
    if (kbd_pressed(now, prev, KBD_BTN_C) && k->allow_cancel) {
        k->cancelled = true;
    }

    /* A activates the cell under the cursor. */
    if (kbd_pressed(now, prev, KBD_BTN_A)) {
        char g = ' ';
        unsigned idx = kbd_cell_index(k, k->cur_x, k->cur_y);
        kbd_cell_kind_t kind = kbd_cell_at(k, idx, &g);
        switch (kind) {
        case KBD_CELL_GLYPH: kbd_append(k, g);   break;
        case KBD_CELL_DEL:   kbd_backspace(k);   break;
        case KBD_CELL_OK:    kbd_try_commit(k);  break;
        case KBD_CELL_BLANK: /* no-op */         break;
        }
    }
}

/* Helper: write a small formatted glyph cell into the scene. The cursor
 * cell is rendered as "[X]" (3 chars), non-cursor cells as " X " for
 * visual alignment. DEL/OK get their multi-char names, with the cursor
 * indicator wrapping them. */
static void render_cell(lobby_scene_t* scene,
                        uint8_t col, uint8_t row,
                        bool highlighted,
                        const char* label)
{
    char buf[8];
    size_t i = 0;
    size_t llen = strlen(label);
    if (llen > 4) llen = 4;
    buf[i++] = highlighted ? '[' : ' ';
    for (size_t j = 0; j < llen; ++j) buf[i++] = label[j];
    buf[i++] = highlighted ? ']' : ' ';
    buf[i] = '\0';
    lobby_scene_text(scene, col, row,
                     highlighted ? KBD_PAL_HIGHLIGHT : KBD_PAL_DEFAULT,
                     buf);
}

void sapp_kbd_render(const sapp_kbd_t* k,
                     lobby_scene_t* scene,
                     const char* prompt)
{
    if (!k || !scene) return;

    /* Prompt — row above buffer. Centered on the layout columns. */
    if (prompt && prompt[0] != '\0' && k->origin_row >= 2) {
        size_t plen = strlen(prompt);
        unsigned grid_w = (unsigned)k->cols * (unsigned)k->cell_w;
        uint8_t pcol = k->origin_col;
        if (plen < grid_w) pcol = (uint8_t)(k->origin_col + (grid_w - plen) / 2u);
        lobby_scene_text(scene, pcol, (uint8_t)(k->origin_row - 2),
                         KBD_PAL_DEFAULT, prompt);
    }

    /* Buffer with trailing-underscore caret if there's room. */
    if (k->origin_row >= 1) {
        char show[SAPP_KBD_BUF_CAP + 1];
        size_t i;
        for (i = 0; i < k->length && i < SAPP_KBD_BUF_CAP; ++i) {
            show[i] = k->buffer[i];
        }
        if (k->length < k->cap_chars && i < SAPP_KBD_BUF_CAP) {
            show[i++] = '_';
        }
        show[i] = '\0';
        lobby_scene_text(scene, k->origin_col, (uint8_t)(k->origin_row - 1),
                         KBD_PAL_HIGHLIGHT, show);
    }

    /* Glyph grid. Each cell occupies 3 screen columns. */
    {
        uint8_t y, x;
        for (y = 0; y < k->rows; ++y) {
            for (x = 0; x < k->cols; ++x) {
                unsigned idx = kbd_cell_index(k, x, y);
                char glyph = ' ';
                kbd_cell_kind_t kind = kbd_cell_at(k, idx, &glyph);
                bool hl = (x == k->cur_x && y == k->cur_y);

                uint8_t col = (uint8_t)(k->origin_col + x * k->cell_w);
                uint8_t row = (uint8_t)(k->origin_row + y * k->cell_h);

                if (scene->n_texts >= LOBBY_SCENE_MAX_TEXTS) return;

                switch (kind) {
                case KBD_CELL_GLYPH: {
                    char one[2] = { glyph, '\0' };
                    render_cell(scene, col, row, hl, one);
                    break;
                }
                case KBD_CELL_DEL:
                    render_cell(scene, col, row, hl, "DEL");
                    break;
                case KBD_CELL_OK:
                    render_cell(scene, col, row, hl, "OK");
                    break;
                case KBD_CELL_BLANK:
                    if (hl) render_cell(scene, col, row, true, " ");
                    break;
                }
            }
        }
    }
}
