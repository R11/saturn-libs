/*
 * libs/saturn-smpc — PAL (Platform Abstraction Layer).
 *
 * The lib's portable core/ calls into the PAL to actually read SMPC
 * peripherals and the RTC. The Saturn shell installs a real PAL backed
 * by the Smpc_Peripheral[N] global; tests install a mock PAL that
 * reports controlled state.
 *
 * One PAL per process. saturn_smpc_install_pal() replaces any prior PAL
 * (subsequent saturn_smpc_init() calls into the new one).
 */

#ifndef SATURN_SMPC_PAL_H
#define SATURN_SMPC_PAL_H

#include <stdint.h>

#include <saturn_base/result.h>
#include <saturn_smpc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct saturn_smpc_pal {
    /* Bring up the underlying peripheral subsystem. May allocate state
     * inside ctx. Return SATURN_OK on success. */
    saturn_result_t (*init)(void* ctx);

    /* Tear down. Symmetric with init. */
    void            (*shutdown)(void* ctx);

    /* Read the current peripheral state into out[]. Implementations write
     * up to SATURN_SMPC_MAX_PADS entries and store the count in *n_out.
     * Slots beyond *n_out are left untouched by the lib. */
    saturn_result_t (*read_pads)(void* ctx,
                                 saturn_smpc_pad_t out[SATURN_SMPC_MAX_PADS],
                                 uint8_t* n_out);

    /* Read the SMPC real-time clock into out. */
    saturn_result_t (*read_rtc)(void* ctx, saturn_smpc_rtc_t* out);

    /* Opaque context passed back to every callback. */
    void* ctx;
} saturn_smpc_pal_t;

/* Install a PAL. Must be called before saturn_smpc_init(). The lib stores
 * the pointer; the caller owns the lifetime. Passing NULL un-installs. */
void saturn_smpc_install_pal(const saturn_smpc_pal_t* pal);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SATURN_SMPC_PAL_H */
