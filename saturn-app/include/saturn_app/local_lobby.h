/*
 * libs/saturn-app/local_lobby — unified lobby state container.
 *
 * The lobby is a single persistent screen with two columns (PLAYERS,
 * GAMES) and a contextual right-panel takeover for keyboard / connect /
 * room-list / room-create. The state machine owns one
 * sapp_local_lobby_t for the players column + cursor state, and a
 * separate `sapp_lobby_view_t` enum that selects what the right panel
 * shows.
 *
 * The struct is exposed read-only so the host harness and tests can
 * assert seating, cursor position, vote selection, etc. without parsing
 * the rendered scene.
 *
 * Slot 0 is always seated as the cart's identity.current_name.
 * Slots 1..7 are guest seats; pad N (N>=1) joins by pressing START
 * while the lobby is on screen.
 *
 * Cursor model: a single `>` indicator owned by pad 1. The cursor can
 * land on any of the 8 player slots (PLAYERS column), any of the
 * registered games (GAMES column), or the action row buttons
 * (CONNECT/LEAVE + START).
 */

#ifndef SATURN_APP_LOCAL_LOBBY_H
#define SATURN_APP_LOCAL_LOBBY_H

#include <stdbool.h>
#include <stdint.h>

#include <saturn_app/identity.h>     /* SAPP_NAME_CAP */
#include <saturn_app/game.h>         /* LOBBY_MAX_PLAYERS */

#ifdef __cplusplus
extern "C" {
#endif

#define SAPP_LOBBY_SLOTS    LOBBY_MAX_PLAYERS   /* 8 */
#define SAPP_LOBBY_NO_OWNER 0xFFu

/* Cursor focus: which column the cursor lives in. */
typedef enum {
    SAPP_LOBBY_FOCUS_PLAYERS = 0,   /* slot 0..SAPP_LOBBY_SLOTS-1 */
    SAPP_LOBBY_FOCUS_GAMES   = 1,   /* game index 0..n_games-1   */
    SAPP_LOBBY_FOCUS_ACTION  = 2    /* action row: 0=CONNECT/LEAVE, 1=START */
} sapp_lobby_focus_t;

/* Right-panel view. The PLAYERS column is always rendered; the
 * GAMES column is replaced by the selected view when not DEFAULT. */
typedef enum {
    SAPP_LOBBY_VIEW_DEFAULT          = 0,
    SAPP_LOBBY_VIEW_NAME_PICK        = 1,
    SAPP_LOBBY_VIEW_NEW_NAME_KBD     = 2,
    SAPP_LOBBY_VIEW_CONNECTING       = 3,
    SAPP_LOBBY_VIEW_LOBBY_LIST       = 4,
    SAPP_LOBBY_VIEW_ROOM_CREATE_KBD  = 5,
    SAPP_LOBBY_VIEW_COUNTDOWN        = 6
} sapp_lobby_view_t;

/* Mode: offline (no server) vs. online (in a room). The mode is
 * implied by online-context state (ctx->room.valid) but we cache it
 * here so renderers don't need to peek at the online ctx. */
typedef enum {
    SAPP_LOBBY_MODE_OFFLINE = 0,
    SAPP_LOBBY_MODE_ONLINE  = 1
} sapp_lobby_mode_t;

typedef struct {
    /* PLAYERS column. */
    char    seated_name[SAPP_LOBBY_SLOTS][SAPP_NAME_CAP];
    bool    seated[SAPP_LOBBY_SLOTS];
    uint8_t cart_color[SAPP_LOBBY_SLOTS];   /* palette index, slot==index */

    /* Cursor (pad 1). */
    sapp_lobby_focus_t focus;
    uint8_t            cursor_player;       /* 0..SAPP_LOBBY_SLOTS-1 */
    uint8_t            cursor_game;         /* 0..n_games-1 */
    uint8_t            cursor_action;       /* 0..1 */

    /* Right-panel view. */
    sapp_lobby_view_t  view;

    /* Per-pad vote state. vote_game_id[N] = game N's pad voted for, or
     * SAPP_LOBBY_NO_OWNER if no vote. ready[N] = pad N has committed
     * its vote (i.e. pressed START on a game). */
    uint8_t  vote_game_id[SAPP_LOBBY_SLOTS];
    bool     ready[SAPP_LOBBY_SLOTS];

    /* Mode + slot the keyboard / picker is owned by. */
    sapp_lobby_mode_t  mode;
    uint8_t            kbd_owner_slot;      /* SAPP_LOBBY_NO_OWNER when none */
} sapp_local_lobby_t;

/* Read-only snapshot of the lobby. NULL if the framework hasn't
 * entered the lobby yet. */
const sapp_local_lobby_t* sapp_lobby_get(void);

/* Number of currently seated slots (>=1 since slot 0 is always
 * seated once we reach the lobby). */
uint8_t sapp_lobby_seated_count(void);

/* Number of pads that have committed a vote (ready[]). */
uint8_t sapp_lobby_ready_count(void);

/* Per-game vote count, summed across seated pads. */
uint8_t sapp_lobby_vote_count_for_game(uint8_t game_id);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_APP_LOCAL_LOBBY_H */
