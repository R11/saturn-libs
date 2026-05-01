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
#include <saturn_app/identity.h>
#include <saturn_app/local_lobby.h>
#include <saturn_app/online.h>

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

/* Identity bootstrap. The platform shell calls this once after installing
 * the BUP PAL and before sapp_run_one_frame(). The framework loads the
 * LOBBY_ID record (or fills a default if missing/corrupt) and decides the
 * starting state: NAME_ENTRY_FIRST_RUN if the loaded record has an empty
 * current_name, otherwise TITLE. Returns the loaded identity for the
 * caller to inspect. Calling sapp_init() resets identity to a NULL state;
 * sapp_bootstrap_identity() must be called after sapp_init(). */
const sapp_identity_t* sapp_bootstrap_identity(void);

/* Read the framework's current identity. NULL if bootstrap hasn't run. */
const sapp_identity_t* sapp_get_identity(void);

/* Replace the framework's identity wholesale. Used by tests that want
 * to install a synthetic identity without going through BUP. Does NOT
 * persist to BUP; for that, save via sapp_identity_save() before/after. */
void sapp_set_identity(const sapp_identity_t* id);

/* Title-splash NBG1 backdrop. Procedural gradient by default; user can
 * substitute a PNG-derived rgb555 image by editing the definition in
 * core/saturn_app_core.c. */
extern const lobby_bg_image_t g_title_bg;

#ifdef __cplusplus
}
#endif
#endif /* SATURN_APP_H */
