/*
 * libs/saturn-app/identity — single-record BUP-backed identity store.
 *
 * Holds the cart's online identity (RESUME uuid + current player name)
 * plus a small FIFO roster of saved names for fast guest-slot seating.
 *
 * Stored in BUP under the record name "LOBBY_ID". The on-disk layout is
 * the sapp_identity_t struct as-is — no marshalling — so any change to
 * the struct must bump SAPP_IDENTITY_VERSION and a load of the old
 * version is treated as "no record" (caller falls back to _default).
 *
 * Backend is whichever sbup PAL the host shell installed (host file
 * backend, or the Saturn BIOS PAL). The first sapp_identity_load() call
 * lazily brings up a process-singleton sbup handle.
 */

#ifndef SATURN_APP_IDENTITY_H
#define SATURN_APP_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAPP_IDENTITY_MAGIC    0x4C424944u   /* 'LBID' */
#define SAPP_IDENTITY_VERSION  1u
#define SAPP_IDENTITY_RECORD   "LOBBY_ID"

#define SAPP_NAME_CAP   11u   /* incl. NUL; max 10 visible chars */
#define SAPP_NAME_MAX    8u   /* roster capacity */

typedef struct {
    uint32_t magic;                              /* SAPP_IDENTITY_MAGIC */
    uint16_t version;                            /* SAPP_IDENTITY_VERSION */
    uint16_t reserved;
    uint8_t  session_uuid[16];                   /* online RESUME identity */
    char     current_name[SAPP_NAME_CAP];        /* null-terminated, ≤10 */
    uint8_t  name_count;                         /* 0..SAPP_NAME_MAX */
    char     names[SAPP_NAME_MAX][SAPP_NAME_CAP];/* FIFO roster, dedupe on add */
} sapp_identity_t;

/* Read the LOBBY_ID record. Returns false if the record is missing,
 * the magic is wrong, the version doesn't match, or the size doesn't
 * match sizeof(sapp_identity_t). On false the caller's *out is left
 * untouched and should typically be filled via sapp_identity_default.
 */
bool sapp_identity_load(sapp_identity_t* out);

/* Write the LOBBY_ID record. Validates magic/version before writing.
 * Returns false on bad header or BUP write failure. */
bool sapp_identity_save(const sapp_identity_t* in);

/* Zero the struct, set magic+version, generate a fresh random
 * session_uuid. Roster starts empty and current_name is "". */
void sapp_identity_default(sapp_identity_t* out);

/* FIFO add with dedupe. If name is already in names[] (case-sensitive
 * exact match) this is a no-op. Otherwise, when name_count < cap the
 * new name is appended; when full, names[1..7] shift down to [0..6]
 * and the new name lands at [7]. Always copies ≤10 chars + NUL. */
void sapp_identity_add_name(sapp_identity_t* id, const char* name);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_APP_IDENTITY_H */
