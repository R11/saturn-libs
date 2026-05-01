/*
 * libs/saturn-bup — PAL (Platform Abstraction Layer).
 *
 * The portable core/ dispatches every operation through the installed
 * PAL. The Saturn shell installs a PAL backed by the BUP BIOS; the
 * host shell installs one backed by files under ~/.lobby_bup/. Tests
 * may install their own.
 *
 * One PAL per process. sbup_install_pal() replaces any prior PAL.
 * Filenames passed into PAL ops are already padded to exactly 11
 * characters (NUL-terminated 12-byte buffer); the PAL must NOT
 * re-pad.
 */

#ifndef SATURN_BUP_PAL_H
#define SATURN_BUP_PAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <saturn_bup.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sbup_pal {
    /* Bring up the backend for the given device (always 0 in v1).
     * Returns SBUP_OK on success. May allocate state inside ctx. */
    sbup_error_t (*init)(void* ctx, uint32_t device);

    /* Tear down. Symmetric with init. May be NULL for stateless backends. */
    void         (*shutdown)(void* ctx);

    /* Read the record into out (cap bytes max). *out_len (if non-NULL)
     * is set to the actual bytes read. Returns SBUP_NOT_FOUND if
     * the record is missing, SBUP_BROKEN if the record exceeds cap. */
    sbup_error_t (*read)(void* ctx, uint32_t device,
                         const char* padded_name,
                         void* out, size_t cap, size_t* out_len);

    /* Write the record (overwriting any existing same-name record). */
    sbup_error_t (*write)(void* ctx, uint32_t device,
                          const char* padded_name,
                          const void* data, size_t len);

    /* Erase the record. Returns SBUP_OK even if no such record existed. */
    sbup_error_t (*erase)(void* ctx, uint32_t device,
                          const char* padded_name);

    /* Fill the device-info struct. */
    sbup_error_t (*stat)(void* ctx, uint32_t device,
                         sbup_device_info_t* out);

    /* Opaque context passed to every callback. */
    void* ctx;
} sbup_pal_t;

/* Install a PAL. Must be called before sbup_init(). The lib stores the
 * pointer; the caller owns the lifetime. Passing NULL un-installs. */
void sbup_install_pal(const sbup_pal_t* pal);

/* Read back the currently-installed PAL (NULL if none). Useful for
 * tests that want to chain calls or assert installation. */
const sbup_pal_t* sbup_get_pal(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURN_BUP_PAL_H */
