/*
 * libs/saturn-bup/host — filesystem-backed BUP storage.
 *
 * Stores each record as a single file under a base directory:
 *   $SBUP_HOME, or
 *   sbup_host_set_basedir(...) override, or
 *   ~/.lobby_bup/  (default)
 *
 * Filenames on disk are the trimmed (trailing-spaces-stripped) record
 * name with a `.bup` suffix. The directory is auto-created on init.
 *
 * Free-space accounting is synthetic: total = 32 KB (matches the
 * Saturn's internal cart), free = 32 KB minus the sum of currently
 * stored bytes. Block size is fixed at 64 bytes for the synthetic
 * free_blocks number — close enough to BUP's real layout that callers
 * can sanity-check without false assumptions.
 *
 * No malloc; the only dynamic allocation is the base-dir override
 * pointer, which the caller owns. Errors from the underlying syscalls
 * are mapped into the sbup_error_t enum.
 *
 * Compiles on macOS and Linux. Compiles under sh-elf-gcc as a no-op
 * (the file is excluded from the Saturn cross build by Makefile).
 */

#include <saturn_bup.h>
#include <saturn_bup/pal.h>
#include <saturn_bup/host.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Synthetic device geometry. Mirrors the internal Saturn cart so callers
 * see plausible numbers from sbup_stat. */
#define SBUP_HOST_TOTAL_SIZE  (32u * 1024u)
#define SBUP_HOST_BLOCK_SIZE  64u

/* On-disk file extension. Helps the user spot stray files in the dir. */
#define SBUP_HOST_SUFFIX ".bup"

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

static const char* g_basedir_override;     /* user-set; not owned */
static char        g_resolved_basedir[1024];
static int         g_basedir_resolved;

/* ---------------------------------------------------------------------------
 * Path resolution
 * ------------------------------------------------------------------------- */

static const char* host_basedir(void) {
    if (g_basedir_resolved) return g_resolved_basedir;

    const char* base = NULL;
    if (g_basedir_override && g_basedir_override[0] != '\0') {
        base = g_basedir_override;
    } else {
        const char* env = getenv("SBUP_HOME");
        if (env && env[0] != '\0') {
            base = env;
        }
    }

    if (base) {
        snprintf(g_resolved_basedir, sizeof(g_resolved_basedir), "%s", base);
    } else {
        const char* home = getenv("HOME");
        if (!home || home[0] == '\0') home = ".";
        snprintf(g_resolved_basedir, sizeof(g_resolved_basedir),
                 "%s/.lobby_bup", home);
    }
    g_basedir_resolved = 1;
    return g_resolved_basedir;
}

/* mkdir -p the base dir. Returns SBUP_OK or a mapped error. */
static sbup_error_t ensure_basedir(void) {
    const char* dir = host_basedir();
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? SBUP_OK : SBUP_BROKEN;
    }
    if (errno != ENOENT) return SBUP_NOT_CONNECTED;

    /* Walk the path one component at a time so we handle nested
     * overrides like /tmp/sbup-XXXXXX/sub gracefully. */
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", dir);
    for (char* p = buf + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0700) != 0 && errno != EEXIST) {
                return SBUP_NOT_CONNECTED;
            }
            *p = '/';
        }
    }
    if (mkdir(buf, 0700) != 0 && errno != EEXIST) return SBUP_NOT_CONNECTED;
    return SBUP_OK;
}

/* Strip trailing spaces from a padded BUP filename, append the suffix,
 * compose the on-disk path. dst must hold at least 1024 bytes. */
static void compose_path(char* dst, size_t cap, const char* padded_name) {
    char trimmed[SBUP_FILENAME_MAX + 1];
    size_t n = 0;
    for (size_t i = 0; i < SBUP_FILENAME_MAX && padded_name[i] != '\0'; i++) {
        trimmed[n++] = padded_name[i];
    }
    while (n > 0 && trimmed[n - 1] == ' ') n--;
    trimmed[n] = '\0';

    snprintf(dst, cap, "%s/%s%s", host_basedir(), trimmed, SBUP_HOST_SUFFIX);
}

/* ---------------------------------------------------------------------------
 * PAL ops
 * ------------------------------------------------------------------------- */

static sbup_error_t host_init(void* ctx, uint32_t device) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;
    return ensure_basedir();
}

static void host_shutdown(void* ctx) {
    (void)ctx;
    /* nothing to release */
}

static sbup_error_t host_read(void* ctx, uint32_t device,
                              const char* padded_name,
                              void* out, size_t cap, size_t* out_len) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;

    char path[1024];
    compose_path(path, sizeof(path), padded_name);

    FILE* f = fopen(path, "rb");
    if (!f) return SBUP_NOT_FOUND;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return SBUP_BROKEN; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return SBUP_BROKEN; }
    rewind(f);

    if ((size_t)sz > cap) {
        fclose(f);
        return SBUP_BROKEN;
    }

    size_t n = fread(out, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) return SBUP_BROKEN;

    if (out_len) *out_len = n;
    return SBUP_OK;
}

static sbup_error_t host_write(void* ctx, uint32_t device,
                               const char* padded_name,
                               const void* data, size_t len) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;

    /* Refuse if it would push us over the synthetic 32 KB cap. Match
     * BUP's real-cart behaviour so host-tested code surfaces the same
     * errors. */
    sbup_device_info_t info;
    if (host_init(NULL, 0) != SBUP_OK) return SBUP_NOT_CONNECTED;

    /* Reuse stat to learn current usage, but exclude the file we're
     * about to overwrite — otherwise an in-place overwrite would
     * double-count. */
    char path[1024];
    compose_path(path, sizeof(path), padded_name);

    struct stat existing;
    size_t existing_size = (stat(path, &existing) == 0) ? (size_t)existing.st_size : 0;

    /* Sum up everything else. */
    DIR* d = opendir(host_basedir());
    if (!d) return SBUP_NOT_CONNECTED;
    size_t used = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[1024];
        snprintf(p, sizeof(p), "%s/%s", host_basedir(), e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) used += (size_t)st.st_size;
    }
    closedir(d);

    /* Subtract the size of the record we're about to replace. */
    if (used >= existing_size) used -= existing_size;

    if (used + len > SBUP_HOST_TOTAL_SIZE) return SBUP_NO_SPACE;
    (void)info;

    FILE* f = fopen(path, "wb");
    if (!f) return SBUP_WRITE_PROTECTED;
    size_t n = fwrite(data, 1, len, f);
    int closed = fclose(f);
    if (n != len || closed != 0) return SBUP_BROKEN;
    return SBUP_OK;
}

static sbup_error_t host_erase(void* ctx, uint32_t device,
                               const char* padded_name) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;

    char path[1024];
    compose_path(path, sizeof(path), padded_name);

    if (unlink(path) != 0 && errno != ENOENT) return SBUP_BROKEN;
    return SBUP_OK;
}

static sbup_error_t host_stat(void* ctx, uint32_t device,
                              sbup_device_info_t* out) {
    (void)ctx;
    if (device != 0) return SBUP_NOT_CONNECTED;
    if (!out) return SBUP_INVALID;

    DIR* d = opendir(host_basedir());
    if (!d) return SBUP_NOT_CONNECTED;

    size_t used = 0;
    uint32_t count = 0;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[1024];
        snprintf(p, sizeof(p), "%s/%s", host_basedir(), e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
            used += (size_t)st.st_size;
            count++;
        }
    }
    closedir(d);

    out->total_size  = SBUP_HOST_TOTAL_SIZE;
    out->free_size   = (used >= SBUP_HOST_TOTAL_SIZE) ? 0u
                       : (uint32_t)(SBUP_HOST_TOTAL_SIZE - used);
    out->free_blocks = out->free_size / SBUP_HOST_BLOCK_SIZE;
    out->data_count  = count;
    return SBUP_OK;
}

/* ---------------------------------------------------------------------------
 * Public registration
 * ------------------------------------------------------------------------- */

static const sbup_pal_t s_host_pal = {
    .init     = host_init,
    .shutdown = host_shutdown,
    .read     = host_read,
    .write    = host_write,
    .erase    = host_erase,
    .stat     = host_stat,
    .ctx      = NULL,
};

void sbup_host_set_basedir(const char* path) {
    g_basedir_override = path;
    g_basedir_resolved = 0;       /* force re-resolve on next use */
}

void sbup_register_host_pal(void) {
    sbup_install_pal(&s_host_pal);
}
