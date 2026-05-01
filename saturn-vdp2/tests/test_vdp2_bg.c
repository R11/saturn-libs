#include <saturn_test/test.h>
#include <saturn_vdp2/bg.h>
#include <string.h>

extern int             saturn_vdp2_bg_test_set_image_calls(void);
extern int             saturn_vdp2_bg_test_clear_calls    (void);
extern const uint16_t* saturn_vdp2_bg_test_last_image     (void);
extern uint16_t        saturn_vdp2_bg_test_last_w         (void);
extern uint16_t        saturn_vdp2_bg_test_last_h         (void);
extern uint16_t        saturn_vdp2_bg_test_last_clear     (void);

static uint16_t g_buf[16 * 8];

static void setup(void)    { saturn_vdp2_bg_init(); }
static void teardown(void) { }
SATURN_TEST_FIXTURE(setup, teardown);

SATURN_TEST(bg_init_records_call) {
    /* init resets clear/set counts to zero and is_enabled() returns
     * false until enable(1) is called. */
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_set_image_calls(), 0);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_clear_calls(),     0);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_is_enabled(),           0);
}

SATURN_TEST(bg_set_image_records_pointer_and_size) {
    SATURN_ASSERT_OK(saturn_vdp2_bg_set_image(g_buf, 16, 8));
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_set_image_calls(), 1);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_last_w(),          16);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_last_h(),          8);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_last_image(),      g_buf);
}

SATURN_TEST(bg_set_image_rejects_oversize_or_null) {
    SATURN_ASSERT_EQ(saturn_vdp2_bg_set_image(NULL, 16, 8),  SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_set_image(g_buf, 0, 8),  SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_set_image(g_buf, 16, 0), SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_set_image(g_buf, 321, 8), SATURN_ERR_INVALID);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_set_image(g_buf, 320, 225), SATURN_ERR_INVALID);
}

SATURN_TEST(bg_clear_records_color) {
    saturn_vdp2_bg_clear(0x83E0);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_clear_calls(), 1);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_test_last_clear(),  0x83E0);
}

SATURN_TEST(bg_enable_toggles_is_enabled) {
    SATURN_ASSERT_EQ(saturn_vdp2_bg_is_enabled(), 0);
    saturn_vdp2_bg_enable(1);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_is_enabled(), 1);
    saturn_vdp2_bg_enable(0);
    SATURN_ASSERT_EQ(saturn_vdp2_bg_is_enabled(), 0);
}
