/*
 * libs/saturn-vdp2/core — host stub for the NBG1 bitmap helper.
 *
 * On host builds we have no VDP2 VRAM. Calls are recorded into module-
 * static counters/snapshots so tests can assert behaviour. The Saturn
 * shell at saturn/saturn_vdp2_bg_saturn.c implements the same API
 * against real hardware and is excluded from host builds.
 */

#include <saturn_vdp2/bg.h>

#include <string.h>

static int      g_inited;
static int      g_enabled;
static int      g_set_image_calls;
static int      g_clear_calls;
static const uint16_t* g_last_image;
static uint16_t g_last_w, g_last_h;
static uint16_t g_last_clear_color;

saturn_result_t saturn_vdp2_bg_init(void)
{
    g_inited           = 1;
    g_enabled          = 0;
    g_set_image_calls  = 0;
    g_clear_calls      = 0;
    g_last_image       = 0;
    g_last_w           = 0;
    g_last_h           = 0;
    g_last_clear_color = 0;
    return SATURN_OK;
}

saturn_result_t saturn_vdp2_bg_set_image(const uint16_t* rgb555,
                                         uint16_t w, uint16_t h)
{
    if (!g_inited)             return SATURN_ERR_NOT_READY;
    if (!rgb555)               return SATURN_ERR_INVALID;
    if (w == 0 || h == 0)      return SATURN_ERR_INVALID;
    if (w > SATURN_VDP2_BG_VIS_W || h > SATURN_VDP2_BG_VIS_H)
                               return SATURN_ERR_INVALID;
    g_set_image_calls++;
    g_last_image = rgb555;
    g_last_w = w;
    g_last_h = h;
    return SATURN_OK;
}

void saturn_vdp2_bg_clear(uint16_t rgb555)
{
    if (!g_inited) return;
    g_clear_calls++;
    g_last_clear_color = rgb555;
    g_last_image = 0;
}

void saturn_vdp2_bg_enable(int on)
{
    g_enabled = on ? 1 : 0;
}

int saturn_vdp2_bg_is_enabled(void)
{
    return g_inited && g_enabled;
}

/* Test-only introspection (not in the public header — tests poke
 * through with extern declarations). */
int                saturn_vdp2_bg_test_set_image_calls(void) { return g_set_image_calls; }
int                saturn_vdp2_bg_test_clear_calls    (void) { return g_clear_calls; }
const uint16_t*    saturn_vdp2_bg_test_last_image     (void) { return g_last_image; }
uint16_t           saturn_vdp2_bg_test_last_w         (void) { return g_last_w; }
uint16_t           saturn_vdp2_bg_test_last_h         (void) { return g_last_h; }
uint16_t           saturn_vdp2_bg_test_last_clear     (void) { return g_last_clear_color; }
