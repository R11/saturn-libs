/*
 * libs/saturn-bup/saturn.h — Saturn-target-only entry point.
 *
 * Saturn games call sbup_register_saturn_pal() once during boot
 * (after VDP/SGL setup and before sbup_init()). This shell calls
 * BUP_Init with a 16 KB workspace owned by the lib, then routes the
 * five operations through the BIOS function pointers at
 * 0x06000350.
 *
 * The host build of the lib does not link the saturn/ shell, so
 * consumers compiling for host should not include this header.
 */

#ifndef SATURN_BUP_SATURN_H
#define SATURN_BUP_SATURN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install the BUP-BIOS-backed PAL. After this call, sbup_init() will
 * call BUP_Init and verify device 0 (internal cart) is present. If the
 * cart is unformatted, sbup_init() also issues BUP_Format(0). */
void sbup_register_saturn_pal(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BUP_SATURN_H */
