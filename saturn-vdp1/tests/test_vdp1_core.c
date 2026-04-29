/*
 * Host tests for saturn-vdp1 core. Drives the lib through a stub PAL
 * that records flush() calls.
 */

#include <saturn_test/test.h>
#include <saturn_vdp1.h>
#include <saturn_vdp1/pal.h>
#include <string.h>

static int      g_flush_calls;
static uint16_t g_last_count;
static saturn_vdp1_quad_t g_last_quads[SATURN_VDP1_MAX_QUADS];

static saturn_result_t stub_init(void* c, uint16_t w, uint16_t h) {
    (void)c; (void)w; (void)h; return SATURN_OK;
}
static void stub_shutdown(void* c) { (void)c; }
static saturn_result_t stub_flush(void* c, const saturn_vdp1_quad_t* q, uint16_t n) {
    (void)c;
    g_flush_calls++;
    g_last_count = n;
    memcpy(g_last_quads, q, n * sizeof(*q));
    return SATURN_OK;
}
static const saturn_vdp1_pal_t k_pal = { stub_init, stub_shutdown, stub_flush, NULL };

static void setup(void) {
    g_flush_calls = 0;
    g_last_count = 0;
    saturn_vdp1_install_pal(&k_pal);
    saturn_vdp1_init(320, 224);
}
static void teardown(void) {
    saturn_vdp1_shutdown();
    saturn_vdp1_install_pal(NULL);
}
SATURN_TEST_FIXTURE(setup, teardown);

SATURN_TEST(vdp1_init_records_screen_size) {
    SATURN_ASSERT_EQ(saturn_vdp1_screen_width(),  320);
    SATURN_ASSERT_EQ(saturn_vdp1_screen_height(), 224);
}

SATURN_TEST(vdp1_init_zero_dim_rejected) {
    /* Already init'd by fixture; install fresh and try with bad args */
    saturn_vdp1_shutdown();
    saturn_vdp1_install_pal(&k_pal);
    SATURN_ASSERT_EQ(saturn_vdp1_init(0, 224),    SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp1_init(320, 0),    SATURN_ERR_INVALID);
}

SATURN_TEST(vdp1_submit_outside_frame_errors) {
    SATURN_ASSERT_EQ(saturn_vdp1_submit_quad(0, 0, 8, 8, 0),
                     SATURN_ERR_NOT_READY);
}

SATURN_TEST(vdp1_begin_resets_count) {
    SATURN_ASSERT_OK(saturn_vdp1_begin_frame());
    SATURN_ASSERT_OK(saturn_vdp1_submit_quad(1, 2, 8, 8, 0xFFFF));
    SATURN_ASSERT_EQ(saturn_vdp1_quad_count(), 1);
    SATURN_ASSERT_OK(saturn_vdp1_begin_frame());
    SATURN_ASSERT_EQ(saturn_vdp1_quad_count(), 0);
}

SATURN_TEST(vdp1_quads_in_order) {
    SATURN_ASSERT_OK(saturn_vdp1_begin_frame());
    SATURN_ASSERT_OK(saturn_vdp1_submit_quad(10, 20, 8, 8, 0x8001));
    SATURN_ASSERT_OK(saturn_vdp1_submit_quad(30, 40, 16, 4, 0x83FF));
    SATURN_ASSERT_EQ(saturn_vdp1_quad_count(), 2);
    const saturn_vdp1_quad_t* q = saturn_vdp1_quads();
    SATURN_ASSERT_EQ(q[0].x, 10); SATURN_ASSERT_EQ(q[0].y, 20);
    SATURN_ASSERT_EQ(q[0].w, 8);  SATURN_ASSERT_EQ(q[0].h, 8);
    SATURN_ASSERT_EQ(q[0].color, 0x8001);
    SATURN_ASSERT_EQ(q[1].x, 30); SATURN_ASSERT_EQ(q[1].y, 40);
    SATURN_ASSERT_EQ(q[1].w, 16); SATURN_ASSERT_EQ(q[1].h, 4);
}

SATURN_TEST(vdp1_zero_size_quad_rejected) {
    SATURN_ASSERT_OK(saturn_vdp1_begin_frame());
    SATURN_ASSERT_EQ(saturn_vdp1_submit_quad(0, 0, 0, 8, 0xFFFF),
                     SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp1_submit_quad(0, 0, 8, 0, 0xFFFF),
                     SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp1_quad_count(), 0);
}

SATURN_TEST(vdp1_end_frame_calls_pal_flush) {
    SATURN_ASSERT_OK(saturn_vdp1_begin_frame());
    SATURN_ASSERT_OK(saturn_vdp1_submit_quad(0, 0, 8, 8, 0x8000));
    SATURN_ASSERT_OK(saturn_vdp1_end_frame());
    SATURN_ASSERT_EQ(g_flush_calls, 1);
    SATURN_ASSERT_EQ(g_last_count, 1);
    SATURN_ASSERT_EQ(g_last_quads[0].w, 8);
}

SATURN_TEST(vdp1_overflow_returns_no_space) {
    unsigned i;
    SATURN_ASSERT_OK(saturn_vdp1_begin_frame());
    for (i = 0; i < SATURN_VDP1_MAX_QUADS; ++i) {
        SATURN_ASSERT_OK(saturn_vdp1_submit_quad(0, 0, 1, 1, 0x8000));
    }
    SATURN_ASSERT_EQ(saturn_vdp1_submit_quad(0, 0, 1, 1, 0x8000),
                     SATURN_ERR_NO_SPACE);
}

SATURN_TEST(vdp1_rgb_helper_packs_correctly) {
    /* white -> all 5-bit components saturated, opaque bit set */
    SATURN_ASSERT_EQ(saturn_vdp1_rgb(255, 255, 255), (uint16_t)0xFFFFu);
    /* pure red */
    SATURN_ASSERT_EQ(saturn_vdp1_rgb(255, 0, 0),     (uint16_t)0x801Fu);
    /* pure green */
    SATURN_ASSERT_EQ(saturn_vdp1_rgb(0, 255, 0),     (uint16_t)0x83E0u);
    /* pure blue */
    SATURN_ASSERT_EQ(saturn_vdp1_rgb(0, 0, 255),     (uint16_t)0xFC00u);
}
