/*
 * tests/test_kbd.c — keyboard widget unit tests.
 *
 * Pure unit tests: no BUP, no PAL. The widget is platform-agnostic.
 */

#include <saturn_test/test.h>
#include <saturn_app/widgets/keyboard.h>

#include <string.h>

#define BTN_RIGHT   0x0001
#define BTN_LEFT    0x0002
#define BTN_DOWN    0x0004
#define BTN_UP      0x0008
#define BTN_START   0x0010
#define BTN_A       0x0020
#define BTN_B       0x0040
#define BTN_C       0x0080

/* Press a button: edge from prev=0 to now=mask, then a release frame. */
static void press(sapp_kbd_t* k, uint16_t mask) {
    sapp_kbd_input(k, mask, 0);
    sapp_kbd_input(k, 0, mask);
}

/* Default-layout init helper (10×4, cap 10). */
static void init_default(sapp_kbd_t* k, bool allow_cancel) {
    sapp_kbd_layout_t L = SAPP_KBD_DEFAULT_LAYOUT(allow_cancel);
    sapp_kbd_init(k, NULL, L);
}

/* Move cursor to a given (x,y). Cursor wraps; just press right/down N times. */
static void move_cursor_to(sapp_kbd_t* k, uint8_t x, uint8_t y) {
    while (k->cur_x != x) press(k, BTN_RIGHT);
    while (k->cur_y != y) press(k, BTN_DOWN);
}

SATURN_TEST(init_clears_buffer_and_state) {
    sapp_kbd_t k;
    init_default(&k, true);
    SATURN_ASSERT_EQ(k.length, 0);
    SATURN_ASSERT_EQ(k.cur_x, 0);
    SATURN_ASSERT_EQ(k.cur_y, 0);
    SATURN_ASSERT_EQ(k.committed, 0);
    SATURN_ASSERT_EQ(k.cancelled, 0);
    SATURN_ASSERT_STR_EQ(k.buffer, "");
}

SATURN_TEST(cursor_wraps_on_right_edge) {
    sapp_kbd_t k;
    init_default(&k, true);
    /* cols=10; press right 9 times -> x=9; once more wraps to 0 (y unchanged). */
    int i;
    for (i = 0; i < 9; ++i) press(&k, BTN_RIGHT);
    SATURN_ASSERT_EQ(k.cur_x, 9);
    press(&k, BTN_RIGHT);
    SATURN_ASSERT_EQ(k.cur_x, 0);
    SATURN_ASSERT_EQ(k.cur_y, 0);
}

SATURN_TEST(cursor_wraps_on_bottom_edge) {
    sapp_kbd_t k;
    init_default(&k, true);
    int i;
    for (i = 0; i < 3; ++i) press(&k, BTN_DOWN);
    SATURN_ASSERT_EQ(k.cur_y, 3);
    press(&k, BTN_DOWN);
    SATURN_ASSERT_EQ(k.cur_y, 0);
    SATURN_ASSERT_EQ(k.cur_x, 0);
}

SATURN_TEST(typing_letters_appends_to_buffer) {
    sapp_kbd_t k;
    init_default(&k, true);
    /* (0,0)=A; A. */
    press(&k, BTN_A);
    /* Move to (1,0)=B; A. */
    press(&k, BTN_RIGHT);
    press(&k, BTN_A);
    /* Move to (2,0)=C; A. */
    press(&k, BTN_RIGHT);
    press(&k, BTN_A);
    SATURN_ASSERT_STR_EQ(k.buffer, "ABC");
    SATURN_ASSERT_EQ(k.length, 3);
}

SATURN_TEST(b_backspaces_from_anywhere) {
    sapp_kbd_t k;
    init_default(&k, true);
    /* Type "AB" then move cursor far away and press B. */
    press(&k, BTN_A);
    press(&k, BTN_RIGHT); press(&k, BTN_A);
    SATURN_ASSERT_STR_EQ(k.buffer, "AB");
    /* move to a non-glyph cell area */
    move_cursor_to(&k, 9, 3);   /* OK cell */
    press(&k, BTN_B);
    SATURN_ASSERT_STR_EQ(k.buffer, "A");
    press(&k, BTN_B);
    SATURN_ASSERT_STR_EQ(k.buffer, "");
    /* B on empty buffer is a no-op. */
    press(&k, BTN_B);
    SATURN_ASSERT_EQ(k.length, 0);
}

SATURN_TEST(del_cell_with_a_backspaces) {
    sapp_kbd_t k;
    init_default(&k, true);
    press(&k, BTN_A);                       /* type 'A' (cursor at (0,0)) */
    SATURN_ASSERT_STR_EQ(k.buffer, "A");
    /* DEL is the second-to-last cell: index = total-2 = 38, which is
     * (col 8, row 3) on a 10x4 grid. */
    move_cursor_to(&k, 8, 3);
    press(&k, BTN_A);
    SATURN_ASSERT_STR_EQ(k.buffer, "");
}

SATURN_TEST(ok_with_empty_does_not_commit) {
    sapp_kbd_t k;
    init_default(&k, true);
    move_cursor_to(&k, 9, 3);              /* OK cell */
    press(&k, BTN_A);
    SATURN_ASSERT_EQ(k.committed, 0);
}

SATURN_TEST(ok_with_content_commits) {
    sapp_kbd_t k;
    init_default(&k, true);
    press(&k, BTN_A);                       /* type 'A' */
    move_cursor_to(&k, 9, 3);               /* OK */
    press(&k, BTN_A);
    SATURN_ASSERT_EQ(k.committed, 1);
    SATURN_ASSERT_STR_EQ(k.buffer, "A");
}

SATURN_TEST(start_button_acts_as_ok) {
    sapp_kbd_t k;
    init_default(&k, true);
    /* START with empty buffer is a no-op. */
    press(&k, BTN_START);
    SATURN_ASSERT_EQ(k.committed, 0);
    /* Type and START commits. */
    press(&k, BTN_A);
    press(&k, BTN_START);
    SATURN_ASSERT_EQ(k.committed, 1);
}

SATURN_TEST(typing_past_cap_chars_is_no_op) {
    sapp_kbd_t k;
    init_default(&k, true);
    /* cap_chars=10; type the 'A' cell repeatedly. */
    int i;
    for (i = 0; i < 12; ++i) press(&k, BTN_A);
    SATURN_ASSERT_EQ(k.length, 10);
    SATURN_ASSERT_STR_EQ(k.buffer, "AAAAAAAAAA");
}

SATURN_TEST(cancel_disabled_when_allow_cancel_false) {
    sapp_kbd_t k;
    init_default(&k, false);
    press(&k, BTN_C);
    SATURN_ASSERT_EQ(k.cancelled, 0);
}

SATURN_TEST(cancel_enabled_when_allow_cancel_true) {
    sapp_kbd_t k;
    init_default(&k, true);
    press(&k, BTN_C);
    SATURN_ASSERT_EQ(k.cancelled, 1);
}

SATURN_TEST(initial_string_pre_populates) {
    sapp_kbd_t k;
    sapp_kbd_layout_t L = SAPP_KBD_DEFAULT_LAYOUT(true);
    sapp_kbd_init(&k, "BOB", L);
    SATURN_ASSERT_STR_EQ(k.buffer, "BOB");
    SATURN_ASSERT_EQ(k.length, 3);
}

SATURN_TEST(render_emits_prompt_and_grid_into_scene) {
    sapp_kbd_t k;
    lobby_scene_t scene;
    init_default(&k, true);
    lobby_scene_clear(&scene, 0);
    sapp_kbd_render(&k, &scene, "ENTER NAME");
    /* Expect prompt + buffer line + at least one cell text. */
    SATURN_ASSERT_GT(scene.n_texts, 3);
}
