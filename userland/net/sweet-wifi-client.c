/*
 * sweet-wifi-client: what a MobileWiFi client (Settings, SpringBoard) gets from wifid: the Wi-Fi devices, then the results of a scan.
 * Uses the private MobileWiFi API through dlsym: WiFiManagerClientCreate / CopyDevices, WiFiDeviceClientScanAsync, WiFiNetwork getters.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>

typedef void *WiFiManagerRef;
typedef void *WiFiDeviceRef;
typedef void *WiFiNetworkRef;

static CFStringRef (*pSSID)(WiFiNetworkRef);
static CFTypeRef (*pProp)(WiFiNetworkRef, CFStringRef);

static void
cstr(CFTypeRef s, char *buf, size_t n)
{
	buf[0] = 0;
	if (s != NULL && CFGetTypeID(s) == CFStringGetTypeID()) {
		CFStringGetCString(s, buf, (CFIndex)n, kCFStringEncodingUTF8);
	}
}

static void
scan_cb(WiFiDeviceRef dev, CFArrayRef results, CFErrorRef error, void *ctx)
{
	printf("scan callback: %ld networks, error %p\n", results ? (long)CFArrayGetCount(results) : -1L, error);
	if (results != NULL) {
		for (CFIndex i = 0; i < CFArrayGetCount(results); i++) {
			WiFiNetworkRef n = (WiFiNetworkRef)CFArrayGetValueAtIndex(results, i);
			char ssid[64], rssi[32] = "";
			CFTypeRef r = pProp ? pProp(n, CFSTR("RSSI")) : NULL;

			cstr(pSSID ? pSSID(n) : NULL, ssid, sizeof(ssid));
			if (r != NULL && CFGetTypeID(r) == CFNumberGetTypeID()) {
				int v = 0;

				CFNumberGetValue(r, kCFNumberIntType, &v);
				snprintf(rssi, sizeof(rssi), "%d", v);
			}
			printf("  %2ld  %-28s rssi %s\n", (long)i, ssid, rssi);
		}
	}
	fflush(stdout);
	CFRunLoopStop(CFRunLoopGetCurrent());
}

int
main(void)
{
	void *lib = dlopen("/System/Library/PrivateFrameworks/MobileWiFi.framework/MobileWiFi", RTLD_NOW);
	WiFiManagerRef (*Create)(CFAllocatorRef, int) = dlsym(lib, "WiFiManagerClientCreate");
	CFArrayRef (*CopyDevices)(WiFiManagerRef) = dlsym(lib, "WiFiManagerClientCopyDevices");
	void (*ScanAsync)(WiFiDeviceRef, CFDictionaryRef, void *, void *) = dlsym(lib, "WiFiDeviceClientScanAsync");
	void (*Schedule)(WiFiManagerRef, CFRunLoopRef, CFStringRef) = dlsym(lib, "WiFiManagerClientScheduleWithRunLoop");
	WiFiManagerRef mgr;
	CFArrayRef devs;

	setvbuf(stdout, NULL, _IONBF, 0);
	pSSID = dlsym(lib, "WiFiNetworkGetSSID");
	pProp = dlsym(lib, "WiFiNetworkGetProperty");
	if (lib == NULL || Create == NULL) {
		printf("no MobileWiFi: %s\n", dlerror());
		return 1;
	}
	mgr = Create(kCFAllocatorDefault, 0);
	printf("manager %p\n", mgr);
	if (mgr == NULL) {
		return 1;
	}
	if (Schedule) {
		Schedule(mgr, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	}
	CFRunLoopRunInMode(kCFRunLoopDefaultMode, 3, false);
	devs = CopyDevices ? CopyDevices(mgr) : NULL;
	printf("devices: %ld\n", devs ? (long)CFArrayGetCount(devs) : -1L);
	if (devs != NULL && CFArrayGetCount(devs) > 0 && ScanAsync) {
		ScanAsync((WiFiDeviceRef)CFArrayGetValueAtIndex(devs, 0), NULL, (void *)scan_cb, NULL);
		CFRunLoopRunInMode(kCFRunLoopDefaultMode, 30, false);
	}
	return 0;
}
