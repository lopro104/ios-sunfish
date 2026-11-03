/*
 * sweet-wlan: publishes the Wi-Fi interface (ew0) into SCDynamicStore
 * so iOS configd/IPMonitor recognizes it as an active network service:
 *   State:/Network/Service/sweet-wlan/IPv4
 *   State:/Network/Service/sweet-wlan/DNS
 *   Setup:/Network/Service/sweet-wlan/IPv4
 *   Setup:/Network/Service/sweet-wlan/Interface
 *
 * Reads IP, mask, router, DNS, SSID from `sysctl kern.sweet_wlan_info`
 * or from command-line arguments:
 *   sweet-wlan [-m|--monitor] [IP MASK ROUTER DNS]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <CoreFoundation/CoreFoundation.h>

typedef const struct __SCDynamicStore *SCDynamicStoreRef;
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, void *, void *);
extern Boolean SCDynamicStoreSetMultiple(SCDynamicStoreRef, CFDictionaryRef set, CFArrayRef remove, CFArrayRef notify);
extern Boolean SCDynamicStoreRemoveValue(SCDynamicStoreRef, CFStringRef key);
extern int SCError(void);
extern const char *SCErrorString(int);

#define SERVICE "4CD8DB21-0AC8-40A5-83C6-094D249CFE68"
#define IFNAME "en0"

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

static int
publish_service(SCDynamicStoreRef store, const char *ip, const char *mask, const char *router, const char *dns)
{
	unsigned int ifidx = if_nametoindex(IFNAME);
	const char *const addr_arr[] = { ip };
	const char *const mask_arr[] = { mask };
	const char *const dns_arr[] = { dns, "1.1.1.1", "8.8.8.8" };

	CFArrayRef addresses = strings(1, addr_arr);
	CFArrayRef masks = strings(1, mask_arr);
	CFArrayRef servers = strings(3, dns_arr);
	CFStringRef router_str = CFStringCreateWithCString(NULL, router, kCFStringEncodingUTF8);

	const void *ipv4_keys[] = {
		CFSTR("Addresses"), CFSTR("SubnetMasks"), CFSTR("Router"),
		CFSTR("InterfaceName"), CFSTR("ConfigMethod")
	};
	const void *ipv4_values[] = {
		addresses, masks, router_str, CFSTR(IFNAME), CFSTR("DHCP")
	};
	CFDictionaryRef ipv4 = dict(5, ipv4_keys, ipv4_values);

	const void *dns_keys[] = { CFSTR("ServerAddresses") };
	const void *dns_values[] = { servers };
	CFDictionaryRef dnsd = dict(1, dns_keys, dns_values);

	const void *if_keys[] = {
		CFSTR("DeviceName"), CFSTR("Type"), CFSTR("Hardware"), CFSTR("UserDefinedName")
	};
	const void *if_values[] = {
		CFSTR(IFNAME), CFSTR("Ethernet"), CFSTR("AirPort"), CFSTR("Wi-Fi")
	};
	CFDictionaryRef ifd = dict(4, if_keys, if_values);

	const void *glob_keys[] = {
		CFSTR("PrimaryInterface"), CFSTR("PrimaryService"), CFSTR("Router")
	};
	const void *glob_values[] = {
		CFSTR(IFNAME), CFSTR(SERVICE), router_str
	};
	CFDictionaryRef glob = dict(3, glob_keys, glob_values);

	/* Global DNS dictionary: mDNSResponder relies on this to direct queries over en0 */
	int six = 6, zero = 0, idx = (int)ifidx;
	CFNumberRef num_flags = CFNumberCreate(NULL, kCFNumberIntType, &six);
	CFNumberRef num_order = CFNumberCreate(NULL, kCFNumberIntType, &zero);
	CFNumberRef num_idx = CFNumberCreate(NULL, kCFNumberIntType, &idx);

	const void *gdns_keys[] = {
		CFSTR("ServerAddresses"),
		CFSTR("__FLAGS__"),
		CFSTR("__ORDER__"),
		CFSTR("__IF_INDEX__"),
		CFSTR("__CONFIGURATION_ID__"),
	};
	const void *gdns_values[] = {
		servers,
		num_flags,
		num_order,
		num_idx,
		CFSTR("Default: 0"),
	};
	CFDictionaryRef gdns = dict(5, gdns_keys, gdns_values);

	/* Set PrimaryRank = First for Wi-Fi service to ensure IPMonitor selects it */
	const void *rank_keys[] = { CFSTR("PrimaryRank") };
	const void *rank_values[] = { CFSTR("First") };
	CFDictionaryRef rank_dict = dict(1, rank_keys, rank_values);

	const void *keys[] = {
		CFSTR("State:/Network/Service/" SERVICE "/IPv4"),
		CFSTR("State:/Network/Service/" SERVICE "/DNS"),
		CFSTR("State:/Network/Interface/" IFNAME "/IPv4"),
		CFSTR("State:/Network/Interface/" IFNAME "/DNS"),
		CFSTR("State:/Network/Global/IPv4"),
		CFSTR("State:/Network/Global/DNS"),
		CFSTR("State:/Network/Service/" SERVICE),
		CFSTR("Setup:/Network/Service/" SERVICE "/IPv4"),
		CFSTR("Setup:/Network/Service/" SERVICE "/Interface"),
	};
	const void *values[] = { ipv4, dnsd, ipv4, dnsd, glob, gdns, rank_dict, ipv4, ifd };
	CFDictionaryRef set = dict(9, keys, values);

	Boolean ok = SCDynamicStoreSetMultiple(store, set, NULL, NULL);

	/* Clear IPConfigurationBusy so callers don't wait */
	SCDynamicStoreRemoveValue(store, CFSTR("State:/Network/Interface/" IFNAME "/IPConfigurationBusy"));
	/* Clear CaptiveNetwork evaluation trap so configd does not rank Wi-Fi Last */
	SCDynamicStoreRemoveValue(store, CFSTR("State:/Network/Interface/" IFNAME "/CaptiveNetwork"));

	CFRelease(set);
	CFRelease(rank_dict);
	CFRelease(gdns);
	CFRelease(num_idx);
	CFRelease(num_order);
	CFRelease(num_flags);
	CFRelease(glob);
	CFRelease(ifd);
	CFRelease(dnsd);
	CFRelease(ipv4);
	CFRelease(router_str);
	CFRelease(servers);
	CFRelease(masks);
	CFRelease(addresses);

	if (!ok) {
		fprintf(stderr, "sweet-wlan: SCDynamicStoreSetMultiple failed: %s\n", SCErrorString(SCError()));
		return -1;
	}
	printf("sweet-wlan: published service %s (" IFNAME " %s, router %s, DNS %s, 1.1.1.1, 8.8.8.8)\n", SERVICE, ip, router, dns);
	return 0;
}

static void
unpublish_service(SCDynamicStoreRef store)
{
	const void *keys[] = {
		CFSTR("State:/Network/Service/" SERVICE "/IPv4"),
		CFSTR("State:/Network/Service/" SERVICE "/DNS"),
		CFSTR("State:/Network/Interface/" IFNAME "/IPv4"),
		CFSTR("State:/Network/Interface/" IFNAME "/DNS"),
		CFSTR("State:/Network/Global/IPv4"),
		CFSTR("State:/Network/Global/DNS"),
		CFSTR("State:/Network/Service/" SERVICE),
		CFSTR("Setup:/Network/Service/" SERVICE "/IPv4"),
		CFSTR("Setup:/Network/Service/" SERVICE "/Interface"),
	};
	CFArrayRef remove = CFArrayCreate(NULL, keys, 9, &kCFTypeArrayCallBacks);
	SCDynamicStoreSetMultiple(store, NULL, remove, NULL);
	CFRelease(remove);
	printf("sweet-wlan: unpublished service %s\n", SERVICE);
}

static int
get_wlan_info(char *ip, char *mask, char *router, char *dns, char *ssid)
{
	char buf[256] = {0};
	size_t len = sizeof(buf) - 1;

	if (sysctlbyname("kern.sweet_wlan_info", buf, &len, NULL, 0) < 0) {
		return -1;
	}
	if (len == 0 || buf[0] == 0) {
		return -1;
	}
	if (sscanf(buf, "%63s %63s %63s %63s %63[^\n]", ip, mask, router, dns, ssid) < 4) {
		return -1;
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	bool monitor = false;
	bool unpublish = false;
	char ip[64] = {0}, mask[64] = {0}, router[64] = {0}, dns[64] = {0}, ssid[64] = {0};

	setvbuf(stdout, NULL, _IOLBF, 0);

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--monitor") == 0) {
			monitor = true;
		} else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--unpublish") == 0) {
			unpublish = true;
		} else if (ip[0] == 0) {
			strncpy(ip, argv[i], sizeof(ip) - 1);
		} else if (mask[0] == 0) {
			strncpy(mask, argv[i], sizeof(mask) - 1);
		} else if (router[0] == 0) {
			strncpy(router, argv[i], sizeof(router) - 1);
		} else if (dns[0] == 0) {
			strncpy(dns, argv[i], sizeof(dns) - 1);
		}
	}

	SCDynamicStoreRef store = SCDynamicStoreCreate(NULL, CFSTR("sweet-wlan"), NULL, NULL);
	if (store == NULL) {
		fprintf(stderr, "sweet-wlan: cannot connect to dynamic store: %s\n", SCErrorString(SCError()));
		return 1;
	}

	if (unpublish) {
		unpublish_service(store);
		return 0;
	}

	if (!monitor) {
		if (ip[0] == 0) {
			if (get_wlan_info(ip, mask, router, dns, ssid) < 0) {
				fprintf(stderr, "sweet-wlan: no active Wi-Fi lease in kern.sweet_wlan_info\n");
				return 1;
			}
		}
		return publish_service(store, ip, mask, router, dns);
	}

	printf("sweet-wlan: monitoring kern.sweet_wlan_info...\n");
	char last_ip[64] = {0};
	bool published = false;

	while (1) {
		char cur_ip[64] = {0}, cur_mask[64] = {0}, cur_router[64] = {0}, cur_dns[64] = {0}, cur_ssid[64] = {0};
		int ret = get_wlan_info(cur_ip, cur_mask, cur_router, cur_dns, cur_ssid);
		if (ret == 0 && cur_ip[0] != 0) {
			if (!published || strcmp(cur_ip, last_ip) != 0) {
				if (publish_service(store, cur_ip, cur_mask, cur_router, cur_dns) == 0) {
					strncpy(last_ip, cur_ip, sizeof(last_ip) - 1);
					published = true;
				}
			}
		} else {
			if (published) {
				unpublish_service(store);
				published = false;
				last_ip[0] = 0;
			}
		}
		sleep(2);
	}

	return 0;
}
