/*
 * state/state_internal.h — internal coordination between core/ and the
 * unified lobby state. Not installed in include/.
 *
 * After the M-redesign there is just one lobby state module
 * (state_lobby.c) and the first-run name entry module. The lobby owns
 * its own internal view machine (NAME_PICK / NEW_NAME_KBD / CONNECTING
 * / LOBBY_LIST / ROOM_CREATE_KBD / COUNTDOWN) — it dispatches input
 * and rendering based on the current view.
 */

#ifndef SATURN_APP_STATE_INTERNAL_H
#define SATURN_APP_STATE_INTERNAL_H

#include <saturn_app.h>
#include <saturn_app/local_lobby.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared scene preamble — core defines this. */
void sapp_scene_clear_for_name_entry(lobby_scene_t* s);

/* ---------------- name_entry (first-run only) -------------------- */

void sapp_state_name_entry_first_run_enter (void);
int  sapp_state_name_entry_first_run_input (lobby_input_t now,
                                            lobby_input_t prev);
void sapp_state_name_entry_first_run_render(lobby_scene_t* scene);

/* ---------------- unified lobby ---------------------------------- */

/* Reset / re-enter the unified lobby. Slot 0 is seated with the
 * framework's current identity. Cursor lands in PLAYERS column on
 * slot 0. View is DEFAULT. */
void sapp_state_lobby_enter(void);

/* Process one frame of input. Returns the next top-level state.
 * Returns LOBBY_STATE_LOBBY ("stay here") most of the time. */
lobby_state_t sapp_state_lobby_input(
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev  [LOBBY_MAX_PLAYERS]);

void sapp_state_lobby_render(lobby_scene_t* scene);

/* Mutable pointer for tests (and for cross-module access from the
 * online ctx if needed). */
sapp_local_lobby_t* sapp_lobby_state(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_APP_STATE_INTERNAL_H */
