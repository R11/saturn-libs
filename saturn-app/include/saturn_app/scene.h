/*
 * libs/saturn-app/scene — the platform-agnostic display list every game
 * fills in its render_scene hook. The framework lowers this onto
 * saturn-vdp1 (quads) and saturn-vdp2 (text + bg) on Saturn, the canvas
 * on web, and stdout on the host harness.
 */

#ifndef SATURN_APP_SCENE_H
#define SATURN_APP_SCENE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOBBY_SCENE_MAX_QUADS    256u
#define LOBBY_SCENE_MAX_TEXTS     32u
#define LOBBY_TEXT_MAX_LEN        40u

typedef struct lobby_quad {
    int16_t  x, y;        /* upper-left in screen pixels */
    uint16_t w, h;
    uint16_t color;       /* RGB555 + bit 15 opaque */
} lobby_quad_t;

typedef struct lobby_text {
    uint8_t col;          /* 0..39 */
    uint8_t row;          /* 0..27 */
    uint8_t palette;      /* 0..15 */
    uint8_t len;
    char    str[LOBBY_TEXT_MAX_LEN];
} lobby_text_t;

/* Forward-declare so we can hold a pointer without dragging in game.h
 * (which includes this header). */
struct lobby_bg_image;

typedef struct lobby_scene {
    uint16_t      bg_color;
    uint16_t      n_quads;
    uint16_t      n_texts;
    lobby_quad_t  quads[LOBBY_SCENE_MAX_QUADS];
    lobby_text_t  texts[LOBBY_SCENE_MAX_TEXTS];

    /* Per-frame NBG1 backdrop request. Pointer-identity is the only
     * thing the platform shell looks at; a stable pointer across frames
     * means "no change, skip re-upload." NULL means "no backdrop —
     * clear NBG1 to a default colour."
     *
     * The lobby state machine sets this each frame:
     *   TITLE     -> &g_title_bg     (procedural / PNG-backed splash)
     *   SELECT    -> NULL            (clear)
     *   PLAYING   -> active game's lobby_game_t.background_image
     *   GAME_OVER -> NULL            (clear) */
    const struct lobby_bg_image* bg_image_ref;
} lobby_scene_t;

/* Convenience helpers for game render functions. */
void  lobby_scene_clear (lobby_scene_t* s, uint16_t bg_color);
int   lobby_scene_quad  (lobby_scene_t* s,
                         int16_t x, int16_t y, uint16_t w, uint16_t h,
                         uint16_t color);
int   lobby_scene_text  (lobby_scene_t* s,
                         uint8_t col, uint8_t row, uint8_t palette,
                         const char* str);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_APP_SCENE_H */
