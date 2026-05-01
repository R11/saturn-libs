/*
 * libs/saturn-app/state — offline lobby state machine identifiers.
 * Online states layer on later.
 */

#ifndef SATURN_APP_STATE_H
#define SATURN_APP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOBBY_STATE_TITLE                = 0,
    LOBBY_STATE_SELECT               = 1,
    LOBBY_STATE_PLAYING              = 2,
    LOBBY_STATE_GAME_OVER            = 3,
    LOBBY_STATE_NAME_ENTRY_FIRST_RUN = 4,
    LOBBY_STATE_LOCAL_LOBBY          = 5,
    LOBBY_STATE_NAME_PICK            = 6,
    LOBBY_STATE_NAME_ENTRY_NEW       = 7,
    LOBBY_STATE_CONNECTING           = 8,  /* M4: drives HELLO/HELLO_ACK */
    LOBBY_STATE_LOBBY_LIST           = 9,  /* M4: server-driven public room list */
    LOBBY_STATE_ROOM_CREATE          = 10, /* M4: keyboard for room name */
    LOBBY_STATE_IN_ROOM              = 11, /* M4: post-JOIN flat member list */
    LOBBY_STATE_GAME_SELECT_ONLINE   = 12, /* M5: per-player game pick + READY */
    LOBBY_STATE_VOTE_TIMER           = 13, /* M5: 5s extension while >50% ready */
    LOBBY_STATE_COUNTDOWN            = 14, /* M5: 3s pre-game */
    LOBBY_STATE_PLAYING_ONLINE       = 15, /* M5: lockstep PLAYING via BUNDLE */
    LOBBY_STATE_RESULTS_ONLINE       = 16  /* M5: brief results before back to IN_ROOM */
} lobby_state_t;

#ifdef __cplusplus
}
#endif
#endif
