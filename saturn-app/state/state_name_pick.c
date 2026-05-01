/*
 * libs/saturn-app/state/state_name_pick.c — name picker + new-name keyboard.
 *
 * NAME_PICK is a right-half cycler that lets the active pad pick a name
 * for the slot it's editing. The lobby left half is rendered behind it
 * (frozen — no left-half input is processed).
 *
 * Bindings (the slot's own pad):
 *   LEFT/RIGHT  cycle through available names (excluding names seated in
 *               OTHER slots so two pads can't pick the same one)
 *   A           seat the slot with the current preview, return to LOCAL_LOBBY
 *   Y (=START)  open NAME_ENTRY_NEW (keyboard with allow_cancel=true)
 *   B           cancel — guest slot un-seats; slot 0 reverts (no un-seat)
 *
 * NAME_ENTRY_NEW commit: FIFO-add the typed name to the roster, save BUP,
 *   return to NAME_PICK with the new name selected (and un-seated slots
 *   pre-seat with it).
 * NAME_ENTRY_NEW cancel: return to NAME_PICK unchanged.
 *
 * Locked decision #4: the default cycle position when entering NAME_PICK
 * is the next free roster entry not seated elsewhere. If none, fall back
 * to the slot's existing seated name (slot 0) or the roster's last entry.
 */

#include "state_internal.h"
#include <saturn_app/widgets/keyboard.h>

#include <stdio.h>
#include <string.h>

/* Mirror saturn-smpc bits. */
#define INP_RIGHT  0x0001
#define INP_LEFT   0x0002
#define INP_DOWN   0x0004
#define INP_UP     0x0008
#define INP_START  0x0010   /* mapped to "Y" semantics in the picker */
#define INP_A      0x0020
#define INP_B      0x0040

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

/* Picker context. */
static struct {
    uint8_t slot;                        /* which lobby slot we're picking for */
    char    options[SAPP_NAME_MAX + 1][SAPP_NAME_CAP];   /* names available */
    uint8_t n_options;
    uint8_t cursor;                      /* index into options */
    bool    inited;
} s_pick;

/* New-name keyboard sub-state. */
static struct {
    sapp_kbd_t kbd;
    bool       inited;
} s_kbd;

/* ------------------------------------------------------------------ */

static int pressed(lobby_input_t now, lobby_input_t prev, uint16_t mask) {
    return ((now & mask) && !(prev & mask)) ? 1 : 0;
}

static void copy_name(char* dst, const char* src) {
    size_t i = 0;
    if (src) {
        for (; i < SAPP_NAME_CAP - 1 && src[i] != '\0'; ++i) dst[i] = src[i];
    }
    for (; i < SAPP_NAME_CAP; ++i) dst[i] = '\0';
}

static bool name_seated_in_other_slot(const sapp_local_lobby_t* lobby,
                                      const char* name, uint8_t exclude_slot)
{
    if (!lobby || !name || !name[0]) return false;
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        if (i == exclude_slot) continue;
        if (!lobby->seated[i]) continue;
        if (strncmp(lobby->seated_name[i], name, SAPP_NAME_CAP) == 0) {
            return true;
        }
    }
    return false;
}

/* Build the available-name list and pick the default cursor. */
static void rebuild_options(uint8_t slot)
{
    const sapp_identity_t*    id    = sapp_get_identity();
    const sapp_local_lobby_t* lobby = sapp_lobby_get();
    s_pick.n_options = 0;
    s_pick.cursor    = 0;

    if (!id) return;

    /* Roster names not seated in OTHER slots. */
    for (uint8_t i = 0; i < id->name_count && i < SAPP_NAME_MAX; ++i) {
        const char* n = id->names[i];
        if (!n[0]) continue;
        if (name_seated_in_other_slot(lobby, n, slot)) continue;
        copy_name(s_pick.options[s_pick.n_options++], n);
    }

    /* Default cursor: per decision #4, "next free roster entry". For
     * guest slots (not currently seated) this is option 0. For an
     * already-seated slot, prefer to land on the currently-seated name
     * so the user can see what's there. Slot 0's current_name is always
     * present in the roster (added at first-run commit + any new-name). */
    if (lobby && lobby->seated[slot] && s_pick.n_options > 0) {
        for (uint8_t i = 0; i < s_pick.n_options; ++i) {
            if (strncmp(s_pick.options[i], lobby->seated_name[slot],
                        SAPP_NAME_CAP) == 0) {
                s_pick.cursor = i;
                break;
            }
        }
    }
}

void sapp_state_name_pick_enter(uint8_t slot)
{
    if (slot >= SAPP_LOBBY_SLOTS) slot = 0;
    s_pick.slot   = slot;
    s_pick.inited = true;
    rebuild_options(slot);
    /* Stash the slot as the keyboard owner so the lobby render knows. */
    sapp_local_lobby_t* lobby = sapp_lobby_state();
    if (lobby) lobby->kbd_owner_slot = slot;
}

/* Returns 1 if the slot has a preview to seat (slot 0 always does — it's
 * already seated with current_name). */
static const char* current_preview(void) {
    if (s_pick.n_options == 0) return NULL;
    return s_pick.options[s_pick.cursor];
}

/* Commit (A) handler. */
static void commit_pick(void)
{
    sapp_local_lobby_t* lobby = sapp_lobby_state();
    const char* name = current_preview();
    if (!lobby || !name) return;

    copy_name(lobby->seated_name[s_pick.slot], name);
    lobby->seated[s_pick.slot] = true;

    if (s_pick.slot == 0) {
        /* Slot 0 commit = identity update + persist. */
        sapp_identity_t id;
        const sapp_identity_t* cur = sapp_get_identity();
        if (cur) id = *cur; else sapp_identity_default(&id);
        copy_name(id.current_name, name);
        sapp_identity_add_name(&id, name);   /* dedupe-safe */
        sapp_set_identity(&id);
        (void)sapp_identity_save(&id);
    }
    lobby->kbd_owner_slot = SAPP_LOBBY_NO_OWNER;
}

/* Cancel (B) handler. */
static void cancel_pick(void)
{
    sapp_local_lobby_t* lobby = sapp_lobby_state();
    if (!lobby) return;
    if (s_pick.slot != 0) {
        /* Guest slot: un-seat. */
        lobby->seated[s_pick.slot] = false;
        memset(lobby->seated_name[s_pick.slot], 0,
               sizeof(lobby->seated_name[s_pick.slot]));
    }
    /* Slot 0: leave seated_name[0] alone — re-enter will re-seat from
     * identity.current_name anyway. */
    lobby->kbd_owner_slot = SAPP_LOBBY_NO_OWNER;
}

lobby_state_t sapp_state_name_pick_input(lobby_input_t now,
                                         lobby_input_t prev)
{
    if (!s_pick.inited) return LOBBY_STATE_LOCAL_LOBBY;

    if (pressed(now, prev, INP_LEFT) && s_pick.n_options > 0) {
        s_pick.cursor = (uint8_t)((s_pick.cursor + s_pick.n_options - 1)
                                  % s_pick.n_options);
    }
    if (pressed(now, prev, INP_RIGHT) && s_pick.n_options > 0) {
        s_pick.cursor = (uint8_t)((s_pick.cursor + 1) % s_pick.n_options);
    }
    if (pressed(now, prev, INP_A)) {
        if (current_preview()) {
            commit_pick();
            return LOBBY_STATE_LOCAL_LOBBY;
        }
    }
    if (pressed(now, prev, INP_START)) {
        sapp_state_name_entry_new_enter();
        return LOBBY_STATE_NAME_ENTRY_NEW;
    }
    if (pressed(now, prev, INP_B)) {
        cancel_pick();
        return LOBBY_STATE_LOCAL_LOBBY;
    }
    return LOBBY_STATE_NAME_PICK;
}

void sapp_state_name_pick_render(lobby_scene_t* scene)
{
    if (!scene || !s_pick.inited) return;

    /* Render the lobby behind us so the left half stays visible. */
    sapp_state_local_lobby_render(scene);

    /* Right half: cols 20..39, rows 4..10. */
    char buf[24];
    snprintf(buf, sizeof(buf), "PICK NAME SLOT %u",
             (unsigned)(s_pick.slot + 1));
    lobby_scene_text(scene, 20, 4, PAL_HIGHLIGHT, buf);

    if (s_pick.n_options == 0) {
        lobby_scene_text(scene, 20, 6, PAL_DIM,
                         "(no saved names)");
    } else {
        char nbuf[SAPP_NAME_CAP + 4];
        snprintf(nbuf, sizeof(nbuf), "< %s >", s_pick.options[s_pick.cursor]);
        lobby_scene_text(scene, 20, 6, PAL_DEFAULT, nbuf);
    }
    lobby_scene_text(scene, 20, 8,  PAL_DIM, "[Y] NEW NAME");
    lobby_scene_text(scene, 20, 9,  PAL_DIM, "[A] CONFIRM");
    lobby_scene_text(scene, 20, 10, PAL_DIM, "[B] CANCEL");
}

/* ============== NAME_ENTRY_NEW (keyboard sub-state) =============== */

void sapp_state_name_entry_new_enter(void)
{
    /* Right-half layout: 5×8 grid at cols 22..36, rows 8..23. Picker UI
     * on the left half stays visible. */
    sapp_kbd_layout_t L = SAPP_KBD_LAYOUT_RIGHT_HALF(true);
    sapp_kbd_init(&s_kbd.kbd, NULL, L);
    s_kbd.inited = true;
}

lobby_state_t sapp_state_name_entry_new_input(lobby_input_t now,
                                              lobby_input_t prev)
{
    if (!s_kbd.inited) return LOBBY_STATE_NAME_PICK;

    sapp_kbd_input(&s_kbd.kbd, now, prev);

    if (s_kbd.kbd.committed) {
        /* Add to roster + save. */
        sapp_identity_t id;
        const sapp_identity_t* cur = sapp_get_identity();
        if (cur) id = *cur; else sapp_identity_default(&id);
        sapp_identity_add_name(&id, s_kbd.kbd.buffer);
        sapp_set_identity(&id);
        (void)sapp_identity_save(&id);

        /* Re-enter NAME_PICK so options pick up the new entry, then
         * point the cursor at the new name. */
        char picked[SAPP_NAME_CAP];
        copy_name(picked, s_kbd.kbd.buffer);
        sapp_state_name_pick_enter(s_pick.slot);
        for (uint8_t i = 0; i < s_pick.n_options; ++i) {
            if (strncmp(s_pick.options[i], picked, SAPP_NAME_CAP) == 0) {
                s_pick.cursor = i;
                break;
            }
        }
        s_kbd.inited = false;
        return LOBBY_STATE_NAME_PICK;
    }
    if (s_kbd.kbd.cancelled) {
        s_kbd.inited = false;
        return LOBBY_STATE_NAME_PICK;
    }
    return LOBBY_STATE_NAME_ENTRY_NEW;
}

void sapp_state_name_entry_new_render(lobby_scene_t* scene)
{
    if (!scene || !s_kbd.inited) return;
    /* Keep the lobby left half visible. Don't render the picker over the
     * keyboard — the keyboard occupies the right half. */
    sapp_state_local_lobby_render(scene);
    sapp_kbd_render(&s_kbd.kbd, scene, "NEW NAME");
}
