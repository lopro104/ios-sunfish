/*
 * m2vtest: UniXNU's native Metal bridge (AdrenoMetal bridge/NativeMetalBridge.rs: the metal2vulkan
 * runtime over Turnip) runs its own self-test on the Pixel 4a: a Vulkan device through Turnip and
 * SweetGPU's NativeAdreno service (xnu 0174), the runtime's AIR fixture library, a compute kernel
 * (1000 outputs), a render pass (8x8) and a draw into imported host pages (64x64).
 *
 *   m2vtest [/path/to/libvulkan_freedreno.dylib [cache dir]]
 *
 * Each step's marker is printed as it starts (0x6501 device, 0x6502 library, 0x6503 compute,
 * 0x6504 render, 0x6505 import, 0x650f done). Exit 0 only if every step ran with no wrong value.
 * The import step needs NativeAdreno BoImport, which 0174 does not have yet: expect it to fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "NativeMetalBridge.h"

static void
mark(uint32_t marker)
{
	static const char *names[] = {"", "device", "fixture library", "compute", "render", "import", "QuartzCore"};
	unsigned step = marker & 0xff;

	printf("step %#x: %s\n", marker, marker == 0x650f ? "done" : step < 7 ? names[step] : "?");
	fflush(stdout);
}

int
main(int argc, char **argv)
{
	const char *loader = argc > 1 ? argv[1] : "/var/jb/usr/lib/libvulkan_freedreno.dylib";
	const char *cache = argc > 2 ? argv[2] : "/var/tmp/m2v-cache";
	struct NmbSelftest result;

	setenv("TU_DARWIN_SERVICE", "NativeAdrenoGpuPublished", 0);
	mkdir(cache, 0755);
	uint32_t code = nmb_selftest(loader, cache, NULL, 0, mark, &result);
	printf("result %#x: stage %#x, compute wrong %u/1000, render wrong %u/64, import wrong %u/4096\n", code, result.stage,
	    result.compute_bad, result.render_bad, result.import_bad);
	if (result.message[0]) {
		printf("message: %.96s\n", result.message);
	}
	return code == 0 && result.compute_bad == 0 && result.render_bad == 0 && result.import_bad == 0 ? 0 : 1;
}
