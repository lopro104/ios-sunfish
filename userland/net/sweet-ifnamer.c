/*
 * sweet-ifnamer: publishes Plugin:InterfaceNamer = { "*QUIET*" = true } in the SCDynamicStore.
 * SCNetworkInterfaceCopyAll* (SystemConfiguration, used by wifid, MobileWiFi's server side and others) first waits for
 * "IOKit to quiesce": it blocks until configd's InterfaceNamer plugin publishes that key with *QUIET*, *COMPLETE* or
 * *TIMEOUT*. Nothing on this system runs InterfaceNamer, so wifid's manager thread blocked there for ever and SpringBoard,
 * waiting for wifid in WiFiManagerClientCreate, never drew its first frame (stuck on the Apple logo).
 */
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>

/* the iOS SDK marks the SCDynamicStore API unavailable; sweet-wlan.c declares what it uses the same way */
typedef const struct __SCDynamicStore *SCDynamicStoreRef;
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, void *, void *);
extern Boolean SCDynamicStoreSetMultiple(SCDynamicStoreRef, CFDictionaryRef set, CFArrayRef remove, CFArrayRef notify);

int
main(void)
{
	SCDynamicStoreRef store = SCDynamicStoreCreate(NULL, CFSTR("sweet-ifnamer"), NULL, NULL);
	CFStringRef key = CFSTR("Plugin:InterfaceNamer");
	const void *k[1] = { CFSTR("*QUIET*") };
	const void *v[1] = { kCFBooleanTrue };
	CFDictionaryRef d, set;
	Boolean ok;

	if (store == NULL) {
		fprintf(stderr, "sweet-ifnamer: no SCDynamicStore\n");
		return 1;
	}
	d = CFDictionaryCreate(NULL, k, v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	{
		const void *sk[1] = { key };
		const void *sv[1] = { d };

		set = CFDictionaryCreate(NULL, sk, sv, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	}
	ok = SCDynamicStoreSetMultiple(store, set, NULL, NULL);
	printf("sweet-ifnamer: Plugin:InterfaceNamer %s\n", ok ? "published" : "FAILED");
	CFRelease(set);
	CFRelease(d);
	CFRelease(store);
	return ok ? 0 : 1;
}
