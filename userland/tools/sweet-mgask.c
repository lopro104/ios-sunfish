/*
 * sweet-mgask: what MobileGestalt answers, for the keys named (or a default set about location and telephony).
 *
 *   sweet-mgask [KEY...]
 *
 * Read only: MGCopyAnswer for each key, printed as CoreFoundation describes it ("(null)" when it has no answer).
 */
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>

extern CFTypeRef MGCopyAnswer(CFStringRef question, CFDictionaryRef options);

static const char *defaults[] = {
	"location-services", "gps", "LocationServicesCapability", "GPSCapability", "gps-capable", "wifi",
	"telephony", "cellular-data", "TelephonyCapability", "HasBaseband", "ProductType", "HardwarePlatform",
	"DeviceClass", "magnetometer", "accelerometer", "gyroscope", "DeviceSupportsNavigation", NULL
};

int
main(int argc, char *argv[])
{
	const char **keys = argc > 1 ? (const char **)argv + 1 : defaults;
	for (int i = 0; keys[i] != NULL; i++) {
		CFStringRef k = CFStringCreateWithCString(NULL, keys[i], kCFStringEncodingUTF8);
		CFTypeRef v = MGCopyAnswer(k, NULL);
		char text[256] = "(null)";
		if (v != NULL) {
			CFStringRef d = CFCopyDescription(v);
			CFStringGetCString(d, text, sizeof(text), kCFStringEncodingUTF8);
			CFRelease(d);
			CFRelease(v);
		}
		printf("%-28s %s\n", keys[i], text);
		CFRelease(k);
	}
	return 0;
}
