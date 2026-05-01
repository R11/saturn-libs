/*
 * libs/saturn-app/core/identity.c — implementation of identity store.
 *
 * Singleton sbup handle is brought up lazily on the first load() call.
 * The PAL must already be installed by the platform shell (host or
 * Saturn) before any sapp_identity_* call.
 */

#include <saturn_app/identity.h>
#include <saturn_bup.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !defined(SAPP_NO_HOST_RAND)
#  include <sys/time.h>
#  include <unistd.h>
#endif

/* Process-singleton handle + init flag. Threading is not a concern on
 * Saturn (single-threaded game loop) and host tests are sequential. */
static sbup_handle_t g_bup;
static bool          g_bup_inited = false;
static bool          g_seeded     = false;

static bool ensure_bup(void) {
    if (g_bup_inited) return true;
    if (!sbup_init(&g_bup)) return false;
    g_bup_inited = true;
    return true;
}

/* Tests need to start with a clean handle when they tear down the PAL
 * between cases. Not part of the public surface, but exposed via a
 * weak-ish convention: tests reset via this internal hook. We expose
 * it through a private declaration in the test file rather than a
 * header to keep the public API minimal. */
void sapp_identity__reset_for_tests(void);
void sapp_identity__reset_for_tests(void) {
    memset(&g_bup, 0, sizeof(g_bup));
    g_bup_inited = false;
}

static void seed_rand_once(void) {
    if (g_seeded) return;
    /* Mix wall-clock seconds with sub-second jitter and pid so two
     * lobby_host processes started in the same second on the same machine
     * (a common test pattern) get distinct UUIDs. On Saturn the time()
     * stub is good enough since each cart cold-boots in isolation. */
    unsigned seed = (unsigned)time(NULL);
#if !defined(SAPP_NO_HOST_RAND)
    {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) == 0) {
            seed ^= (unsigned)tv.tv_usec;
        }
        seed ^= (unsigned)getpid() * 2654435761u;
    }
#endif
    srand(seed);
    g_seeded = true;
}

bool sapp_identity_load(sapp_identity_t* out) {
    if (!out) return false;
    if (!ensure_bup()) return false;

    sapp_identity_t tmp;
    size_t n = 0;
    if (!sbup_read(&g_bup, SAPP_IDENTITY_RECORD,
                   &tmp, sizeof(tmp), &n)) {
        return false;
    }
    if (n != sizeof(sapp_identity_t))   return false;
    if (tmp.magic   != SAPP_IDENTITY_MAGIC)   return false;
    if (tmp.version != SAPP_IDENTITY_VERSION) return false;

    *out = tmp;
    return true;
}

bool sapp_identity_save(const sapp_identity_t* in) {
    if (!in) return false;
    if (in->magic   != SAPP_IDENTITY_MAGIC)   return false;
    if (in->version != SAPP_IDENTITY_VERSION) return false;
    if (!ensure_bup()) return false;
    return sbup_write(&g_bup, SAPP_IDENTITY_RECORD,
                      in, sizeof(*in));
}

void sapp_identity_default(sapp_identity_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->magic    = SAPP_IDENTITY_MAGIC;
    out->version  = SAPP_IDENTITY_VERSION;
    out->reserved = 0;

    seed_rand_once();
    /* RAND_MAX is only guaranteed ≥32767, so fill byte-by-byte. */
    for (size_t i = 0; i < sizeof(out->session_uuid); ++i) {
        out->session_uuid[i] = (uint8_t)(rand() & 0xFF);
    }
}

static void copy_name(char* dst, const char* src) {
    /* Copy up to SAPP_NAME_CAP-1 chars, always NUL-terminate. */
    size_t i = 0;
    if (src) {
        for (; i < SAPP_NAME_CAP - 1 && src[i] != '\0'; ++i) {
            dst[i] = src[i];
        }
    }
    for (; i < SAPP_NAME_CAP; ++i) {
        dst[i] = '\0';
    }
}

void sapp_identity_add_name(sapp_identity_t* id, const char* name) {
    if (!id || !name || name[0] == '\0') return;

    /* Build the truncated form once so dedupe and store are consistent. */
    char clean[SAPP_NAME_CAP];
    copy_name(clean, name);
    if (clean[0] == '\0') return;

    /* Dedupe: case-sensitive exact match. */
    for (uint8_t i = 0; i < id->name_count && i < SAPP_NAME_MAX; ++i) {
        if (strncmp(id->names[i], clean, SAPP_NAME_CAP) == 0) {
            return;
        }
    }

    if (id->name_count < SAPP_NAME_MAX) {
        copy_name(id->names[id->name_count], clean);
        id->name_count++;
        return;
    }

    /* Full: shift names[1..7] -> [0..6], drop oldest, append new at [7]. */
    for (uint8_t i = 1; i < SAPP_NAME_MAX; ++i) {
        memcpy(id->names[i - 1], id->names[i], SAPP_NAME_CAP);
    }
    copy_name(id->names[SAPP_NAME_MAX - 1], clean);
}
