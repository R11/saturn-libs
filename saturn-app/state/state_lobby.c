/*
 * libs/saturn-app/state/state_lobby.c — unified lobby screen.
 *
 * One persistent screen with two columns and a contextual right-panel
 * takeover. Replaces the old chain of {LOCAL_LOBBY, SELECT, IN_ROOM,
 * GAME_SELECT_ONLINE, VOTE_TIMER, COUNTDOWN, CONNECTING, LOBBY_LIST,
 * ROOM_CREATE} screens. The protocol-side state still tracks readiness
 * and timer ticks (in the online ctx); the lobby just renders them
 * inline rather than as separate screens.
 *
 * Layout (40-col NBG0 character grid, 28 rows tall):
 *
 *     col 0..18           col 20..39
 *
 *     ROOM "FOO"          (online only — row 0)
 *     PLAYERS              GAMES (or right-panel view)
 *
 *   row 3   > 1. ALICE       SNAKE
 *   row 5     2. BOB       > PONG
 *   row 7     3. ----        PAINT
 *   ...
 *   row 17    8. ----
 *
 *   row 19  PRESS START TO JOIN
 *   row 21  > [CONNECT]  [START]   (offline)
 *           > [LEAVE]              (online)
 *
 * Cursor (single `>` indicator owned by pad 1):
 *   - PLAYERS column: focus on slot N (UP/DOWN moves within column,
 *     RIGHT moves to GAMES, LEFT wraps to GAMES, DOWN past slot 7
 *     wraps into the action row).
 *   - GAMES column: focus on game N (UP/DOWN moves within column,
 *     LEFT moves back to PLAYERS, DOWN past last game wraps to
 *     action row).
 *   - ACTION row: LEFT/RIGHT cycles between buttons; UP returns to
 *     wherever the cursor came from in the previous column.
 *
 * Pad 1 A:
 *   - On a player slot: open NAME_PICK in the right panel for that
 *     slot (re-open even if seated — slot 0 is the self-edit path).
 *   - On a game: select that game as pad 1's vote highlight (does
 *     NOT commit; pad 1 must press START to commit).
 *   - On CONNECT: enter CONNECTING view (kick off network handshake).
 *   - On LEAVE: send ROOM_LEAVE, drop back to offline mode.
 *   - On the START action button: equivalent to pressing START with
 *     focus on the highlighted game.
 *
 * Per-pad START semantics:
 *   - Pad N (any pad) NOT seated -> seat in next free slot, open
 *     NAME_PICK for that slot.
 *   - Pad N seated, focus on a game (only pad 1 can move focus —
 *     other pads use their last-known cursor_game which defaults to
 *     pad 1's cursor_game) -> commit pad N's vote on that game and
 *     mark pad N ready.
 *
 *   When >50% of seated pads are ready, the OFFLINE path begins a
 *   local 5s extension timer (frame-counted — display only, no real
 *   wallclock for cart-side determinism). On expiry the lobby picks
 *   a winner (Mario-Kart-style weighted random over all ready votes)
 *   and starts the offline game (transition to LOBBY_STATE_PLAYING).
 *
 *   Online path: the lobby just sends READY frames; the SERVER drives
 *   VOTE_TIMER / GAME_PICK / COUNTDOWN / GAME_START. The lobby renders
 *   the server's countdown values inline.
 */

#include "state_internal.h"
#include "state_online.h"
#include "../net/sapp_net.h"
#include "../net/sapp_proto.h"

#include <saturn_app/widgets/keyboard.h>

#include <stdio.h>
#include <string.h>

/* Mirror saturn-smpc bits — same as core. */
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

/* Layout constants. */
#define COL_PLAYERS_X    0u
#define COL_GAMES_X      20u
#define COL_HEADER_ROW   1u
#define COL_ROW_FIRST    3u
#define COL_ROW_STRIDE   2u
#define HINT_ROW         19u
#define ACTION_ROW       21u
#define STATUS_ROW       25u
#define ROOM_ROW         0u

/* Internal lobby state. */
static sapp_local_lobby_t s_lobby;
static int                s_inited;

/* NAME_PICK state (was state_name_pick.c). */
static struct {
    uint8_t slot;
    char    options[SAPP_NAME_MAX + 1][SAPP_NAME_CAP];
    uint8_t n_options;
    uint8_t cursor;
    bool    inited;
} s_pick;

/* Keyboard state for NEW_NAME and ROOM_CREATE. */
static struct {
    sapp_kbd_t kbd;
    bool       inited;
} s_kbd;

/* Lobby-list cursor (within LOBBY_LIST view). */
static uint8_t s_list_cursor;

/* Connect attempt state. */
static int s_connect_attempted;

/* Offline vote-timer + countdown (frame-counted). 60fps assumed. */
#define FRAMES_PER_SEC   60u
#define OFFLINE_VOTE_FRAMES   (5u * FRAMES_PER_SEC)
#define OFFLINE_COUNTDOWN_FRAMES (3u * FRAMES_PER_SEC)
static uint32_t s_offline_vote_frames;     /* counts down; 0 means inactive */
static uint32_t s_offline_countdown_frames;
static uint8_t  s_offline_picked_game;     /* result of weighted-random pick */
static uint32_t s_offline_seed;            /* lcg state for deterministic picks */

/* The next top-level state we should transition to (after rendering).
 * Set by helpers; drained by sapp_state_lobby_input(). */
static lobby_state_t s_pending_next_state;
static bool          s_pending_pending;

/* ------------------------------------------------------------------ */

void sapp_state_local_lobby__reset(void);
void sapp_state_local_lobby__reset(void)
{
    memset(&s_lobby, 0, sizeof(s_lobby));
    memset(&s_pick,  0, sizeof(s_pick));
    memset(&s_kbd,   0, sizeof(s_kbd));
    s_inited = 0;
    s_list_cursor = 0;
    s_connect_attempted = 0;
    s_offline_vote_frames = 0;
    s_offline_countdown_frames = 0;
    s_offline_picked_game = 0;
    s_offline_seed = 0xC0FFEEu;
    s_pending_pending = false;
}

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

static uint8_t next_free_slot(void) {
    for (uint8_t i = 1; i < SAPP_LOBBY_SLOTS; ++i) {
        if (!s_lobby.seated[i]) return i;
    }
    return SAPP_LOBBY_NO_OWNER;
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
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i)
        if (s_lobby.seated[i]) n++;
    return n;
}

uint8_t sapp_lobby_ready_count(void) {
    if (!s_inited) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i)
        if (s_lobby.seated[i] && s_lobby.ready[i]) n++;
    return n;
}

uint8_t sapp_lobby_vote_count_for_game(uint8_t game_id) {
    if (!s_inited) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        if (s_lobby.seated[i] && s_lobby.ready[i]
         && s_lobby.vote_game_id[i] == game_id) n++;
    }
    return n;
}

void sapp_state_lobby_enter(void)
{
    if (!s_inited) {
        memset(&s_lobby, 0, sizeof(s_lobby));
        for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
            s_lobby.cart_color[i] = i;
            s_lobby.vote_game_id[i] = SAPP_LOBBY_NO_OWNER;
        }
        s_inited = 1;
    }
    seat_self();
    s_lobby.kbd_owner_slot = SAPP_LOBBY_NO_OWNER;
    /* Don't reset focus on re-enter — preserves "land on the row I
     * activated" UX after returning from a takeover view. */
    if (s_lobby.focus > SAPP_LOBBY_FOCUS_ACTION) s_lobby.focus = SAPP_LOBBY_FOCUS_PLAYERS;
    if (s_lobby.cursor_player >= SAPP_LOBBY_SLOTS) s_lobby.cursor_player = 0;
    if (s_lobby.cursor_action > 1) s_lobby.cursor_action = 0;
    if (s_lobby.view > SAPP_LOBBY_VIEW_COUNTDOWN) s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;

    /* Bug 1 fix: clear per-round vote/ready state every time we (re)enter
     * the lobby. Without this, RESULTS_ONLINE / GAME_OVER -> LOBBY would
     * leave seated pads still ready with their old votes, immediately
     * re-tripping the >50% majority check and starting the same game
     * again. The 5s vote timer + 3s countdown frame counters and any
     * pending state-machine transition are also reset so a stale tick
     * doesn't fire one frame after re-entry. */
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        s_lobby.ready[i]        = false;
        s_lobby.vote_game_id[i] = SAPP_LOBBY_NO_OWNER;
    }
    s_offline_vote_frames      = 0;
    s_offline_countdown_frames = 0;
    s_offline_picked_game      = 0;
    s_pending_pending          = false;
    /* Force back to DEFAULT view so a stale COUNTDOWN view from the prior
     * round doesn't render. */
    s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
}

/* ------------------------------------------------------------------
 * Mode helpers — derived from the online ctx.
 * ------------------------------------------------------------------ */

static sapp_lobby_mode_t derive_mode(void) {
    const sapp_online_ctx_t* ctx = sapp_online_ctx();
    return (ctx && ctx->room.valid) ? SAPP_LOBBY_MODE_ONLINE
                                    : SAPP_LOBBY_MODE_OFFLINE;
}

static void refresh_mode(void) {
    s_lobby.mode = derive_mode();
}

/* ------------------------------------------------------------------
 * Cursor navigation (DEFAULT view).
 * ------------------------------------------------------------------ */

static void cursor_move(int dx, int dy) {
    uint8_t n_games = sapp_count_games();
    switch (s_lobby.focus) {
    case SAPP_LOBBY_FOCUS_PLAYERS:
        if (dy < 0) {
            if (s_lobby.cursor_player == 0) {
                s_lobby.cursor_player = SAPP_LOBBY_SLOTS - 1;
            } else s_lobby.cursor_player--;
        } else if (dy > 0) {
            if (s_lobby.cursor_player + 1 >= SAPP_LOBBY_SLOTS) {
                /* fall into action row */
                s_lobby.focus = SAPP_LOBBY_FOCUS_ACTION;
                s_lobby.cursor_action = 0;
            } else s_lobby.cursor_player++;
        } else if (dx > 0 || dx < 0) {
            if (n_games > 0) {
                s_lobby.focus = SAPP_LOBBY_FOCUS_GAMES;
                if (s_lobby.cursor_game >= n_games) s_lobby.cursor_game = 0;
            }
        }
        break;
    case SAPP_LOBBY_FOCUS_GAMES:
        if (dy < 0) {
            if (s_lobby.cursor_game == 0) {
                if (n_games > 0) s_lobby.cursor_game = n_games - 1;
            } else s_lobby.cursor_game--;
        } else if (dy > 0) {
            if (s_lobby.cursor_game + 1 >= n_games) {
                s_lobby.focus = SAPP_LOBBY_FOCUS_ACTION;
                s_lobby.cursor_action = (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) ? 0 : 1;
            } else s_lobby.cursor_game++;
        } else if (dx < 0 || dx > 0) {
            s_lobby.focus = SAPP_LOBBY_FOCUS_PLAYERS;
        }
        break;
    case SAPP_LOBBY_FOCUS_ACTION: {
        uint8_t n_actions = (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) ? 1 : 2;
        if (dy < 0) {
            /* Up: prefer GAMES if there are any, else PLAYERS. */
            if (n_games > 0) {
                s_lobby.focus = SAPP_LOBBY_FOCUS_GAMES;
                if (s_lobby.cursor_game >= n_games) s_lobby.cursor_game = n_games - 1;
            } else {
                s_lobby.focus = SAPP_LOBBY_FOCUS_PLAYERS;
            }
        } else if (dy > 0) {
            /* Down: wrap back to PLAYERS top. */
            s_lobby.focus = SAPP_LOBBY_FOCUS_PLAYERS;
            s_lobby.cursor_player = 0;
        } else if (dx > 0) {
            if (n_actions > 1)
                s_lobby.cursor_action = (uint8_t)((s_lobby.cursor_action + 1) % n_actions);
        } else if (dx < 0) {
            if (n_actions > 1) {
                s_lobby.cursor_action = (uint8_t)((s_lobby.cursor_action + n_actions - 1) % n_actions);
            }
        }
        break;
    }
    }
}

/* ------------------------------------------------------------------
 * NAME_PICK helpers (was state_name_pick.c).
 * ------------------------------------------------------------------ */

static bool name_seated_in_other_slot(const char* name, uint8_t exclude) {
    if (!name || !name[0]) return false;
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        if (i == exclude) continue;
        if (!s_lobby.seated[i]) continue;
        if (strncmp(s_lobby.seated_name[i], name, SAPP_NAME_CAP) == 0)
            return true;
    }
    return false;
}

static void pick_rebuild_options(uint8_t slot) {
    const sapp_identity_t* id = sapp_get_identity();
    s_pick.n_options = 0;
    s_pick.cursor    = 0;
    if (!id) return;
    for (uint8_t i = 0; i < id->name_count && i < SAPP_NAME_MAX; ++i) {
        const char* n = id->names[i];
        if (!n[0]) continue;
        if (name_seated_in_other_slot(n, slot)) continue;
        copy_name(s_pick.options[s_pick.n_options++], n);
    }
    if (s_lobby.seated[slot] && s_pick.n_options > 0) {
        for (uint8_t i = 0; i < s_pick.n_options; ++i) {
            if (strncmp(s_pick.options[i], s_lobby.seated_name[slot],
                        SAPP_NAME_CAP) == 0) {
                s_pick.cursor = i;
                break;
            }
        }
    }
}

static void open_name_pick(uint8_t slot) {
    if (slot >= SAPP_LOBBY_SLOTS) slot = 0;
    s_pick.slot = slot;
    s_pick.inited = true;
    pick_rebuild_options(slot);
    s_lobby.kbd_owner_slot = slot;
    s_lobby.view = SAPP_LOBBY_VIEW_NAME_PICK;
}

static const char* pick_current_preview(void) {
    if (s_pick.n_options == 0) return NULL;
    return s_pick.options[s_pick.cursor];
}

static void pick_commit(void) {
    const char* name = pick_current_preview();
    if (!name) return;
    copy_name(s_lobby.seated_name[s_pick.slot], name);
    s_lobby.seated[s_pick.slot] = true;
    if (s_pick.slot == 0) {
        sapp_identity_t id;
        const sapp_identity_t* cur = sapp_get_identity();
        if (cur) id = *cur; else sapp_identity_default(&id);
        copy_name(id.current_name, name);
        sapp_identity_add_name(&id, name);
        sapp_set_identity(&id);
        (void)sapp_identity_save(&id);
    }
    s_lobby.kbd_owner_slot = SAPP_LOBBY_NO_OWNER;
    s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
    s_pick.inited = false;
}

static void pick_cancel(void) {
    if (s_pick.slot != 0) {
        s_lobby.seated[s_pick.slot] = false;
        memset(s_lobby.seated_name[s_pick.slot], 0,
               sizeof(s_lobby.seated_name[s_pick.slot]));
    }
    s_lobby.kbd_owner_slot = SAPP_LOBBY_NO_OWNER;
    s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
    s_pick.inited = false;
}

/* ------------------------------------------------------------------
 * Keyboard helpers (NEW_NAME + ROOM_CREATE).
 * ------------------------------------------------------------------ */

static void open_new_name_kbd(void) {
    sapp_kbd_layout_t L = SAPP_KBD_LAYOUT_RIGHT_HALF(true);
    sapp_kbd_init(&s_kbd.kbd, NULL, L);
    s_kbd.inited = true;
    s_lobby.view = SAPP_LOBBY_VIEW_NEW_NAME_KBD;
}

static void new_name_commit(void) {
    sapp_identity_t id;
    const sapp_identity_t* cur = sapp_get_identity();
    if (cur) id = *cur; else sapp_identity_default(&id);
    sapp_identity_add_name(&id, s_kbd.kbd.buffer);
    sapp_set_identity(&id);
    (void)sapp_identity_save(&id);

    /* Re-enter NAME_PICK so options pick up the new entry, and seat
     * the picker cursor on the new name. */
    char picked[SAPP_NAME_CAP];
    copy_name(picked, s_kbd.kbd.buffer);
    open_name_pick(s_pick.slot);
    for (uint8_t i = 0; i < s_pick.n_options; ++i) {
        if (strncmp(s_pick.options[i], picked, SAPP_NAME_CAP) == 0) {
            s_pick.cursor = i;
            break;
        }
    }
    s_kbd.inited = false;
}

static void open_room_create_kbd(void) {
    sapp_kbd_layout_t L = SAPP_KBD_DEFAULT_LAYOUT(true);
    L.cap_chars = SAPP_ROOM_NAME_CAP - 1;
    if (L.cap_chars > SAPP_KBD_BUF_CAP - 1) L.cap_chars = SAPP_KBD_BUF_CAP - 1;
    sapp_kbd_init(&s_kbd.kbd, NULL, L);
    s_kbd.inited = true;
    s_lobby.view = SAPP_LOBBY_VIEW_ROOM_CREATE_KBD;
    /* Clear any prior room state. */
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    memset(&ctx->room, 0, sizeof(ctx->room));
    ctx->joining  = false;
    ctx->creating = false;
}

/* ------------------------------------------------------------------
 * Connect / network helpers.
 * ------------------------------------------------------------------ */

static void enter_connecting(void) {
    sapp_online_ctx_reset();
    sapp_online_set_status("CONNECTING...");
    sapp_online_install_recv();
    s_connect_attempted = 0;
    s_lobby.view = SAPP_LOBBY_VIEW_CONNECTING;
}

static void leave_connect_view(void) {
    sapp_net_disconnect();
    s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
}

static void open_lobby_list(void) {
    s_list_cursor = 0;
    sapp_online_set_status("ROOM LIST");
    /* Send LOBBY_LIST_REQ. */
    uint8_t buf[4];
    size_t  n = sapp_proto_encode_lobby_list_req(buf, sizeof(buf), 0xFF, 0);
    if (n) sapp_net_send_frame(buf, n);
    s_lobby.view = SAPP_LOBBY_VIEW_LOBBY_LIST;
}

static void send_room_leave(void) {
    uint8_t buf[2];
    size_t  n = sapp_proto_encode_room_leave(buf, sizeof(buf));
    if (n) (void)sapp_net_send_frame(buf, n);
}

static void send_ready(uint8_t ready, uint8_t game_id) {
    uint8_t buf[4];
    size_t n = sapp_proto_encode_ready(buf, sizeof(buf), ready, game_id);
    if (n) (void)sapp_net_send_frame(buf, n);
}

/* ------------------------------------------------------------------
 * Vote / start game logic.
 * ------------------------------------------------------------------ */

static bool offline_majority_ready(void);

static void pad_commit_vote(uint8_t pad, uint8_t game_id) {
    if (pad >= SAPP_LOBBY_SLOTS) return;
    if (!s_lobby.seated[pad]) return;
    if (game_id >= sapp_count_games()) return;

    /* Bug 2 fix: pressing START/A on the same game you're already voting
     * for toggles the ready bit off (un-ready). Pressing it on a
     * different game switches the vote and stays ready. This works both
     * before majority and during the 5s vote-extension timer; once the
     * COUNTDOWN view is active the input dispatcher routes elsewhere
     * and votes are locked. */
    if (s_lobby.ready[pad] && s_lobby.vote_game_id[pad] == game_id) {
        s_lobby.ready[pad]        = false;
        s_lobby.vote_game_id[pad] = SAPP_LOBBY_NO_OWNER;
        if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
            send_ready(0, game_id);
        }
        /* Cancel any in-flight offline vote-extension timer if we
         * dropped below majority. offline_tick_timers handles this on
         * its own next frame, but explicitly clearing avoids a 1-frame
         * mismatch in the displayed timer. */
        if (s_lobby.mode == SAPP_LOBBY_MODE_OFFLINE
         && !offline_majority_ready()) {
            s_offline_vote_frames = 0;
        }
        return;
    }

    s_lobby.vote_game_id[pad] = game_id;
    s_lobby.ready[pad]        = true;

    if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
        send_ready(1, game_id);
    }
}

static void offline_pick_winner(void) {
    /* Mario-Kart-style weighted random over seated+ready votes. */
    uint8_t total = 0;
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        if (s_lobby.seated[i] && s_lobby.ready[i]) total++;
    }
    if (total == 0) {
        s_offline_picked_game = 0;
        return;
    }
    /* tiny LCG */
    s_offline_seed = s_offline_seed * 1103515245u + 12345u;
    uint8_t pick = (uint8_t)((s_offline_seed >> 8) % total);
    uint8_t k = 0;
    for (uint8_t i = 0; i < SAPP_LOBBY_SLOTS; ++i) {
        if (s_lobby.seated[i] && s_lobby.ready[i]) {
            if (k == pick) {
                s_offline_picked_game = s_lobby.vote_game_id[i];
                return;
            }
            k++;
        }
    }
    s_offline_picked_game = 0;
}

static bool offline_majority_ready(void) {
    uint8_t seated = sapp_lobby_seated_count();
    uint8_t ready  = sapp_lobby_ready_count();
    if (seated == 0) return false;
    return (ready * 2u) > seated;
}

static void offline_tick_timers(void) {
    if (s_lobby.mode != SAPP_LOBBY_MODE_OFFLINE) {
        s_offline_vote_frames = 0;
        s_offline_countdown_frames = 0;
        return;
    }
    if (s_offline_countdown_frames > 0) {
        s_offline_countdown_frames--;
        if (s_offline_countdown_frames == 0) {
            /* Launch the picked game. */
            s_pending_pending = true;
            s_pending_next_state = LOBBY_STATE_PLAYING;
            s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
        }
        return;
    }
    if (s_offline_vote_frames > 0) {
        s_offline_vote_frames--;
        if (!offline_majority_ready()) {
            /* lost majority — cancel */
            s_offline_vote_frames = 0;
            s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
        } else if (s_offline_vote_frames == 0) {
            offline_pick_winner();
            s_offline_countdown_frames = OFFLINE_COUNTDOWN_FRAMES;
            s_lobby.view = SAPP_LOBBY_VIEW_COUNTDOWN;
        }
        return;
    }
    /* No timer running. Check if majority is ready and start one. */
    if (offline_majority_ready() && sapp_count_games() > 0) {
        /* Solo offline: skip the 5s vote-extension window — there's no
         * one else to weigh in. Go straight to the 3s countdown so the
         * player has a moment to brace before the game launches. */
        if (sapp_lobby_seated_count() == 1) {
            offline_pick_winner();
            s_offline_countdown_frames = OFFLINE_COUNTDOWN_FRAMES;
            s_lobby.view = SAPP_LOBBY_VIEW_COUNTDOWN;
        } else {
            s_offline_vote_frames = OFFLINE_VOTE_FRAMES;
        }
    }
}

/* Get the currently-selected game id under pad 1's cursor (used as
 * the default game id for any pad's START commit). */
static uint8_t pad1_focused_game(void) {
    uint8_t n = sapp_count_games();
    if (n == 0) return 0;
    if (s_lobby.cursor_game >= n) return 0;
    return s_lobby.cursor_game;
}

/* Helper: which game does pad N's START commit a vote for?
 *   - If pad 1's focus is on GAMES or ACTION-row START, use cursor_game.
 *   - Else default to game 0 (or pad N's last vote if it had one). */
static uint8_t pad_vote_target(uint8_t pad) {
    uint8_t n = sapp_count_games();
    if (n == 0) return 0;
    if (s_lobby.vote_game_id[pad] != SAPP_LOBBY_NO_OWNER
     && s_lobby.vote_game_id[pad] < n) {
        return s_lobby.vote_game_id[pad];
    }
    return pad1_focused_game();
}

/* ------------------------------------------------------------------
 * Per-pad START handler.
 * ------------------------------------------------------------------ */

static void handle_pad_start(uint8_t pad) {
    if (pad >= SAPP_LOBBY_SLOTS) return;
    if (!s_lobby.seated[pad]) {
        /* Seat in pad's own slot index if free, else next free. */
        uint8_t slot = pad;
        if (s_lobby.seated[slot]) {
            uint8_t f = next_free_slot();
            if (f == SAPP_LOBBY_NO_OWNER) return;
            slot = f;
        }
        s_lobby.cursor_player = slot;
        s_lobby.focus = SAPP_LOBBY_FOCUS_PLAYERS;
        open_name_pick(slot);
        return;
    }
    /* Seated: commit vote on focused game (or pad's own last vote
     * if pad 1 isn't focused on a game). */
    uint8_t target = pad_vote_target(pad);
    pad_commit_vote(pad, target);
}

/* ------------------------------------------------------------------
 * View input dispatch.
 * ------------------------------------------------------------------ */

static lobby_state_t input_default(
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev[LOBBY_MAX_PLAYERS])
{
    lobby_input_t menu_now  = inputs ? inputs[0] : 0;
    lobby_input_t menu_prev = prev   ? prev[0]   : 0;

    /* Pad 1 cursor nav. */
    if (pressed(menu_now, menu_prev, INP_UP))    cursor_move(0, -1);
    if (pressed(menu_now, menu_prev, INP_DOWN))  cursor_move(0, +1);
    if (pressed(menu_now, menu_prev, INP_LEFT))  cursor_move(-1, 0);
    if (pressed(menu_now, menu_prev, INP_RIGHT)) cursor_move(+1, 0);

    /* Pad 1 A. */
    if (pressed(menu_now, menu_prev, INP_A)) {
        switch (s_lobby.focus) {
        case SAPP_LOBBY_FOCUS_PLAYERS:
            open_name_pick(s_lobby.cursor_player);
            break;
        case SAPP_LOBBY_FOCUS_GAMES:
            /* Bug 3 fix: A and START both commit a vote on the focused
             * game. Splitting "A=highlight, START=commit" was confusing
             * — pressing A produced no visible response and the user
             * had to discover that START was the actual commit key. */
            handle_pad_start(0);
            break;
        case SAPP_LOBBY_FOCUS_ACTION:
            if (s_lobby.cursor_action == 0) {
                /* CONNECT (offline) or LEAVE (online). */
                if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
                    send_room_leave();
                    sapp_online_ctx_reset();
                    refresh_mode();
                    s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
                } else {
                    enter_connecting();
                }
            } else {
                /* START button: equivalent to pad 1 START on focused game. */
                handle_pad_start(0);
            }
            break;
        }
    }

    /* Per-pad START. */
    for (uint8_t pad = 0; pad < LOBBY_MAX_PLAYERS; ++pad) {
        lobby_input_t pn = inputs ? inputs[pad] : 0;
        lobby_input_t pp = prev   ? prev[pad]   : 0;
        if (pressed(pn, pp, INP_START)) {
            handle_pad_start(pad);
        }
    }

    /* Tick offline timers. */
    offline_tick_timers();

    /* Online lifecycle: pump net + watch for game-pick / start. */
    if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
        sapp_net_poll();
        sapp_online_ctx_t* ctx = sapp_online_ctx();
        if (ctx->vote_timer_secs != 0xFF
         || ctx->countdown_secs  != 0xFF) {
            s_lobby.view = SAPP_LOBBY_VIEW_COUNTDOWN;
        }
        if (ctx->game_start_pending) {
            return LOBBY_STATE_PLAYING_ONLINE;
        }
    }

    if (s_pending_pending) {
        s_pending_pending = false;
        return s_pending_next_state;
    }
    return LOBBY_STATE_LOBBY;
}

static lobby_state_t input_name_pick(lobby_input_t now, lobby_input_t prev) {
    if (!s_pick.inited) { s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT; return LOBBY_STATE_LOBBY; }
    if (pressed(now, prev, INP_LEFT) && s_pick.n_options > 0) {
        s_pick.cursor = (uint8_t)((s_pick.cursor + s_pick.n_options - 1) % s_pick.n_options);
    }
    if (pressed(now, prev, INP_RIGHT) && s_pick.n_options > 0) {
        s_pick.cursor = (uint8_t)((s_pick.cursor + 1) % s_pick.n_options);
    }
    if (pressed(now, prev, INP_A)) {
        if (pick_current_preview()) pick_commit();
    } else if (pressed(now, prev, INP_START)) {
        open_new_name_kbd();
    } else if (pressed(now, prev, INP_B)) {
        pick_cancel();
    }
    return LOBBY_STATE_LOBBY;
}

static lobby_state_t input_new_name_kbd(lobby_input_t now, lobby_input_t prev) {
    if (!s_kbd.inited) { s_lobby.view = SAPP_LOBBY_VIEW_NAME_PICK; return LOBBY_STATE_LOBBY; }
    sapp_kbd_input(&s_kbd.kbd, now, prev);
    if (s_kbd.kbd.committed) {
        new_name_commit();
    } else if (s_kbd.kbd.cancelled) {
        s_kbd.inited = false;
        s_lobby.view = SAPP_LOBBY_VIEW_NAME_PICK;
    }
    return LOBBY_STATE_LOBBY;
}

static lobby_state_t input_connecting(lobby_input_t now, lobby_input_t prev) {
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    if (pressed(now, prev, INP_B)) {
        leave_connect_view();
        return LOBBY_STATE_LOBBY;
    }
    if (!sapp_net_active()) {
        sapp_online_set_status("NO NETWORK BACKEND");
        return LOBBY_STATE_LOBBY;
    }
    if (!s_connect_attempted) {
        s_connect_attempted = 1;
        if (!sapp_net_connect()) {
            sapp_online_set_status("CONNECT FAILED -- B BACK");
            return LOBBY_STATE_LOBBY;
        }
        if (!sapp_online_send_hello()) {
            sapp_online_set_status("HELLO SEND FAILED -- B BACK");
            return LOBBY_STATE_LOBBY;
        }
        sapp_online_set_status("WAITING HELLO_ACK...");
    }
    sapp_net_poll();
    if (ctx->handshaken) {
        open_lobby_list();
    }
    return LOBBY_STATE_LOBBY;
}

static lobby_state_t input_lobby_list(lobby_input_t now, lobby_input_t prev) {
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    sapp_net_poll();
    /* If a JOIN reply landed (room valid && not joining/creating) — switch to default+online mode. */
    if (ctx->room.valid && !ctx->joining && !ctx->creating) {
        refresh_mode();
        s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
        return LOBBY_STATE_LOBBY;
    }
    if (pressed(now, prev, INP_B)) {
        sapp_net_disconnect();
        sapp_online_ctx_reset();
        refresh_mode();
        s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
        return LOBBY_STATE_LOBBY;
    }
    if (pressed(now, prev, INP_START)) {
        open_room_create_kbd();
        return LOBBY_STATE_LOBBY;
    }
    if (ctx->list.n > 0) {
        if (pressed(now, prev, INP_UP)   && s_list_cursor > 0) s_list_cursor--;
        if (pressed(now, prev, INP_DOWN) && s_list_cursor + 1 < ctx->list.n) s_list_cursor++;
        if (pressed(now, prev, INP_A)) {
            const sapp_room_summary_t* r = &ctx->list.rooms[s_list_cursor];
            uint8_t buf[16];
            size_t  n = sapp_proto_encode_room_join(buf, sizeof(buf), r->room_id);
            if (n && sapp_net_send_frame(buf, n)) {
                ctx->joining = true;
                sapp_online_set_status("JOINING...");
            } else {
                sapp_online_set_status("JOIN SEND FAILED");
            }
        }
    }
    return LOBBY_STATE_LOBBY;
}

static lobby_state_t input_room_create_kbd(lobby_input_t now, lobby_input_t prev) {
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    sapp_net_poll();
    if (!s_kbd.inited) { s_lobby.view = SAPP_LOBBY_VIEW_LOBBY_LIST; return LOBBY_STATE_LOBBY; }
    sapp_kbd_input(&s_kbd.kbd, now, prev);
    /* Mirror buffer for tests. */
    {
        size_t i = 0;
        for (; i + 1 < sizeof(ctx->room_create_buf) && s_kbd.kbd.buffer[i]; ++i)
            ctx->room_create_buf[i] = s_kbd.kbd.buffer[i];
        ctx->room_create_buf[i] = '\0';
    }
    if (s_kbd.kbd.committed) {
        uint8_t buf[40];
        size_t  n = sapp_proto_encode_room_create(buf, sizeof(buf),
                                                  /*game_id*/ 0,
                                                  /*max_slots*/ SAPP_ROOM_SLOTS_MAX,
                                                  /*visibility*/ SAPP_VIS_PUBLIC,
                                                  s_kbd.kbd.buffer);
        s_kbd.inited = false;
        if (n && sapp_net_send_frame(buf, n)) {
            ctx->creating = true;
            sapp_online_set_status("CREATING...");
        } else {
            sapp_online_set_status("CREATE SEND FAILED");
        }
        s_lobby.view = SAPP_LOBBY_VIEW_LOBBY_LIST;
    } else if (s_kbd.kbd.cancelled) {
        s_kbd.inited = false;
        s_lobby.view = SAPP_LOBBY_VIEW_LOBBY_LIST;
    }
    return LOBBY_STATE_LOBBY;
}

static lobby_state_t input_countdown(lobby_input_t now, lobby_input_t prev) {
    (void)now; (void)prev;
    /* Online: server-driven; just pump and watch for GAME_START. */
    if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
        sapp_online_ctx_t* ctx = sapp_online_ctx();
        sapp_net_poll();
        if (ctx->game_start_pending) {
            return LOBBY_STATE_PLAYING_ONLINE;
        }
        if (ctx->vote_timer_secs == 0xFF
         && ctx->countdown_secs  == 0xFF
         && !ctx->game_pick_pending) {
            /* Server cancelled — back to default view. */
            s_lobby.view = SAPP_LOBBY_VIEW_DEFAULT;
        }
        return LOBBY_STATE_LOBBY;
    }
    /* Offline: tick-driven. */
    offline_tick_timers();
    if (s_pending_pending) {
        s_pending_pending = false;
        return s_pending_next_state;
    }
    return LOBBY_STATE_LOBBY;
}

/* ------------------------------------------------------------------
 * Public input entry point.
 * ------------------------------------------------------------------ */

lobby_state_t sapp_state_lobby_input(
    const lobby_input_t inputs[LOBBY_MAX_PLAYERS],
    const lobby_input_t prev  [LOBBY_MAX_PLAYERS])
{
    if (!s_inited) sapp_state_lobby_enter();
    refresh_mode();

    /* Server-driven hand-off can land at any time: if a GAME_START
     * has been queued, jump straight to PLAYING_ONLINE regardless of
     * the current view. */
    {
        sapp_online_ctx_t* ctx = sapp_online_ctx();
        if (ctx->game_start_pending) {
            return LOBBY_STATE_PLAYING_ONLINE;
        }
    }

    lobby_input_t menu_now  = inputs ? inputs[0] : 0;
    lobby_input_t menu_prev = prev   ? prev[0]   : 0;

    switch (s_lobby.view) {
    case SAPP_LOBBY_VIEW_DEFAULT:         return input_default(inputs, prev);
    case SAPP_LOBBY_VIEW_NAME_PICK:       return input_name_pick(menu_now, menu_prev);
    case SAPP_LOBBY_VIEW_NEW_NAME_KBD:    return input_new_name_kbd(menu_now, menu_prev);
    case SAPP_LOBBY_VIEW_CONNECTING:      return input_connecting(menu_now, menu_prev);
    case SAPP_LOBBY_VIEW_LOBBY_LIST:      return input_lobby_list(menu_now, menu_prev);
    case SAPP_LOBBY_VIEW_ROOM_CREATE_KBD: return input_room_create_kbd(menu_now, menu_prev);
    case SAPP_LOBBY_VIEW_COUNTDOWN:       return input_countdown(menu_now, menu_prev);
    }
    return LOBBY_STATE_LOBBY;
}

/* ==================================================================
 * Rendering.
 * ================================================================ */

static void render_players_column(lobby_scene_t* scene, bool show_cursor) {
    /* Optional ROOM line (online). */
    if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
        const sapp_room_state_t* rs = sapp_online_room_state();
        char rid[SAPP_ROOM_ID_LEN + 1] = {0};
        if (rs && rs->valid) {
            for (uint8_t i = 0; i < SAPP_ROOM_ID_LEN; ++i) {
                uint8_t c = rs->room_id[i];
                rid[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
            }
        }
        char buf[24];
        snprintf(buf, sizeof(buf), "ROOM %s", rid[0] ? rid : "?");
        lobby_scene_text(scene, COL_PLAYERS_X, ROOM_ROW, PAL_DEFAULT, buf);
    }

    lobby_scene_text(scene, COL_PLAYERS_X, COL_HEADER_ROW,
                     PAL_HIGHLIGHT, "PLAYERS");

    /* Online mode: render the server-authoritative member list. The
     * cart's own focus cursor still works against the SLOTS index. */
    if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
        const sapp_room_state_t* rs = sapp_online_room_state();
        bool member_in_slot[SAPP_LOBBY_SLOTS] = {0};
        char names[SAPP_LOBBY_SLOTS][SAPP_NAME_CAP] = {{0}};
        if (rs && rs->valid) {
            for (uint8_t i = 0; i < rs->n_members && i < SAPP_LOBBY_SLOTS; ++i) {
                uint8_t s = rs->members[i].slot;
                if (s >= SAPP_LOBBY_SLOTS) continue;
                member_in_slot[s] = true;
                size_t k;
                for (k = 0; k < SAPP_NAME_CAP - 1 && rs->members[i].name[k]; ++k)
                    names[s][k] = rs->members[i].name[k];
                names[s][k] = '\0';
            }
        }
        for (uint8_t slot = 0; slot < SAPP_LOBBY_SLOTS; ++slot) {
            char buf[24];
            bool focused = show_cursor
                        && s_lobby.focus == SAPP_LOBBY_FOCUS_PLAYERS
                        && s_lobby.cursor_player == slot;
            const char* arrow = focused ? ">" : " ";
            uint8_t row = (uint8_t)(COL_ROW_FIRST + slot * COL_ROW_STRIDE);
            uint8_t pal = focused ? PAL_HIGHLIGHT
                                  : (member_in_slot[slot] ? PAL_DEFAULT : PAL_DIM);
            if (member_in_slot[slot]) {
                snprintf(buf, sizeof(buf), "%s %u. %.10s",
                         arrow, (unsigned)(slot + 1), names[slot]);
            } else {
                snprintf(buf, sizeof(buf), "%s %u. ----",
                         arrow, (unsigned)(slot + 1));
            }
            buf[19] = '\0';
            lobby_scene_text(scene, COL_PLAYERS_X, row, pal, buf);
        }
        lobby_scene_text(scene, COL_PLAYERS_X, HINT_ROW, PAL_DIM,
                         "PRESS START TO JOIN");
        return;
    }

    /* Offline mode: render local seating. */
    for (uint8_t slot = 0; slot < SAPP_LOBBY_SLOTS; ++slot) {
        char buf[24];
        bool focused = show_cursor
                    && s_lobby.focus == SAPP_LOBBY_FOCUS_PLAYERS
                    && s_lobby.cursor_player == slot;
        const char* arrow = focused ? ">" : " ";
        uint8_t row = (uint8_t)(COL_ROW_FIRST + slot * COL_ROW_STRIDE);
        uint8_t pal = focused ? PAL_HIGHLIGHT
                              : (s_lobby.seated[slot] ? PAL_DEFAULT : PAL_DIM);
        if (s_lobby.seated[slot]) {
            char ready_tag = s_lobby.ready[slot] ? '*' : ' ';
            snprintf(buf, sizeof(buf), "%s %u. %.10s%c",
                     arrow, (unsigned)(slot + 1),
                     s_lobby.seated_name[slot], ready_tag);
        } else {
            snprintf(buf, sizeof(buf), "%s %u. ----",
                     arrow, (unsigned)(slot + 1));
        }
        buf[19] = '\0';
        lobby_scene_text(scene, COL_PLAYERS_X, row, pal, buf);
    }
    lobby_scene_text(scene, COL_PLAYERS_X, HINT_ROW, PAL_DIM,
                     "PRESS START TO JOIN");
}

static void render_games_column(lobby_scene_t* scene, bool show_cursor) {
    uint8_t n_games = sapp_count_games();
    lobby_scene_text(scene, COL_GAMES_X, COL_HEADER_ROW,
                     PAL_HIGHLIGHT, "GAMES");
    if (n_games == 0) {
        lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST,
                         PAL_DIM, "(no games)");
        return;
    }
    /* Pad 0's vote target — used to mark "this is the game YOU voted for"
     * separately from the cursor `>` (Bug 3: previously voting and
     * hovering looked identical). */
    uint8_t self_vote = s_lobby.ready[0] ? s_lobby.vote_game_id[0]
                                         : SAPP_LOBBY_NO_OWNER;
    for (uint8_t g = 0; g < n_games && g < 12; ++g) {
        const lobby_game_t* gm = sapp_get_game(g);
        char buf[24];
        bool focused = show_cursor
                    && s_lobby.focus == SAPP_LOBBY_FOCUS_GAMES
                    && s_lobby.cursor_game == g;
        const char* arrow = focused ? ">" : " ";
        uint8_t row = (uint8_t)(COL_ROW_FIRST + g * COL_ROW_STRIDE);
        uint8_t pal = focused ? PAL_HIGHLIGHT : PAL_DEFAULT;
        const char* name = (gm && gm->display_name) ? gm->display_name : "?";
        uint8_t votes = sapp_lobby_vote_count_for_game(g);
        char self_mark = (self_vote == g) ? '*' : ' ';
        if (votes > 0) {
            snprintf(buf, sizeof(buf), "%s%c%.10s %u",
                     arrow, self_mark, name, (unsigned)votes);
        } else {
            snprintf(buf, sizeof(buf), "%s%c%.10s",
                     arrow, self_mark, name);
        }
        lobby_scene_text(scene, COL_GAMES_X, row, pal, buf);
    }
}

static void render_action_row(lobby_scene_t* scene) {
    bool focused = (s_lobby.focus == SAPP_LOBBY_FOCUS_ACTION);
    if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
        bool hl = focused && s_lobby.cursor_action == 0;
        char buf[16];
        snprintf(buf, sizeof(buf), "%s [LEAVE]", hl ? ">" : " ");
        lobby_scene_text(scene, COL_PLAYERS_X, ACTION_ROW,
                         hl ? PAL_HIGHLIGHT : PAL_DEFAULT, buf);
    } else {
        bool hl_c = focused && s_lobby.cursor_action == 0;
        bool hl_s = focused && s_lobby.cursor_action == 1;
        char buf[16];
        snprintf(buf, sizeof(buf), "%s [CONNECT]", hl_c ? ">" : " ");
        lobby_scene_text(scene, COL_PLAYERS_X, ACTION_ROW,
                         hl_c ? PAL_HIGHLIGHT : PAL_DEFAULT, buf);
        snprintf(buf, sizeof(buf), "%s [START]", hl_s ? ">" : " ");
        lobby_scene_text(scene, 14, ACTION_ROW,
                         hl_s ? PAL_HIGHLIGHT : PAL_DEFAULT, buf);
    }
}

static void render_status_line(lobby_scene_t* scene) {
    /* Offline countdown / vote-timer status. */
    if (s_lobby.mode == SAPP_LOBBY_MODE_OFFLINE) {
        if (s_offline_countdown_frames > 0) {
            char buf[24];
            uint32_t secs = (s_offline_countdown_frames + FRAMES_PER_SEC - 1) / FRAMES_PER_SEC;
            snprintf(buf, sizeof(buf), "STARTING IN %u...", (unsigned)secs);
            lobby_scene_text(scene, COL_PLAYERS_X, STATUS_ROW, PAL_HIGHLIGHT, buf);
        } else if (s_offline_vote_frames > 0) {
            char buf[24];
            uint32_t secs = (s_offline_vote_frames + FRAMES_PER_SEC - 1) / FRAMES_PER_SEC;
            snprintf(buf, sizeof(buf), "VOTE TIMER %u", (unsigned)secs);
            lobby_scene_text(scene, COL_PLAYERS_X, STATUS_ROW, PAL_DIM, buf);
        }
        return;
    }
    /* Online: show server timer + status. */
    sapp_online_ctx_t* ctx = sapp_online_ctx();
    if (ctx->countdown_secs != 0xFF) {
        char buf[24];
        snprintf(buf, sizeof(buf), "STARTING IN %u...",
                 (unsigned)(ctx->countdown_secs & 0xFF));
        lobby_scene_text(scene, COL_PLAYERS_X, STATUS_ROW, PAL_HIGHLIGHT, buf);
    } else if (ctx->vote_timer_secs != 0xFF) {
        char buf[24];
        snprintf(buf, sizeof(buf), "VOTE TIMER %u",
                 (unsigned)(ctx->vote_timer_secs & 0xFF));
        lobby_scene_text(scene, COL_PLAYERS_X, STATUS_ROW, PAL_DIM, buf);
    } else {
        const char* st = sapp_online_status();
        if (st && st[0]) {
            lobby_scene_text(scene, COL_PLAYERS_X, STATUS_ROW, PAL_DIM, st);
        }
    }
}

static void render_name_pick_panel(lobby_scene_t* scene) {
    char buf[24];
    snprintf(buf, sizeof(buf), "PICK NAME SLOT %u",
             (unsigned)(s_pick.slot + 1));
    lobby_scene_text(scene, COL_GAMES_X, COL_HEADER_ROW, PAL_HIGHLIGHT, buf);
    if (s_pick.n_options == 0) {
        lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST,
                         PAL_DIM, "(no saved names)");
    } else {
        char nbuf[SAPP_NAME_CAP + 4];
        snprintf(nbuf, sizeof(nbuf), "< %s >", s_pick.options[s_pick.cursor]);
        lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST, PAL_DEFAULT, nbuf);
    }
    lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST + 2, PAL_DIM, "[Y] NEW NAME");
    lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST + 3, PAL_DIM, "[A] CONFIRM");
    lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST + 4, PAL_DIM, "[B] CANCEL");
}

static void render_new_name_kbd_panel(lobby_scene_t* scene) {
    sapp_kbd_render(&s_kbd.kbd, scene, "NEW NAME");
}

static void render_connecting_panel(lobby_scene_t* scene) {
    lobby_scene_text(scene, COL_GAMES_X, COL_HEADER_ROW,
                     PAL_HIGHLIGHT, "CONNECTING");
    const char* st = sapp_online_status();
    if (st && st[0]) {
        lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST, PAL_DEFAULT, st);
    }
    lobby_scene_text(scene, COL_GAMES_X, ACTION_ROW, PAL_DIM, "B: BACK");
}

static void render_lobby_list_panel(lobby_scene_t* scene) {
    const sapp_online_ctx_t* ctx = sapp_online_ctx();
    lobby_scene_text(scene, COL_GAMES_X, COL_HEADER_ROW,
                     PAL_HIGHLIGHT, "ROOMS");
    if (ctx->list.n == 0) {
        lobby_scene_text(scene, COL_GAMES_X, COL_ROW_FIRST,
                         PAL_DIM, "(no rooms)");
    } else {
        for (uint8_t i = 0; i < ctx->list.n && i < 8; ++i) {
            const sapp_room_summary_t* r = &ctx->list.rooms[i];
            char buf[24];
            snprintf(buf, sizeof(buf), "%s %.10s %u/%u",
                     (i == s_list_cursor) ? ">" : " ",
                     r->name[0] ? r->name : "(no name)",
                     (unsigned)r->occ, (unsigned)r->max);
            lobby_scene_text(scene, COL_GAMES_X,
                             (uint8_t)(COL_ROW_FIRST + i * COL_ROW_STRIDE),
                             (i == s_list_cursor) ? PAL_HIGHLIGHT : PAL_DEFAULT,
                             buf);
        }
    }
    lobby_scene_text(scene, COL_GAMES_X, ACTION_ROW, PAL_DIM,
                     "A:JOIN START:NEW B:BACK");
    const char* st = sapp_online_status();
    if (st && st[0]) {
        lobby_scene_text(scene, COL_PLAYERS_X, STATUS_ROW, PAL_DIM, st);
    }
}

static void render_room_create_panel(lobby_scene_t* scene) {
    sapp_kbd_render(&s_kbd.kbd, scene, "ROOM NAME");
}

static void render_countdown_panel(lobby_scene_t* scene) {
    /* Countdown displays in the GAMES column under the header. */
    const lobby_game_t* g = NULL;
    if (s_lobby.mode == SAPP_LOBBY_MODE_ONLINE) {
        g = sapp_get_game(sapp_online_picked_game_id());
    } else {
        g = sapp_get_game(s_offline_picked_game);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "GAME: %s",
             (g && g->display_name) ? g->display_name : "?");
    lobby_scene_text(scene, COL_GAMES_X, COL_HEADER_ROW, PAL_HIGHLIGHT, buf);
    /* Status line carries the countdown text (rendered by status helper). */
}

void sapp_state_lobby_render(lobby_scene_t* scene)
{
    if (!s_inited) sapp_state_lobby_enter();
    if (!scene) return;
    refresh_mode();

    /* Players column always renders. The GAMES column is replaced by
     * the right-panel view if not DEFAULT. The action row + status line
     * are always rendered too (so countdown displays inline). */
    bool show_cursor = (s_lobby.view == SAPP_LOBBY_VIEW_DEFAULT
                     || s_lobby.view == SAPP_LOBBY_VIEW_COUNTDOWN);
    render_players_column(scene, show_cursor);
    render_action_row    (scene);
    render_status_line   (scene);

    switch (s_lobby.view) {
    case SAPP_LOBBY_VIEW_DEFAULT:
        render_games_column(scene, true);
        break;
    case SAPP_LOBBY_VIEW_NAME_PICK:
        render_name_pick_panel(scene);
        break;
    case SAPP_LOBBY_VIEW_NEW_NAME_KBD:
        render_new_name_kbd_panel(scene);
        break;
    case SAPP_LOBBY_VIEW_CONNECTING:
        render_connecting_panel(scene);
        break;
    case SAPP_LOBBY_VIEW_LOBBY_LIST:
        render_lobby_list_panel(scene);
        break;
    case SAPP_LOBBY_VIEW_ROOM_CREATE_KBD:
        render_room_create_panel(scene);
        break;
    case SAPP_LOBBY_VIEW_COUNTDOWN:
        render_games_column(scene, false);
        render_countdown_panel(scene);
        break;
    }
}
