/*
 * libs/saturn-app/game — the plug-in contract every game implements.
 *
 * v1 simplification of design/04-game-contract.md:
 *   - Single-player, offline focus.
 *   - inputs is a fixed [LOBBY_MAX_PLAYERS] array; games read [0] only
 *     for single-player.
 *   - No audio / save / hash hooks yet (will land alongside saturn-scsp /
 *     saturn-bup).
 */

#ifndef SATURN_APP_GAME_H
#define SATURN_APP_GAME_H

#include <stdint.h>
#include <stddef.h>

#include <saturn_app/scene.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOBBY_MAX_PLAYERS    8u

typedef uint16_t lobby_input_t;   /* matches saturn-smpc button bitmask */

typedef enum {
    LOBBY_OUTCOME_RUNNING = 0,
    LOBBY_OUTCOME_WINNER  = 1,
    LOBBY_OUTCOME_DRAW    = 2,
    LOBBY_OUTCOME_QUIT    = 3
} lobby_outcome_t;

typedef struct lobby_game_config {
    uint32_t seed;
    uint8_t  n_players;
    uint8_t  difficulty;
} lobby_game_config_t;

typedef struct lobby_game_result {
    uint8_t  outcome;        /* lobby_outcome_t */
    uint8_t  winner_slot;    /* valid when outcome == WINNER */
    uint32_t score[LOBBY_MAX_PLAYERS];
} lobby_game_result_t;

typedef struct lobby_game {
    /* Identity */
    const char*  id;
    const char*  display_name;
    uint8_t      min_players;
    uint8_t      max_players;
    size_t       state_size;        /* framework allocates this many bytes */

    /* Required hooks */
    void (*init)        (void* state, const lobby_game_config_t* cfg);
    void (*tick)        (void* state,
                         const lobby_input_t inputs[LOBBY_MAX_PLAYERS]);
    void (*render_scene)(const void* state, lobby_scene_t* out);
    void (*is_done)     (const void* state, lobby_game_result_t* out);
    void (*teardown)    (void* state);
} lobby_game_t;

#ifdef __cplusplus
}
#endif
#endif /* SATURN_APP_GAME_H */
