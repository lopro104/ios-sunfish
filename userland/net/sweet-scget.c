/*
 * sweet-scget [PATTERN]  : print the SCDynamicStore keys matching PATTERN (default "State:/Network/Interface/.*") and their values.
 * sweet-scget watch PATTERN : poll once a second and print every change (to catch transient wrong values).
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>

typedef const struct __SCDynamicStore *SCDynamicStoreRef;
extern SCDynamicStoreRef SCDynamicStoreCreate(CFAllocatorRef, CFStringRef, void *, void *);
extern CFArrayRef SCDynamicStoreCopyKeyList(SCDynamicStoreRef, CFStringRef);
extern CFPropertyListRef SCDynamicStoreCopyValue(SCDynamicStoreRef, CFStringRef);

static void
cs(CFTypeRef o, char *buf, size_t n)
{
	CFStringRef d = o ? CFCopyDescription(o) : NULL;

	buf[0] = 0;
	if (d != NULL) {
		CFStringGetCString(d, buf, (CFIndex)n, kCFStringEncodingUTF8);
		CFRelease(d);
	}
}

int
main(int argc, char **argv)
{
	SCDynamicStoreRef st = SCDynamicStoreCreate(NULL, CFSTR("sweet-scget"), NULL, NULL);
	int watch = argc > 1 && strcmp(argv[1], "watch") == 0;
	const char *pat = argc > (watch ? 2 : 1) ? argv[watch ? 2 : 1] : "State:/Network/Interface/.*";
	CFStringRef p = CFStringCreateWithCString(NULL, pat, kCFStringEncodingUTF8);
	static char last[64][8192];
	int round = 0;

	setvbuf(stdout, NULL, _IONBF, 0);
	do {
		CFArrayRef keys = SCDynamicStoreCopyKeyList(st, p);

		for (CFIndex i = 0; keys != NULL && i < CFArrayGetCount(keys) && i < 64; i++) {
			char kb[256], vb[8192];
			CFStringRef k = CFArrayGetValueAtIndex(keys, i);

			CFStringGetCString(k, kb, sizeof(kb), kCFStringEncodingUTF8);
			cs(SCDynamicStoreCopyValue(st, k), vb, sizeof(vb));
			if (!watch || strcmp(vb, last[i]) != 0) {
				printf("[%d] %s = %s\n", round, kb, vb);
				strlcpy(last[i], vb, sizeof(last[i]));
			}
		}
		if (keys != NULL) {
			CFRelease(keys);
		}
		round++;
		if (watch) {
			sleep(1);
		}
	} while (watch && round < 120);
	return 0;
}
