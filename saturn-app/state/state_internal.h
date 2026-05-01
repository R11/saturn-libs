/*
 * state/state_internal.h — internal coordination between core/ and the
 * per-state modules. Not installed in include/.
 *
 * Each state module exports an enter/input/render trio. core dispatches
 * based on lobby_state_t. Input functions return a non-zero "transition
 * request" code if they want core to advance to a different state.
 */

#ifndef SATURN_APP_STATE_INTERNAL_H
#define SATURN_APP_STATE_INTERNAL_H

#include <saturn_app.h>
#include <saturn_app/local_lobby.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared scene preamble — core defines these. */
void sapp_scene_clear_for_name_entry(lobby_scene_t* s);

/* ---------------- name_entry (first-run + new-name) -------------------- */

void sapp_state_name_entry_first_run_enter (void);
int  sapp_state_name_entry_first_run_input (lobby_input_t now,
                                            lobby_input_t prev);
void sapp_state_name_entry_first_run_render(lobby_scene_t* scene);

/* ---------------- local_lobby ------------------------------------------ */

/* Initialise (or re-initialise) the lobby. Slot 0 is seated with the
 * framework's current identity. Cursor lands on slot 0. */
void sapp_state_local_lobby_enter(void);

/* Process one frame of input. Returns the next state to transition to,
 * or LOBBY_STATE_LOCAL_LOBBY (== "stay here"). */
lobby_state_t sapp_state_local_lobby_input(
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev  [LOBBY_MAX_PLAYERS]);

void sapp_state_local_lobby_render(lobby_scene_t* scene);

/* Direct access for the name_pick module. */
sapp_local_lobby_t* sapp_lobby_state(void);

/* ---------------- name_pick (cycler + new-name keyboard) --------------- */

/* Open NAME_PICK for the given slot. */
void sapp_state_name_pick_enter(uint8_t slot);

/* Returns next state. NAME_PICK / NAME_ENTRY_NEW / LOCAL_LOBBY. */
lobby_state_t sapp_state_name_pick_input(lobby_input_t now,
                                         lobby_input_t prev);
void sapp_state_name_pick_render(lobby_scene_t* scene);

/* New-name keyboard sub-state. Owned by name_pick (allow_cancel=true). */
void sapp_state_name_entry_new_enter(void);
lobby_state_t sapp_state_name_entry_new_input(lobby_input_t now,
                                              lobby_input_t prev);
void sapp_state_name_entry_new_render(lobby_scene_t* scene);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_APP_STATE_INTERNAL_H */
