/*
 * sweet-shot: a screenshot of what the panel shows. The kernel publishes the panel's own memory as
 * an IOSurface (SweetFramebuffer's default surface: "SWEET FB: AppleCLCD ... default surface N" in
 * the boot log); this looks it up by id and writes its pixels, as they are, to a file:
 *
 *   sweet-shot ID FILE        FILE: "SWSHOT" u32 width, height, bytes a row, pixel format, then rows
 *
 * tools/sweet-shot.py on the PC makes a PNG of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <IOSurface/IOSurfaceRef.h>

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "usage: sweet-shot SURFACE-ID FILE\n");
		return 2;
	}
	IOSurfaceRef s = IOSurfaceLookup((IOSurfaceID)strtoul(argv[1], NULL, 0));
	if (s == NULL) {
		fprintf(stderr, "sweet-shot: no surface %s\n", argv[1]);
		return 1;
	}
	IOSurfaceLock(s, kIOSurfaceLockReadOnly, NULL);
	uint32_t head[4] = { (uint32_t)IOSurfaceGetWidth(s), (uint32_t)IOSurfaceGetHeight(s),
		             (uint32_t)IOSurfaceGetBytesPerRow(s), IOSurfaceGetPixelFormat(s) };
	FILE *f = fopen(argv[2], "wb");
	if (f == NULL) {
		perror(argv[2]);
		return 1;
	}
	fwrite("SWSHOT", 1, 6, f);
	fwrite(head, sizeof(head), 1, f);
	fwrite(IOSurfaceGetBaseAddress(s), head[2], head[1], f);
	fclose(f);
	IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, NULL);
	printf("sweet-shot: surface %s, %u x %u, %u bytes a row -> %s\n", argv[1], head[0], head[1], head[2], argv[2]);
	CFRelease(s);
	return 0;
}
