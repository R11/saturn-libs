/*
 * libs/saturn-app/state/state_local_lobby.c — local lobby state.
 *
 * Side-by-side layout (cols 0..19 list, cols 20..39 keyboard panel when
 * a sub-state opens it). The lobby itself just renders the slot list and
 * action row; the right half is owned by NAME_PICK / NAME_ENTRY_NEW.
 *
 * Per-pad join model:
 *   - Slot 0 is always seated as identity.current_name.
 *   - Pads 1..7 join by pressing START while inputs[N] hits the lobby —
 *     this opens NAME_PICK for slot N (kbd_owner_slot=N).
 *   - The menu cursor (D-pad nav) is driven by inputs[0]; A activates.
 *     Activating slot N opens NAME_PICK for that slot, owner = pad 0.
 *   - On a seated slot, A re-opens NAME_PICK (so the user can change
 *     who's seated there). On slot 0 this is the self-edit path.
 *   - On an empty slot, A opens NAME_PICK with default = next free roster.
 *
 * CONNECT is a stub for M4 — pressing A on it transitions to
 * LOBBY_STATE_CONNECTING, which renders a placeholder and returns to
 * LOCAL_LOBBY on B.
 *
 * PLAY_OFFLINE is a temporary M3-only seam so the existing offline
 * snake/pong/paint flow stays reachable from tests until the online
 * milestones (M4..M6) provide a proper rooms path. Remove in M4.
 */

#include "state_internal.h"

#include <stdio.h>
#include <string.h>

/* Mirror of saturn-smpc bits — same constants as core. */
#define INP_RIGHT  0x0001
#define INP_LEFT   0x0002
#define INP_DOWN   0x0004
#define INP_UP     0x0008
#define INP_START  0x0010
#define INP_A      0x0020
#define INP_B      0x0040

#define PAL_DEFAULT   0
#define PAL_HIGHLIGHT 1
#define PAL_DIM       2

static sapp_local_lobby_t s_lobby;
static int                s_inited;

/* Reset hook called from sapp_init() so a fresh-init test sees a fresh
 * lobby (the module-static s_lobby would otherwise carry over). */
void sapp_state_local_lobby__reset(void);
void sapp_state_local_lobby__reset(void)
{
    memset(&s_lobby, 0, sizeof(s_lobby));
    s_inited = 0;
}

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

static void seat_self(void) {
    const sapp_identity_t* id = sapp_get_identity();
    copy_name(s_lobby.seated_name[0],
              (id && id->current_name[0]) ? id->current_name : "P1");
    s_lobby.seated[0] = true;
}

void sapp_state_local_lobby_enter(void) {
    if (!s_inited) {
        memset(&s_lobby, 0, sizeof(s_lobby));
        for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
            s_lobby.cart_color[i] = i;   /* simple per-slot palette planning */
        }
        s_inited = 1;
    }
    seat_self();
    /* Don't reset cursor on re-enter (e.g. returning from NAME_PICK):
     * the user expects to land on the same row they activated. */
    if (s_lobby.cursor >= SAPP_LOBBY_CURSOR_COUNT) s_lobby.cursor = 0;
    s_lobby.kbd_owner_slot = SAPP_LOBBY_NO_OWNER;
}

const sapp_local_lobby_t* sapp_lobby_get(void) {
    return s_inited ? &s_lobby : NULL;
}

sapp_local_lobby_t* sapp_lobby_state(void) {
    return s_inited ? &s_lobby : NULL;
}

uint8_t sapp_lobby_seated_count(void) {
    if (!s_inited) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) if (s_lobby.seated[i]) n++;
    return n;
}

/* ------------------------------------------------------------------ */

static void cursor_step(int8_t delta) {
    int v = (int)s_lobby.cursor + delta;
    if (v < 0) v = SAPP_LOBBY_CURSOR_COUNT - 1;
    if (v >= SAPP_LOBBY_CURSOR_COUNT) v = 0;
    s_lobby.cursor = (uint8_t)v;
}

lobby_state_t sapp_state_local_lobby_input(
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev  [LOBBY_MAX_PLAYERS])
{
    if (!s_inited) sapp_state_local_lobby_enter();

    lobby_input_t menu_now  = inputs ? inputs[0] : 0;
    lobby_input_t menu_prev = prev   ? prev[0]   : 0;

    /* Menu nav. */
    if (pressed(menu_now, menu_prev, INP_UP))   cursor_step(-1);
    if (pressed(menu_now, menu_prev, INP_DOWN)) cursor_step(+1);

    /* A on cursor. */
    if (pressed(menu_now, menu_prev, INP_A)) {
        uint8_t c = s_lobby.cursor;
        if (c <= SAPP_LOBBY_CURSOR_SLOT_LAST) {
            sapp_state_name_pick_enter(c);
            return LOBBY_STATE_NAME_PICK;
        } else if (c == SAPP_LOBBY_CURSOR_CONNECT) {
            return LOBBY_STATE_CONNECTING;
        } else if (c == SAPP_LOBBY_CURSOR_PLAY_OFFLINE) {
            /* Temporary M3 seam — drop into the legacy SELECT screen. */
            return LOBBY_STATE_SELECT;
        }
    }

    /* Per-pad START on a non-self slot = quick-join that pad's slot. */
    for (uint8_t slot = 1; slot < LOBBY_MAX_PLAYERS; ++slot) {
        lobby_input_t pn = inputs ? inputs[slot] : 0;
        lobby_input_t pp = prev   ? prev[slot]   : 0;
        if (pressed(pn, pp, INP_START) && !s_lobby.seated[slot]) {
            s_lobby.cursor = slot;
            sapp_state_name_pick_enter(slot);
            return LOBBY_STATE_NAME_PICK;
        }
    }

    return LOBBY_STATE_LOCAL_LOBBY;
}

/* ------------------------------------------------------------------ */

/* Slot lines occupy stride 2 NBG0 rows. Slot 0 sits at row 3, slot N at
 * row 3 + 2*N. Last slot (7) lands at row 17. */
#define LOBBY_SLOT_ROW0     3
#define LOBBY_SLOT_STRIDE   2
#define LOBBY_HINT_ROW      (LOBBY_SLOT_ROW0 + LOBBY_SLOT_STRIDE * SAPP_LOBBY_SLOTS)            /* 19 */
#define LOBBY_DIVIDER_ROW   (LOBBY_HINT_ROW + 1)                                                /* 20 */
#define LOBBY_CONNECT_ROW   (LOBBY_DIVIDER_ROW + 1)                                             /* 21 */
#define LOBBY_OFFLINE_ROW   (LOBBY_CONNECT_ROW + 2)                                             /* 23 */

static void render_slot_line(lobby_scene_t* scene, uint8_t slot,
                             bool highlighted)
{
    char buf[24];
    const char* arrow = highlighted ? ">" : " ";
    uint8_t row = (uint8_t)(LOBBY_SLOT_ROW0 + slot * LOBBY_SLOT_STRIDE);
    uint8_t pal = highlighted ? PAL_HIGHLIGHT
                              : (s_lobby.seated[slot] ? PAL_DEFAULT : PAL_DIM);

    if (s_lobby.seated[slot]) {
        size_t i = 0;
        buf[i++] = arrow[0]; buf[i++] = ' ';
        buf[i++] = (char)('1' + slot); buf[i++] = '.'; buf[i++] = ' ';
        const char* n = s_lobby.seated_name[slot];
        for (size_t j = 0; j < SAPP_NAME_CAP - 1 && n[j] && i < sizeof(buf) - 1; ++j) {
            buf[i++] = n[j];
        }
        buf[i] = '\0';
    } else {
        /* Compact empty marker — single hint line below tells the user how
         * to join. */
        snprintf(buf, sizeof(buf), "%s %u. ----", arrow,
                 (unsigned)(slot + 1));
        buf[19] = '\0';
    }
    lobby_scene_text(scene, 0, row, pal, buf);
}

void sapp_state_local_lobby_render(lobby_scene_t* scene) {
    if (!s_inited) sapp_state_local_lobby_enter();
    if (!scene) return;

    /* Header. */
    lobby_scene_text(scene, 0, 0, PAL_HIGHLIGHT, "LOCAL LOBBY");
    lobby_scene_text(scene, 0, 1, PAL_DIM,       "-------------------");

    /* Slot list — stride 2 rows for breathing room. */
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        render_slot_line(scene, i, s_lobby.cursor == i);
    }

    /* Single join hint instead of repeating "press start" per empty slot. */
    lobby_scene_text(scene, 0, LOBBY_HINT_ROW, PAL_DIM,
                     "PRESS START TO JOIN");

    /* Divider + action row. */
    lobby_scene_text(scene, 0, LOBBY_DIVIDER_ROW, PAL_DIM,
                     "-------------------");

    {
        bool hl = (s_lobby.cursor == SAPP_LOBBY_CURSOR_CONNECT);
        char buf[16];
        snprintf(buf, sizeof(buf), "%s CONNECT", hl ? ">" : " ");
        lobby_scene_text(scene, 0, LOBBY_CONNECT_ROW,
                         hl ? PAL_HIGHLIGHT : PAL_DEFAULT, buf);
    }
    {
        bool hl = (s_lobby.cursor == SAPP_LOBBY_CURSOR_PLAY_OFFLINE);
        char buf[20];
        snprintf(buf, sizeof(buf), "%s PLAY OFFLINE", hl ? ">" : " ");
        lobby_scene_text(scene, 0, LOBBY_OFFLINE_ROW,
                         hl ? PAL_HIGHLIGHT : PAL_DEFAULT, buf);
    }
}
