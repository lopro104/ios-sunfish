/*
 * SweetLocationd.dylib: locationd without its deadlock on itself (ElleKit, filter: locationd).
 *
 * In locationd the CLEmergencyControllerAdapterSilo thread asks CoreLocation for an authorization status
 * (CLInternalGetAuthorizationStatus, a synchronous NSXPC request to locationd's own service), which locationd answers
 * on its main thread; the main thread meanwhile waits in dispatch_sync for that silo's queue. Neither returns, and
 * from then on locationd answers nobody: Settings' Date & Time hung in -[PSGDateTimeController setAutomaticTimeFooter]
 * on the same request (stackshot in the 0x8BADF00D report Preferences-2026-11-01-074402.ips).
 *
 * The first such call was +[CLLocationManager _authorizationStatusForBundleIdentifier:bundlePath:] (CoreLocation, which
 * the public +authorizationStatus... methods wrap). In locationd it can only be locationd asking itself, so here that
 * method answers 2, kCLAuthorizationStatusDenied, without the round trip. It is replaced through the Objective-C
 * runtime (data, not code: no page of locationd is modified). The next deadlock was CLSeparationAlertsServiceSilo in
 * -[CLLocationInternalClient copyMonitoredRegionsForBundleIdentifier:orBundlePath:] (report 075528): every method of
 * CLLocationInternalClient is a synchronous request to locationd's internal service, so in locationd each one is
 * answered 0 here (NO, NULL, nothing), except its own plumbing and methods that do not return in x0 (a double).
 * Then CLHarvestControllerSilo (report 075916): -[CLHarvesterService beginService] -> -[CLMescalSigner initInSilo:]
 * waits in mach_msg for FairPlay (fairplayd.H2 relaunches every few seconds and never answers here), and the main
 * thread and CLInternalServiceSilo wait for that silo. The signer only signs harvested location data for Apple, so
 * here it is never made: initInSilo: answers nil. The constructor does nothing else: CoreFoundation and
 * Foundation are not ready this early (a CF/Security call from a tweak constructor ended in an endless
 * +[NSString stringWithFormat:] recursion in CommCenter).
 */
#include <dlfcn.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <objc/runtime.h>

static int
denied(__unused id self, __unused SEL cmd, __unused id identifier, __unused id path)
{
	return 2;       /* kCLAuthorizationStatusDenied */
}

static long
nothing(__unused id self, __unused SEL cmd)
{
	return 0;
}

/* a return type answered in x0: integers, BOOL, pointers, objects, void (sizes up to 8) */
static int
in_x0(const char *types)
{
	while (*types == 'r' || *types == 'n' || *types == 'N' || *types == 'o' || *types == 'O' || *types == 'R' || *types == 'V') {
		types++;
	}
	return strchr("cCsSiIlLqQB@^*#:v", *types) != NULL;
}

static id
no_signer(__unused id self, __unused SEL cmd, __unused id silo)
{
	return nil;
}

static const char *plumbing[] = { "connection", "synchronousRemoteObject", "init", "dealloc", ".cxx_destruct", NULL };

__attribute__((constructor)) static void
sweet_locationd(void)
{
	dlopen("/System/Library/Frameworks/CoreLocation.framework/CoreLocation", RTLD_NOW);
	Class manager = objc_getClass("CLLocationManager");
	Method m = manager != NULL ?
	    class_getClassMethod(manager, sel_registerName("_authorizationStatusForBundleIdentifier:bundlePath:")) : NULL;

	if (m != NULL) {
		method_setImplementation(m, (IMP)denied);
	}

	Class signer = objc_getClass("CLMescalSigner");
	Method init = signer != NULL ? class_getInstanceMethod(signer, sel_registerName("initInSilo:")) : NULL;
	if (init != NULL) {
		method_setImplementation(init, (IMP)no_signer);
	}

	Class client = objc_getClass("CLLocationInternalClient");
	unsigned int count = 0;
	Method *methods = client != NULL ? class_copyMethodList(client, &count) : NULL;
	for (unsigned int i = 0; i < count; i++) {
		const char *name = sel_getName(method_getName(methods[i]));
		int skip = !in_x0(method_getTypeEncoding(methods[i]));
		for (int k = 0; !skip && plumbing[k] != NULL; k++) {
			skip = strcmp(name, plumbing[k]) == 0 || strncmp(name, "initWith", 8) == 0;
		}
		if (!skip) {
			method_setImplementation(methods[i], (IMP)nothing);
		}
	}
	free(methods);
}
