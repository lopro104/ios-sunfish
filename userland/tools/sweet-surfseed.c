/*
 * sweet-surfseed: does an IOSurface's seed move when it is written? The GL layer (eagl) saw no
 * seed move in backboardd for any surface it refilled, and adds up every surface's memory instead.
 *
 *   sweet-surfseed                 make a surface, lock/write/unlock it, print the seeds
 *   sweet-surfseed ID              look surface ID up (another process's), print its seed 10 times
 *   sweet-surfseed ID poke         look it up, lock + unlock it for writing once, print the seed
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>

extern IOSurfaceRef IOSurfaceLookup(IOSurfaceID csid);

static void
put(CFMutableDictionaryRef d, CFStringRef key, long value)
{
	CFNumberRef n = CFNumberCreate(NULL, kCFNumberLongType, &value);
	CFDictionarySetValue(d, key, n);
	CFRelease(n);
}

int
main(int argc, char **argv)
{
	IOSurfaceRef s;
	uint32_t seed;

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (argc >= 2) {
		s = IOSurfaceLookup((IOSurfaceID)strtoul(argv[1], NULL, 0));
		if (s == NULL) {
			printf("no surface %s\n", argv[1]);
			return 1;
		}
		printf("surface %u: %zux%zu '%.4s' seed %u\n", IOSurfaceGetID(s), IOSurfaceGetWidth(s), IOSurfaceGetHeight(s),
		    (char *)&(uint32_t){ __builtin_bswap32(IOSurfaceGetPixelFormat(s)) }, IOSurfaceGetSeed(s));
		if (argc >= 3) {
			IOSurfaceLock(s, 0, &seed);
			printf("locked, seed %u\n", seed);
			IOSurfaceUnlock(s, 0, &seed);
			printf("unlocked, seed %u, IOSurfaceGetSeed %u\n", seed, IOSurfaceGetSeed(s));
			return 0;
		}
		for (int i = 0; i < 10; i++) {
			usleep(500000);
			printf("seed %u\n", IOSurfaceGetSeed(s));
		}
		return 0;
	}

	CFMutableDictionaryRef props = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	put(props, kIOSurfaceWidth, 64);
	put(props, kIOSurfaceHeight, 64);
	put(props, kIOSurfaceBytesPerElement, 4);
	put(props, kIOSurfacePixelFormat, 'BGRA');
	s = IOSurfaceCreate(props);
	if (s == NULL) {
		printf("IOSurfaceCreate failed\n");
		return 1;
	}
	printf("surface %u, seed %u\n", IOSurfaceGetID(s), IOSurfaceGetSeed(s));
	IOSurfaceLock(s, 0, &seed);
	printf("locked for writing: lock's seed %u, IOSurfaceGetSeed %u\n", seed, IOSurfaceGetSeed(s));
	memset(IOSurfaceGetBaseAddress(s), 0x55, 64 * 4);
	IOSurfaceUnlock(s, 0, &seed);
	printf("unlocked: unlock's seed %u, IOSurfaceGetSeed %u\n", seed, IOSurfaceGetSeed(s));
	IOSurfaceLock(s, kIOSurfaceLockReadOnly, &seed);
	IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, &seed);
	printf("read-only lock + unlock: seed %u, IOSurfaceGetSeed %u\n", seed, IOSurfaceGetSeed(s));
	printf("waiting 30 s: run 'sweet-surfseed %u poke' in another shell\n", IOSurfaceGetID(s));
	for (int i = 0; i < 6; i++) {
		sleep(5);
		printf("seed %u\n", IOSurfaceGetSeed(s));
	}
	return 0;
}
