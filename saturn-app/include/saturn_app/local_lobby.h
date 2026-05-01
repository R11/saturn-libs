/*
 * libs/saturn-app/local_lobby — local-lobby state container.
 *
 * The state machine owns one sapp_local_lobby_t. It's exposed (read-only)
 * to the host harness so end-to-end tests can assert seating, cursor, etc.
 * without parsing rendered text.
 *
 * Layout (cols 0..19 = list, cols 20..39 = keyboard panel when open):
 *
 *   LOCAL LOBBY
 *   -------------
 *   > 1. ALICE
 *     2. BOB
 *     3. -- press start --
 *     ...
 *   -------------
 *     CONNECT
 *     PLAY OFFLINE   (temporary M3 dev-seam → SELECT, removed in M4)
 *
 * Slot 0 is always seated and equals the framework's identity current_name.
 * Slots 1..7 are guest seats; a guest pad joins by pressing START while the
 * lobby is on screen.
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

/* Cursor positions in the left-hand list. Slots are 0..7; the action row
 * lives at SAPP_LOBBY_CURSOR_CONNECT and (M3-only seam) PLAY_OFFLINE. */
enum {
    SAPP_LOBBY_CURSOR_SLOT0       = 0,
    SAPP_LOBBY_CURSOR_SLOT_LAST   = SAPP_LOBBY_SLOTS - 1,
    SAPP_LOBBY_CURSOR_CONNECT     = SAPP_LOBBY_SLOTS,           /* 8 */
    SAPP_LOBBY_CURSOR_PLAY_OFFLINE = SAPP_LOBBY_SLOTS + 1,      /* 9 — temp */
    SAPP_LOBBY_CURSOR_COUNT       = SAPP_LOBBY_SLOTS + 2
};

#define SAPP_LOBBY_NO_OWNER  0xFFu

typedef struct {
    char    seated_name[SAPP_LOBBY_SLOTS][SAPP_NAME_CAP];
    bool    seated[SAPP_LOBBY_SLOTS];
    uint8_t cart_color[SAPP_LOBBY_SLOTS];   /* palette index, slot==index for now */
    uint8_t cursor;                          /* SAPP_LOBBY_CURSOR_* */
    uint8_t kbd_owner_slot;                  /* slot the keyboard is editing for, or 0xFF */
} sapp_local_lobby_t;

/* Read-only snapshot of the lobby. NULL if the framework hasn't entered
 * the lobby yet. */
const sapp_local_lobby_t* sapp_lobby_get(void);

/* Number of currently seated slots (>=1 since slot 0 is always seated
 * once we reach the lobby). */
uint8_t sapp_lobby_seated_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_APP_LOCAL_LOBBY_H */
