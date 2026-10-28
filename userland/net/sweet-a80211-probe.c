/*
 * sweet-a80211-probe: calls Apple's IO80211 library (the one wifid and Settings use) against ew0 and prints what it makes of the
 * kernel's SIOCGA80211 answers: GetPower, GetPowerState, CopyValue of some types, GetInfoCopy (a dictionary of many values).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>

static void
show(const char *what, CFTypeRef o)
{
	static char buf[262144];

	if (o == NULL) {
		printf("%s: (null)\n", what);
		return;
	}
	CFStringRef d = CFCopyDescription(o);

	if (d != NULL && CFStringGetCString(d, buf, sizeof(buf), kCFStringEncodingUTF8)) {
		printf("%s: %s\n", what, buf);
	}
	if (d != NULL) {
		CFRelease(d);
	}
}

int
main(int argc, char **argv)
{
	void *lib = dlopen("/System/Library/PrivateFrameworks/IO80211.framework/IO80211", RTLD_NOW);
	int (*Open)(void **) = dlsym(lib, "Apple80211Open");
	int (*Close)(void *) = dlsym(lib, "Apple80211Close");
	int (*Bind)(void *, CFStringRef) = dlsym(lib, "Apple80211BindToInterface");
	int (*GetPower)(void *, unsigned char *) = dlsym(lib, "Apple80211GetPower");
	int (*GetPowerState)(void *, int *) = dlsym(lib, "Apple80211GetPowerState");
	int (*CopyValue)(void *, int, CFTypeRef, CFTypeRef *) = dlsym(lib, "Apple80211CopyValue");
	int (*GetInfoCopy)(void *, CFDictionaryRef *) = dlsym(lib, "Apple80211GetInfoCopy");
	int (*Get)(void *, int, int, void *, unsigned long) = dlsym(lib, "Apple80211Get");
	int (*Scan)(void *, CFArrayRef *, CFDictionaryRef) = dlsym(lib, "Apple80211Scan");
	void *h = NULL;
	int rc;

	setvbuf(stdout, NULL, _IONBF, 0);
	if (lib == NULL || Open == NULL) {
		printf("no library: %s\n", dlerror());
		return 1;
	}
	rc = Open(&h);
	printf("Open -> %d\n", rc);
	rc = Bind(h, CFSTR("en0"));
	printf("Bind en0 -> %d\n", rc);
	if (GetPower) {
		unsigned char on = 99;

		rc = GetPower(h, &on);
		printf("GetPower -> %d on=%d\n", rc, on);
	}
	if (GetPowerState) {
		int st = 99;

		rc = GetPowerState(h, &st);
		printf("GetPowerState -> %d state=%d\n", rc, st);
	}
	if (argc > 1 && strcmp(argv[1], "scan") == 0 && Scan) {
		CFArrayRef res = NULL;

		rc = Scan(h, &res, NULL);
		printf("Scan -> %d\n", rc);
		show("  results", res);
		argc = 1;
	}
	for (int i = 1; i < argc; i++) {
		int type = atoi(argv[i]);
		CFTypeRef out = NULL;

		rc = CopyValue(h, type, NULL, &out);
		printf("CopyValue(%d) -> %d\n", type, rc);
		show("  value", out);
	}
	if (GetInfoCopy) {
		CFDictionaryRef info = NULL;

		rc = GetInfoCopy(h, &info);
		printf("GetInfoCopy -> %d\n", rc);
		show("  info", info);
	}
	if (Get && argc == 1) {
		unsigned char buf[256];

		memset(buf, 0, sizeof(buf));
		rc = Get(h, 13, 0, buf, 8);
		printf("raw Get(STATE) -> %d: %02x%02x%02x%02x %02x%02x%02x%02x\n", rc, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
	}
	if (Close) {
		Close(h);
	}
	return 0;
}
