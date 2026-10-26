/*
 * sweet-net: the USB network (ue0, tools/sweet-usbnet.sh: phone 192.168.7.2, PC 192.168.7.1 with NAT)
 * as a network service in configd's dynamic store, the way IPConfiguration would publish one it got
 * by DHCP: State:/Network/Service/sweet-usb/IPv4 (address, router) and .../DNS (servers). configd's
 * IPMonitor then makes it the primary service: the default route and DNS for every program.
 * configd never does it itself: ue0 is no IONetworkInterface, so it has no network service for it.
 *
 *   sweet-net [DNS ...]     (default DNS 1.1.1.1 8.8.8.8)
 *
 * The SCDynamicStore calls are not in the iPhoneOS SDK's headers (API_UNAVAILABLE(ios)), but
 * SystemConfiguration.framework has them; they are declared here.
 */
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>

typedef const struct __SCDynamicStore *SCDynamicStoreRef;
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, void *, void *);
extern Boolean SCDynamicStoreSetMultiple(SCDynamicStoreRef, CFDictionaryRef set, CFArrayRef remove, CFArrayRef notify);
extern int SCError(void);
extern const char *SCErrorString(int);

#define SERVICE "sweet-usb"

static CFArrayRef
strings(int n, const char *const *v)
{
	CFMutableArrayRef a = CFArrayCreateMutable(NULL, n, &kCFTypeArrayCallBacks);
	for (int i = 0; i < n; i++) {
		CFStringRef s = CFStringCreateWithCString(NULL, v[i], kCFStringEncodingUTF8);
		CFArrayAppendValue(a, s);
		CFRelease(s);
	}
	return a;
}

static CFDictionaryRef
dict(int n, const void **keys, const void **values)
{
	return CFDictionaryCreate(NULL, keys, values, n, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

int
main(int argc, char *argv[])
{
	static const char *const address[] = { "192.168.7.2" }, *const mask[] = { "255.255.255.0" };
	static const char *const dns_default[] = { "1.1.1.1", "8.8.8.8" };
	const char *const *dns = argc > 1 ? (const char *const *)argv + 1 : dns_default;
	int ndns = argc > 1 ? argc - 1 : 2;

	SCDynamicStoreRef store = SCDynamicStoreCreate(NULL, CFSTR("sweet-net"), NULL, NULL);
	if (store == NULL) {
		fprintf(stderr, "sweet-net: no dynamic store: %s\n", SCErrorString(SCError()));
		return 1;
	}
	CFArrayRef addresses = strings(1, address), masks = strings(1, mask), servers = strings(ndns, dns);
	const void *ipv4_keys[] = { CFSTR("Addresses"), CFSTR("SubnetMasks"), CFSTR("Router"), CFSTR("InterfaceName"),
		                    CFSTR("ConfigMethod") };
	const void *ipv4_values[] = { addresses, masks, CFSTR("192.168.7.1"), CFSTR("ue0"), CFSTR("Manual") };
	CFDictionaryRef ipv4 = dict(5, ipv4_keys, ipv4_values);
	const void *dns_keys[] = { CFSTR("ServerAddresses") };
	const void *dns_values[] = { servers };
	CFDictionaryRef dnsd = dict(1, dns_keys, dns_values);
	const void *if_keys[] = { CFSTR("DeviceName"), CFSTR("Type"), CFSTR("Hardware"), CFSTR("UserDefinedName") };
	const void *if_values[] = { CFSTR("ue0"), CFSTR("Ethernet"), CFSTR("Ethernet"), CFSTR("USB (sweet)") };
	CFDictionaryRef ifd = dict(4, if_keys, if_values);

	const void *keys[] = {
		CFSTR("State:/Network/Service/" SERVICE "/IPv4"), CFSTR("State:/Network/Service/" SERVICE "/DNS"),
		CFSTR("Setup:/Network/Service/" SERVICE "/IPv4"), CFSTR("Setup:/Network/Service/" SERVICE "/Interface"),
	};
	const void *values[] = { ipv4, dnsd, ipv4, ifd };
	CFDictionaryRef set = dict(4, keys, values);
	Boolean ok = SCDynamicStoreSetMultiple(store, set, NULL, NULL);
	printf("sweet-net: service %s (ue0 192.168.7.2, router 192.168.7.1, %d DNS server%s from %s): %s\n", SERVICE, ndns,
	    ndns == 1 ? "" : "s", dns[0], ok ? "set" : SCErrorString(SCError()));
	return ok ? 0 : 1;
}
