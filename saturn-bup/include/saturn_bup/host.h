/*
 * libs/saturn-bup/host.h — host-build-only entry point.
 *
 * Apps building for the host (CLI, tests, browser harness) call
 * sbup_register_host_pal() once before sbup_init(). The Saturn build
 * of the lib does not link the host/ shell, so consumers cross-compiling
 * for sh-elf should not include this header.
 *
 * The host PAL stores each record as a single file under a directory
 * derived from $SBUP_HOME (default: ~/.lobby_bup/). The directory is
 * created on the first init() if it does not already exist.
 */

#ifndef SATURN_BUP_HOST_H
#define SATURN_BUP_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install the filesystem-backed PAL. After this call, sbup_init()
 * resolves to ~/.lobby_bup/ (or $SBUP_HOME if set). */
void sbup_register_host_pal(void);

/* Set an explicit base directory before registering. Pass NULL to
 * revert to the default (~/.lobby_bup or $SBUP_HOME). The pointer is
 * not copied — the caller must keep the string alive for the lifetime
 * of the PAL. Used by tests to point at a tmpdir. */
void sbup_host_set_basedir(const char* path);

#ifdef __cplusplus
}
#endif

#endif /* SATURN_BUP_HOST_H */
