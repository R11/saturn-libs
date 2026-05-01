/*
 * libs/saturn-app/net — public hooks for installing a network backend
 * from a platform shell or test harness.
 *
 * The full backend vtable lives in net/sapp_net.h, which is internal to
 * the library. Shells and tests interact through the install helpers
 * declared here.
 */

#ifndef SATURN_APP_NET_PUBLIC_H
#define SATURN_APP_NET_PUBLIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install the POSIX TCP backend pointing at "host:port" (port defaults
 * to 7780 if omitted). Returns 0 on success, -1 on parse error.
 *
 * After calling this, sapp_init has nothing to do — the framework will
 * lazily pull on the backend the first time it enters CONNECTING. */
int  sapp_net_host_install(const char* endpoint);

/* Tear down whatever backend is currently installed. */
void sapp_net_uninstall(void);

#ifdef __cplusplus
}
#endif
#endif
