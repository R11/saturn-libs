/*
 * libs/saturn-smpc/saturn.h — Saturn-target-only entry point.
 *
 * Apps building for the Saturn target call saturn_smpc_register_saturn_pal()
 * once before saturn_smpc_init(). The host build of the lib does not link
 * the saturn/ shell, so consumers compiling for host should not include
 * this header.
 */

#ifndef SATURN_SMPC_SATURN_H
#define SATURN_SMPC_SATURN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install the Saturn-hardware PAL. After this call, saturn_smpc_init()
 * will read real Smpc_Peripheral[] data and the SMPC RTC. */
void saturn_smpc_register_saturn_pal(void);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_SMPC_SATURN_H */
