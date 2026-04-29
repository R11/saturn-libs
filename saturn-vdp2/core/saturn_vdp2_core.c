#include <saturn_vdp2.h>
#include <saturn_vdp2/pal.h>

#include <string.h>

static const saturn_vdp2_pal_t* g_pal;
static int                      g_initialised;
static int                      g_in_frame;

static uint16_t                 g_bg_color;
static saturn_vdp2_text_t       g_texts[SATURN_VDP2_MAX_TEXTS];
static uint16_t                 g_n_texts;

void saturn_vdp2_install_pal(const saturn_vdp2_pal_t* pal)
{
    g_pal = pal;
    g_initialised = 0;
}

saturn_result_t saturn_vdp2_init(void)
{
    saturn_result_t r;
    if (!g_pal || !g_pal->init) return SATURN_ERR_NOT_READY;
    r = g_pal->init(g_pal->ctx);
    if (r != SATURN_OK) return r;
    g_bg_color    = 0;
    g_n_texts     = 0;
    g_in_frame    = 0;
    g_initialised = 1;
    return SATURN_OK;
}

void saturn_vdp2_shutdown(void)
{
    if (!g_initialised) return;
    if (g_pal && g_pal->shutdown) g_pal->shutdown(g_pal->ctx);
    g_initialised = 0;
}

saturn_result_t saturn_vdp2_begin_frame(void)
{
    if (!g_initialised) return SATURN_ERR_NOT_READY;
    g_in_frame = 1;
    g_n_texts  = 0;
    g_bg_color = 0;
    return SATURN_OK;
}

saturn_result_t saturn_vdp2_clear(uint16_t color)
{
    if (!g_initialised || !g_in_frame) return SATURN_ERR_NOT_READY;
    g_bg_color = color;
    return SATURN_OK;
}

saturn_result_t saturn_vdp2_print(uint8_t col, uint8_t row,
                                  uint8_t palette, const char* s)
{
    saturn_vdp2_text_t* t;
    size_t              len = 0;
    if (!g_initialised || !g_in_frame)        return SATURN_ERR_NOT_READY;
    if (!s)                                    return SATURN_ERR_INVALID;
    if (col >= SATURN_VDP2_TEXT_COLS
     || row >= SATURN_VDP2_TEXT_ROWS)          return SATURN_ERR_INVALID;
    if (g_n_texts >= SATURN_VDP2_MAX_TEXTS)    return SATURN_ERR_NO_SPACE;

    while (s[len] && len < SATURN_VDP2_MAX_TEXT_LEN) len++;
    /* Truncate to remaining columns from `col` to keep things simple. */
    if (col + len > SATURN_VDP2_TEXT_COLS) len = SATURN_VDP2_TEXT_COLS - col;

    t = &g_texts[g_n_texts++];
    t->col     = col;
    t->row     = row;
    t->palette = palette;
    t->len     = (uint8_t)len;
    memcpy(t->text, s, len);
    if (len < SATURN_VDP2_MAX_TEXT_LEN) t->text[len] = '\0';
    return SATURN_OK;
}

saturn_result_t saturn_vdp2_end_frame(void)
{
    saturn_result_t r = SATURN_OK;
    if (!g_initialised || !g_in_frame) return SATURN_ERR_NOT_READY;
    g_in_frame = 0;
    if (g_pal && g_pal->flush) {
        r = g_pal->flush(g_pal->ctx, g_bg_color, g_texts, g_n_texts);
    }
    return r;
}

uint16_t                   saturn_vdp2_text_count(void)  { return g_n_texts; }
const saturn_vdp2_text_t*  saturn_vdp2_texts(void)       { return g_texts; }
uint16_t                   saturn_vdp2_clear_color(void) { return g_bg_color; }
