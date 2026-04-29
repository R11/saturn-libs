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
    LOBBY_STATE_TITLE      = 0,
    LOBBY_STATE_SELECT     = 1,
    LOBBY_STATE_PLAYING    = 2,
    LOBBY_STATE_GAME_OVER  = 3
} lobby_state_t;

#ifdef __cplusplus
}
#endif
#endif
