/*
 * libs/saturn-bup/core — portable BUP storage logic.
 *
 * Owns the handle, dispatches every operation through the installed
 * PAL, and provides shared utilities (filename padding, error->string
 * mapping). No platform headers, no malloc, no globals beyond the PAL
 * pointer; the caller owns the handle's storage.
 *
 * Host-testable; the same .c also compiles under sh-elf-gcc and emcc
 * unchanged.
 */

#include <saturn_bup.h>
#include <saturn_bup/pal.h>

#include <string.h>

/* ---------------------------------------------------------------------------
 * PAL install
 * ------------------------------------------------------------------------- */

static const sbup_pal_t* g_pal;

void sbup_install_pal(const sbup_pal_t* pal) {
    g_pal = pal;
}

const sbup_pal_t* sbup_get_pal(void) {
    return g_pal;
}

/* ---------------------------------------------------------------------------
 * Filename padding (BUP convention: exactly 11 chars, space-padded,
 * NUL-terminated 12-byte buffer).
 * ------------------------------------------------------------------------- */

void sbup_pad_filename(char* dst, const char* src) {
    if (!dst) return;

    size_t i = 0;
    if (src) {
        for (; i < SBUP_FILENAME_MAX && src[i] != '\0'; i++) {
            dst[i] = src[i];
        }
    }
    for (; i < SBUP_FILENAME_MAX; i++) {
        dst[i] = ' ';
    }
    dst[SBUP_FILENAME_MAX] = '\0';
}

/* ---------------------------------------------------------------------------
 * Error -> string
 * ------------------------------------------------------------------------- */

const char* sbup_error_string(sbup_error_t err) {
    switch (err) {
        case SBUP_OK:              return "OK";
        case SBUP_NOT_CONNECTED:   return "Device not connected";
        case SBUP_UNFORMATTED:     return "Device not formatted";
        case SBUP_WRITE_PROTECTED: return "Write protected";
        case SBUP_NO_SPACE:        return "No space";
        case SBUP_NOT_FOUND:       return "File not found";
        case SBUP_ALREADY_EXISTS:  return "File already exists";
        case SBUP_VERIFY_FAILED:   return "Verify failed";
        case SBUP_BROKEN:          return "Data broken / buffer too small";
        case SBUP_NOT_READY:       return "Lib not ready (no PAL or not initialised)";
        case SBUP_INVALID:         return "Invalid argument";
        default:                   return "Unknown error";
    }
}

const char* sbup_last_error(const sbup_handle_t* h) {
    if (!h) return sbup_error_string(SBUP_INVALID);
    return sbup_error_string(h->last_error);
}

/* ---------------------------------------------------------------------------
 * Handle helpers
 * ------------------------------------------------------------------------- */

static bool fail(sbup_handle_t* h, sbup_error_t err) {
    if (h) h->last_error = err;
    return false;
}

static bool ok(sbup_handle_t* h) {
    if (h) h->last_error = SBUP_OK;
    return true;
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

bool sbup_init(sbup_handle_t* h) {
    if (!h) return false;

    memset(h, 0, sizeof(*h));
    h->device = 0;

    if (!g_pal || !g_pal->init) return fail(h, SBUP_NOT_READY);

    sbup_error_t rc = g_pal->init(g_pal->ctx, h->device);
    if (rc != SBUP_OK) return fail(h, rc);

    h->initialised = true;
    return ok(h);
}

/* ---------------------------------------------------------------------------
 * Read / Write / Erase / Stat
 * ------------------------------------------------------------------------- */

bool sbup_read(sbup_handle_t* h, const char* name,
               void* out, size_t cap, size_t* out_len) {
    if (!h) return false;
    if (!h->initialised) return fail(h, SBUP_NOT_READY);
    if (!name || !out || cap == 0) return fail(h, SBUP_INVALID);
    if (!g_pal || !g_pal->read) return fail(h, SBUP_NOT_READY);

    char padded[SBUP_FILENAME_MAX + 1];
    sbup_pad_filename(padded, name);

    sbup_error_t rc = g_pal->read(g_pal->ctx, h->device,
                                  padded, out, cap, out_len);
    if (rc != SBUP_OK) return fail(h, rc);
    return ok(h);
}

bool sbup_write(sbup_handle_t* h, const char* name,
                const void* data, size_t len) {
    if (!h) return false;
    if (!h->initialised) return fail(h, SBUP_NOT_READY);
    if (!name || !data || len == 0) return fail(h, SBUP_INVALID);
    if (!g_pal || !g_pal->write) return fail(h, SBUP_NOT_READY);

    char padded[SBUP_FILENAME_MAX + 1];
    sbup_pad_filename(padded, name);

    sbup_error_t rc = g_pal->write(g_pal->ctx, h->device,
                                   padded, data, len);
    if (rc != SBUP_OK) return fail(h, rc);
    return ok(h);
}

bool sbup_erase(sbup_handle_t* h, const char* name) {
    if (!h) return false;
    if (!h->initialised) return fail(h, SBUP_NOT_READY);
    if (!name) return fail(h, SBUP_INVALID);
    if (!g_pal || !g_pal->erase) return fail(h, SBUP_NOT_READY);

    char padded[SBUP_FILENAME_MAX + 1];
    sbup_pad_filename(padded, name);

    sbup_error_t rc = g_pal->erase(g_pal->ctx, h->device, padded);
    if (rc != SBUP_OK) return fail(h, rc);
    return ok(h);
}

bool sbup_stat(sbup_handle_t* h, sbup_device_info_t* out) {
    if (!h) return false;
    if (!h->initialised) return fail(h, SBUP_NOT_READY);
    if (!out) return fail(h, SBUP_INVALID);
    if (!g_pal || !g_pal->stat) return fail(h, SBUP_NOT_READY);

    sbup_error_t rc = g_pal->stat(g_pal->ctx, h->device, out);
    if (rc != SBUP_OK) return fail(h, rc);
    return ok(h);
}
