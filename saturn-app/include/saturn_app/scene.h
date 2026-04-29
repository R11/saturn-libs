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

typedef struct lobby_scene {
    uint16_t      bg_color;
    uint16_t      n_quads;
    uint16_t      n_texts;
    lobby_quad_t  quads[LOBBY_SCENE_MAX_QUADS];
    lobby_text_t  texts[LOBBY_SCENE_MAX_TEXTS];
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
