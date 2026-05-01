/*
 * libs/saturn-bup — Saturn Backup RAM (BUP) storage.
 *
 * Public surface for reading and writing named records on the Saturn's
 * built-in 32 KB backup RAM. Two backends drive the same API:
 *
 *   * Saturn shell (saturn/) — calls into the BUP BIOS via the
 *     SH-2 vector table at 0x06000350. Initialised with a 16 KB
 *     workspace owned by the shell.
 *   * Host shell  (host/)   — files under ~/.lobby_bup/, one file
 *     per record name, raw bytes. Auto-creates the directory.
 *
 * Lifecycle:
 *   1. Game/host main installs a PAL via either
 *        sbup_register_host_pal()   (host build)
 *        sbup_register_saturn_pal() (Saturn build)
 *      before any sbup_* call.
 *   2. sbup_init(&h) brings up device 0 (internal cart).
 *   3. sbup_read / sbup_write / sbup_erase / sbup_stat operate on
 *      the handle.
 *
 * Filenames are passed by the caller as ≤11-char ASCII; the lib pads
 * to 11 internally to match the BUP directory format. Write mode is
 * always overwrite — an existing record with the same name is erased
 * first, then the new bytes are written.
 *
 * No platform headers leak through this header. Saturn-specific
 * BIOS macros live entirely inside saturn/sbup_saturn.c.
 */

#ifndef SATURN_BUP_H
#define SATURN_BUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <saturn_base/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum filename length on BUP (excluding the null terminator). */
#define SBUP_FILENAME_MAX 11u

/* Error codes mirror the BUP BIOS return values for the Saturn shell;
 * the host shell maps its errno failures into the same enum so callers
 * see one error space regardless of backend. */
typedef enum {
    SBUP_OK              = 0,
    SBUP_NOT_CONNECTED   = 1,
    SBUP_UNFORMATTED     = 2,
    SBUP_WRITE_PROTECTED = 3,
    SBUP_NO_SPACE        = 4,
    SBUP_NOT_FOUND       = 5,
    SBUP_ALREADY_EXISTS  = 6,
    SBUP_VERIFY_FAILED   = 7,
    SBUP_BROKEN          = 8,
    SBUP_NOT_READY       = 9,  /* lib used before init / no PAL */
    SBUP_INVALID         = 10  /* bad argument */
} sbup_error_t;

/* Device statistics, returned by sbup_stat(). The Saturn shell fills
 * these from BUP_Stat; the host shell fills total_size with a synthetic
 * 32 KB cap and free_size by subtracting the on-disk total. */
typedef struct {
    uint32_t total_size;   /* bytes total */
    uint32_t free_size;    /* bytes free */
    uint32_t free_blocks;  /* free block count (Saturn) or free_size / 64 (host) */
    uint32_t data_count;   /* number of records currently stored */
} sbup_device_info_t;

/* Opaque handle. Callers allocate it (typically on the stack); the lib
 * never malloc()s. The struct is exposed so it is statically sizable;
 * fields are not part of the contract — only sbup_* functions and
 * sbup_last_error() should touch them. */
typedef struct sbup_handle {
    bool         initialised;
    uint32_t     device;        /* always 0 for v1 (internal cart) */
    sbup_error_t last_error;
} sbup_handle_t;

/* Initialise the handle against device 0 (internal backup RAM cart).
 * Requires a PAL to have been installed beforehand (one of
 * sbup_register_host_pal / sbup_register_saturn_pal).
 *
 * Returns true on success. On failure, sbup_last_error(h) returns the
 * underlying error (typically SBUP_NOT_READY if no PAL, or
 * SBUP_NOT_CONNECTED if the device is missing).
 */
bool sbup_init (sbup_handle_t* h);

/* Read the named record into out. cap is the size of the destination
 * buffer; if the on-device record is larger, the call fails with
 * SBUP_BROKEN (caller should retry with a bigger buffer).
 *
 * On success, *out_len (if non-NULL) is set to the actual bytes read.
 */
bool sbup_read (sbup_handle_t* h, const char* name,
                void* out, size_t cap, size_t* out_len);

/* Write the named record. Mode is always overwrite: any existing record
 * with the same name is erased first. */
bool sbup_write(sbup_handle_t* h, const char* name,
                const void* data, size_t len);

/* Delete the named record. Succeeds (returns true) even if no such
 * record exists — callers commonly use this to ensure a clean slate. */
bool sbup_erase(sbup_handle_t* h, const char* name);

/* Fill out with current device stats. */
bool sbup_stat (sbup_handle_t* h, sbup_device_info_t* out);

/* Return a static human-readable string for the handle's last error.
 * The pointer is valid for the lifetime of the program. */
const char* sbup_last_error(const sbup_handle_t* h);

/* Map any sbup_error_t to its static string. Useful when the caller has
 * the error code without a handle (e.g. logging from PAL code). */
const char* sbup_error_string(sbup_error_t err);

/* Pad/truncate src into dst as an exactly-11-char space-padded
 * filename. dst must have room for 12 bytes (11 + NUL). Exposed for
 * tests; production code does not need to call this. */
void sbup_pad_filename(char* dst, const char* src);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURN_BUP_H */
