/*
 * sweet-surfplane: what IOSurface.framework reports for the planes of a two-plane surface, to find
 * where in the kernel's per-plane record (SweetSurface's lock result, 0x68 + 0x80 * plane) it
 * reads each field. The kernel had IOSurfaceGetBytesPerRowOfPlane answer 0 for every plane.
 * With a test kernel that fills the record's unused offsets with 0x5100<offset>, a value of that
 * form names the offset the framework reads.
 */
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurfaceRef.h>

static void
put(CFMutableDictionaryRef d, CFStringRef key, long value)
{
	CFNumberRef n = CFNumberCreate(NULL, kCFNumberLongType, &value);
	CFDictionarySetValue(d, key, n);
	CFRelease(n);
}

int
main(void)
{
	CFMutableDictionaryRef props = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFMutableArrayRef planes = CFArrayCreateMutable(NULL, 2, &kCFTypeArrayCallBacks);
	long w[2] = { 64, 32 }, h[2] = { 32, 16 }, bpe[2] = { 1, 2 }, bpr[2] = { 1000, 2000 }, off[2] = { 0, 65536 };

	put(props, kIOSurfaceWidth, 64);
	put(props, kIOSurfaceHeight, 32);
	put(props, kIOSurfacePixelFormat, '420f');
	for (int i = 0; i < 2; i++) {
		CFMutableDictionaryRef p = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		put(p, kIOSurfacePlaneWidth, w[i]);
		put(p, kIOSurfacePlaneHeight, h[i]);
		put(p, kIOSurfacePlaneBytesPerElement, bpe[i]);
		put(p, kIOSurfacePlaneBytesPerRow, bpr[i]);
		put(p, kIOSurfacePlaneOffset, off[i]);
		put(p, kIOSurfacePlaneSize, bpr[i] * h[i]);
		CFArrayAppendValue(planes, p);
		CFRelease(p);
	}
	CFDictionarySetValue(props, kIOSurfacePlaneInfo, planes);
	IOSurfaceRef s = IOSurfaceCreate(props);
	if (s == NULL) {
		printf("IOSurfaceCreate failed\n");
		return 1;
	}
	printf("surface %u: %zu x %zu, %zu planes, bytes a row %zu, alloc %zu\n", IOSurfaceGetID(s), IOSurfaceGetWidth(s),
	    IOSurfaceGetHeight(s), IOSurfaceGetPlaneCount(s), IOSurfaceGetBytesPerRow(s), IOSurfaceGetAllocSize(s));
	IOSurfaceLock(s, 0, NULL);
	char *base = IOSurfaceGetBaseAddress(s);
	for (size_t i = 0; i < 2; i++) {
		printf("plane %zu (asked w %ld h %ld bpe %ld bpr %ld off %ld): width %#zx height %#zx bpe %#zx bpr %#zx ew %#zx eh %#zx base+%#lx\n",
		    i, w[i], h[i], bpe[i], bpr[i], off[i], IOSurfaceGetWidthOfPlane(s, i), IOSurfaceGetHeightOfPlane(s, i),
		    IOSurfaceGetBytesPerElementOfPlane(s, i), IOSurfaceGetBytesPerRowOfPlane(s, i),
		    IOSurfaceGetElementWidthOfPlane(s, i), IOSurfaceGetElementHeightOfPlane(s, i),
		    (long)((char *)IOSurfaceGetBaseAddressOfPlane(s, i) - base));
	}
	IOSurfaceUnlock(s, 0, NULL);
	CFRelease(s);
	return 0;
}
