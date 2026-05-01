/*
 * libs/saturn-app/state/state_name_entry.c — first-run name entry.
 *
 * Owns a single sapp_kbd_t instance and the input/render plumbing for
 * LOBBY_STATE_NAME_ENTRY_FIRST_RUN. Hooked into the core state machine
 * via the three exported functions below; `core/saturn_app_core.c`
 * dispatches to them when the state is active.
 *
 * On commit:
 *   - The keyboard's buffer is copied into the framework's identity
 *     `current_name`.
 *   - The new name is FIFO-added to the roster.
 *   - The identity is saved through the installed BUP PAL.
 *   - The state machine advances to LOCAL_LOBBY.
 *
 * Cancel is disabled on first run (allow_cancel=false), so this state
 * has no exit other than commit.
 */

#include <saturn_app.h>
#include <saturn_app/widgets/keyboard.h>

#include <string.h>

/* Defined in core/saturn_app_core.c. Internal coordination — not in
 * the public header. */
void sapp_scene_clear_for_name_entry(lobby_scene_t* s);

/* Keyboard widget instance — module-static, no malloc. */
static sapp_kbd_t  s_kbd;
static int         s_inited;

void sapp_state_name_entry_first_run_enter(void)
{
    sapp_kbd_layout_t L = SAPP_KBD_DEFAULT_LAYOUT(false);
    sapp_kbd_init(&s_kbd, NULL, L);
    s_inited = 1;
}

/* Returns 1 if the state committed (caller should advance to TITLE), 0
 * otherwise. Cancel is impossible here (allow_cancel=false). */
int sapp_state_name_entry_first_run_input(lobby_input_t now,
                                          lobby_input_t prev)
{
    if (!s_inited) sapp_state_name_entry_first_run_enter();

    sapp_kbd_input(&s_kbd, now, prev);

    if (s_kbd.committed) {
        /* Commit: copy buffer into the live identity, FIFO-add, save. */
        sapp_identity_t id;
        const sapp_identity_t* cur = sapp_get_identity();
        if (cur) {
            id = *cur;
        } else {
            sapp_identity_default(&id);
        }

        /* Truncate to SAPP_NAME_CAP-1 chars + NUL. */
        size_t i = 0;
        for (; i < SAPP_NAME_CAP - 1 && s_kbd.buffer[i] != '\0'; ++i) {
            id.current_name[i] = s_kbd.buffer[i];
        }
        for (; i < SAPP_NAME_CAP; ++i) id.current_name[i] = '\0';

        sapp_identity_add_name(&id, id.current_name);
        sapp_set_identity(&id);
        (void)sapp_identity_save(&id);   /* best-effort */

        s_inited = 0;                     /* reset for any future re-entry */
        return 1;
    }
    return 0;
}

void sapp_state_name_entry_first_run_render(lobby_scene_t* scene)
{
    if (!s_inited) sapp_state_name_entry_first_run_enter();
    sapp_scene_clear_for_name_entry(scene);
    sapp_kbd_render(&s_kbd, scene, "ENTER YOUR NAME");
}
