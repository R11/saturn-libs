/*
 * libs/saturn-app — the lobby framework.
 *
 * v1 design: a registry of games, a four-state offline state machine
 * (TITLE / SELECT / PLAYING / GAME_OVER), and a frame loop the platform
 * shell drives. The framework is platform-agnostic; the platform supplies
 * input via sapp_run_one_frame(inputs[]) and the framework returns a
 * scene the platform draws.
 */

#ifndef SATURN_APP_H
#define SATURN_APP_H

#include <stdint.h>
#include <stddef.h>

#include <saturn_base/result.h>
#include <saturn_app/scene.h>
#include <saturn_app/game.h>
#include <saturn_app/state.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAPP_MAX_GAMES       16u
#define SAPP_GAME_STATE_BYTES 4096u  /* per-game state arena */

/* Lifecycle. */
saturn_result_t sapp_init    (uint16_t screen_w, uint16_t screen_h,
                              uint32_t boot_seed);
void            sapp_shutdown(void);

/* Game registry. Pointers must remain valid for the lifetime of the app. */
saturn_result_t sapp_register_game(const lobby_game_t* g);
uint8_t         sapp_game_count   (void);

/* Per-frame entry point. inputs[] carries one lobby_input_t per player
 * slot. inputs[0] is the canonical "menu pad" used to drive the lobby
 * itself. Returns the scene to be drawn this frame. */
const lobby_scene_t* sapp_run_one_frame(const lobby_input_t inputs[LOBBY_MAX_PLAYERS]);

/* Introspection. */
lobby_state_t sapp_state         (void);
const char*   sapp_active_game_id(void);   /* NULL when not PLAYING */
uint32_t      sapp_frame_count   (void);

/* Drive the state machine externally (used by tests / harnesses). */
void sapp_force_state(lobby_state_t s);

#ifdef __cplusplus
}
#endif
#endif /* SATURN_APP_H */
