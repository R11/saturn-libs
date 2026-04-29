/*
 * libs/saturn-vdp1/core — portable command-list machinery.
 *
 * Holds the per-frame quad list plus the screen size and the installed
 * PAL pointer. Tests link this without any saturn/ shell.
 */

#include <saturn_vdp1.h>
#include <saturn_vdp1/pal.h>

#include <string.h>

static const saturn_vdp1_pal_t* g_pal;
static int                      g_initialised;

static uint16_t                 g_screen_w;
static uint16_t                 g_screen_h;

static saturn_vdp1_quad_t       g_quads[SATURN_VDP1_MAX_QUADS];
static uint16_t                 g_n_quads;
static int                      g_in_frame;

void saturn_vdp1_install_pal(const saturn_vdp1_pal_t* pal)
{
    g_pal         = pal;
    g_initialised = 0;
}

saturn_result_t saturn_vdp1_init(uint16_t screen_w, uint16_t screen_h)
{
    saturn_result_t r;

    if (!g_pal || !g_pal->init) return SATURN_ERR_NOT_READY;
    if (screen_w == 0 || screen_h == 0) return SATURN_ERR_INVALID;

    r = g_pal->init(g_pal->ctx, screen_w, screen_h);
    if (r != SATURN_OK) return r;

    g_screen_w    = screen_w;
    g_screen_h    = screen_h;
    g_n_quads     = 0;
    g_in_frame    = 0;
    g_initialised = 1;
    return SATURN_OK;
}

void saturn_vdp1_shutdown(void)
{
    if (!g_initialised) return;
    if (g_pal && g_pal->shutdown) g_pal->shutdown(g_pal->ctx);
    g_initialised = 0;
    g_in_frame    = 0;
    g_n_quads     = 0;
}

saturn_result_t saturn_vdp1_begin_frame(void)
{
    if (!g_initialised) return SATURN_ERR_NOT_READY;
    g_n_quads  = 0;
    g_in_frame = 1;
    return SATURN_OK;
}

saturn_result_t saturn_vdp1_submit_quad(int16_t x, int16_t y,
                                        uint16_t w, uint16_t h,
                                        saturn_vdp1_color_t color)
{
    saturn_vdp1_quad_t* q;
    if (!g_initialised || !g_in_frame)             return SATURN_ERR_NOT_READY;
    if (w == 0 || h == 0)                          return SATURN_ERR_INVALID;
    if (g_n_quads >= SATURN_VDP1_MAX_QUADS)        return SATURN_ERR_NO_SPACE;

    q = &g_quads[g_n_quads++];
    q->x = x; q->y = y; q->w = w; q->h = h; q->color = color;
    return SATURN_OK;
}

saturn_result_t saturn_vdp1_end_frame(void)
{
    saturn_result_t r = SATURN_OK;
    if (!g_initialised || !g_in_frame)             return SATURN_ERR_NOT_READY;
    g_in_frame = 0;
    if (g_pal && g_pal->flush) {
        r = g_pal->flush(g_pal->ctx, g_quads, g_n_quads);
    }
    return r;
}

uint16_t saturn_vdp1_quad_count(void)             { return g_n_quads; }
const saturn_vdp1_quad_t* saturn_vdp1_quads(void) { return g_quads; }
uint16_t saturn_vdp1_screen_width (void)          { return g_screen_w; }
uint16_t saturn_vdp1_screen_height(void)          { return g_screen_h; }
