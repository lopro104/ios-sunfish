/*
 * sweet-tap X Y [HOLD_MS]: inject a single-finger tap at screen pixel (X, Y) of the 1080x2340 display through IOHIDEventSystemClient.
 * Digitizer events (a hand parent with one finger child), normalized to 0..1; the same thing the touch driver delivers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <CoreFoundation/CoreFoundation.h>

typedef void *HIDClient;
typedef void *HIDEvent;

static void *lib;

static void *
sym(const char *n)
{
	void *p = dlsym(lib, n);

	if (p == NULL) {
		fprintf(stderr, "missing %s\n", n);
		exit(1);
	}
	return p;
}

int
main(int argc, char **argv)
{
	HIDClient (*ClientCreate)(CFAllocatorRef) = NULL;
	int (*Dispatch)(HIDClient, HIDEvent) = NULL;
	HIDEvent (*CreateDigitizer)(CFAllocatorRef, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, double, double, double, double, double, int, int, uint32_t) = NULL;
	HIDEvent (*CreateFinger)(CFAllocatorRef, uint64_t, uint32_t, uint32_t, uint32_t, double, double, double, double, double, int, int, uint32_t) = NULL;
	void (*Append)(HIDEvent, HIDEvent, uint32_t) = NULL;
	void (*SetInt)(HIDEvent, uint32_t, int) = NULL;
	void (*SetSender)(HIDEvent, uint64_t) = NULL;
	double x, y;
	unsigned hold = argc > 3 ? (unsigned)atoi(argv[3]) : 60;
	HIDClient c;

	if (argc < 3) {
		fprintf(stderr, "usage: sweet-tap X Y [HOLD_MS]\n");
		return 2;
	}
	x = atof(argv[1]) / 1080.0;
	y = atof(argv[2]) / 2340.0;
	lib = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_NOW);
	ClientCreate = sym("IOHIDEventSystemClientCreate");
	Dispatch = sym("IOHIDEventSystemClientDispatchEvent");
	CreateDigitizer = sym("IOHIDEventCreateDigitizerEvent");
	CreateFinger = sym("IOHIDEventCreateDigitizerFingerEvent");
	Append = sym("IOHIDEventAppendEvent");
	SetInt = sym("IOHIDEventSetIntegerValue");
	SetSender = sym("IOHIDEventSetSenderID");
	c = ClientCreate(kCFAllocatorDefault);
	if (c == NULL) {
		fprintf(stderr, "no HID client\n");
		return 1;
	}
	for (int phase = 0; phase < 2; phase++) {
		uint64_t ts = mach_absolute_time();
		int touching = phase == 0;
		/* kIOHIDDigitizerEventRange 1, Touch 2, Position 4, Identity 0x20 */
		uint32_t mask = touching ? (1 | 2 | 0x20) : (1 | 2);
		HIDEvent parent = CreateDigitizer(kCFAllocatorDefault, ts, 3 /* hand */, 0, 0, mask, 0, x, y, 0, 0, 0, touching, touching, 0);
		HIDEvent finger = CreateFinger(kCFAllocatorDefault, ts, 1, 2, mask, x, y, 0, touching ? 1.0 : 0.0, 0, touching, touching, 0);

		SetInt(parent, 0xB0016 /* kIOHIDEventFieldDigitizerIsDisplayIntegrated */, 1);
		Append(parent, finger, 0);
		SetSender(parent, 0x8000000817319372ULL);
		printf("%s dispatch -> %d\n", touching ? "down" : "up", Dispatch(c, parent));
		if (touching) {
			usleep(hold * 1000);
		}
	}
	return 0;
}
