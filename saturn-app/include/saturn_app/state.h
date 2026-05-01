/*
 * libs/saturn-app/state — top-level lobby state machine identifiers.
 *
 * Major UX redesign (2026-04-29): the previous chain of screens
 * (TITLE -> LOCAL_LOBBY -> SELECT -> IN_ROOM -> GAME_SELECT_ONLINE ->
 * VOTE_TIMER -> COUNTDOWN -> ...) collapsed into a single persistent
 * lobby screen with two columns (PLAYERS / GAMES) plus a contextual
 * right-panel takeover for keyboard / connect status. The unified
 * screen handles offline AND online play; the protocol-side state
 * tracking (READY / VOTE_TIMER / COUNTDOWN) lives inside the lobby
 * state's view enum, not as separate screens.
 *
 * The remaining top-level states are: first-run name entry, the
 * unified lobby, full-screen game takeover (offline + online), and
 * the offline GAME_OVER summary.
 */

#ifndef SATURN_APP_STATE_H
#define SATURN_APP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOBBY_STATE_NAME_ENTRY_FIRST_RUN = 0,
    LOBBY_STATE_LOBBY                = 1,  /* the unified screen */
    LOBBY_STATE_PLAYING              = 2,  /* offline single-game */
    LOBBY_STATE_GAME_OVER            = 3,  /* offline post-game */
    LOBBY_STATE_PLAYING_ONLINE       = 4,  /* lockstep PLAYING via BUNDLE */
    LOBBY_STATE_RESULTS_ONLINE       = 5   /* brief results before back */
} lobby_state_t;

#ifdef __cplusplus
}
#endif
#endif
