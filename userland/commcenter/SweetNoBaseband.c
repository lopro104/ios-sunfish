/*
 * SweetNoBaseband.dylib: CommCenter without the iPhone modem's kernel driver (ElleKit, filter: CommCenter).
 *
 * CommCenter takes the phone for an iPhone with a modem (the ADT's baseband-chipset 'mav17', MobileGestalt telephony)
 * and talks to the modem's packet-data driver through ApplePDPHelper.framework. Our kernel has no such driver, so
 * ApplePDPHelperInterface never gets a connection, and its first call locked a mutex at address 0 (sig 11 every 2 s,
 * logs/live-20261101.txt). Every operation of that class ends in one of four calls into the user client; each is
 * answered here with kIOReturnNotFound, what IOKit says when the service is not there, without touching the object.
 * Without CommCenter running, Settings' Privacy page drops Location Services (refreshLocationServicesAvailable
 * fails on com.apple.commcenter.coretelephony.xpc) and the pages that ask it for subscriptions hang.
 */
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <syslog.h>

#define kIOReturnNotFound 0xe00002f0

typedef void (*hook_fn)(void *symbol, void *replacement, void **original);

static int
not_found(void)
{
	return (int)kIOReturnNotFound;
}

static const char *calls[] = {
	"_ZN23ApplePDPHelperInterface16callStructMethodEjPKcPvjS2_Pj",
	"_ZN23ApplePDPHelperInterface16callScalarMethodEjPKcPyjS2_Pj",
	"_ZN23ApplePDPHelperInterface10callMethodEjPKcPyjPvmS2_PjS3_Pm",
	"_ZN23ApplePDPHelperInterface21callAsyncScalarMethodEjPKcPvS2_PyjS3_Pj",
};

__attribute__((constructor)) static void
sweet_no_baseband(void)
{
	hook_fn hook = (hook_fn)dlsym(RTLD_DEFAULT, "MSHookFunction");
	void *pdp = dlopen("/System/Library/PrivateFrameworks/ApplePDPHelper.framework/ApplePDPHelper", RTLD_NOW | RTLD_NOLOAD);

	if (pdp == NULL) {
		pdp = dlopen("/System/Library/PrivateFrameworks/ApplePDPHelper.framework/ApplePDPHelper", RTLD_NOW);
	}
	if (hook == NULL || pdp == NULL) {
		syslog(LOG_ERR, "SweetNoBaseband: no MSHookFunction (%p) or ApplePDPHelper (%p)", (void *)hook, pdp);
		return;
	}
	for (unsigned i = 0; i < sizeof(calls) / sizeof(calls[0]); i++) {
		void *f = dlsym(pdp, calls[i]);
		if (f != NULL) {
			void *original = NULL;
			hook(f, (void *)not_found, &original);
		}
		syslog(LOG_NOTICE, "SweetNoBaseband: %s %s", calls[i], f != NULL ? "answers kIOReturnNotFound" : "NOT FOUND");
	}
}
