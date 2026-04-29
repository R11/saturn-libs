#include <saturn_test/test.h>
#include <saturn_vdp2.h>
#include <saturn_vdp2/pal.h>
#include <string.h>

static int      g_flush_calls;
static uint16_t g_last_bg;
static uint16_t g_last_n;

static saturn_result_t init_(void* c) { (void)c; return SATURN_OK; }
static void shut_(void* c) { (void)c; }
static saturn_result_t flush_(void* c, uint16_t bg,
                              const saturn_vdp2_text_t* t, uint16_t n) {
    (void)c; (void)t;
    g_flush_calls++;
    g_last_bg = bg;
    g_last_n  = n;
    return SATURN_OK;
}
static const saturn_vdp2_pal_t k_pal = { init_, shut_, flush_, NULL };

static void setup(void)    { g_flush_calls = 0; saturn_vdp2_install_pal(&k_pal); saturn_vdp2_init(); }
static void teardown(void) { saturn_vdp2_shutdown(); saturn_vdp2_install_pal(NULL); }
SATURN_TEST_FIXTURE(setup, teardown);

SATURN_TEST(vdp2_print_records_text) {
    SATURN_ASSERT_OK(saturn_vdp2_begin_frame());
    SATURN_ASSERT_OK(saturn_vdp2_clear(0x8421));
    SATURN_ASSERT_OK(saturn_vdp2_print(2, 5, 0, "HELLO"));
    SATURN_ASSERT_EQ(saturn_vdp2_text_count(), 1);
    const saturn_vdp2_text_t* t = saturn_vdp2_texts();
    SATURN_ASSERT_EQ(t[0].col, 2);
    SATURN_ASSERT_EQ(t[0].row, 5);
    SATURN_ASSERT_EQ(t[0].len, 5);
    SATURN_ASSERT_STR_EQ(t[0].text, "HELLO");
    SATURN_ASSERT_EQ(saturn_vdp2_clear_color(), 0x8421);
}

SATURN_TEST(vdp2_print_truncates_to_screen_width) {
    char buf[60];
    int i;
    for (i = 0; i < 50; ++i) buf[i] = 'X';
    buf[50] = '\0';
    SATURN_ASSERT_OK(saturn_vdp2_begin_frame());
    SATURN_ASSERT_OK(saturn_vdp2_print(35, 0, 0, buf));
    /* col=35, cols=40 -> truncate to 5 glyphs */
    const saturn_vdp2_text_t* t = saturn_vdp2_texts();
    SATURN_ASSERT_EQ(t[0].len, 5);
}

SATURN_TEST(vdp2_print_invalid_pos_rejected) {
    SATURN_ASSERT_OK(saturn_vdp2_begin_frame());
    SATURN_ASSERT_EQ(saturn_vdp2_print(40, 0, 0, "x"), SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp2_print(0, 28, 0, "x"), SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp2_print(0, 0, 0, NULL), SATURN_ERR_INVALID);
}

SATURN_TEST(vdp2_end_frame_calls_flush) {
    SATURN_ASSERT_OK(saturn_vdp2_begin_frame());
    SATURN_ASSERT_OK(saturn_vdp2_clear(0x83E0));
    SATURN_ASSERT_OK(saturn_vdp2_print(0, 0, 0, "hi"));
    SATURN_ASSERT_OK(saturn_vdp2_end_frame());
    SATURN_ASSERT_EQ(g_flush_calls, 1);
    SATURN_ASSERT_EQ(g_last_n, 1);
    SATURN_ASSERT_EQ(g_last_bg, 0x83E0);
}

SATURN_TEST(vdp2_begin_resets_state) {
    SATURN_ASSERT_OK(saturn_vdp2_begin_frame());
    SATURN_ASSERT_OK(saturn_vdp2_print(0, 0, 0, "stale"));
    SATURN_ASSERT_OK(saturn_vdp2_begin_frame());
    SATURN_ASSERT_EQ(saturn_vdp2_text_count(), 0);
    SATURN_ASSERT_EQ(saturn_vdp2_clear_color(), 0);
}
