/*
 * SweetMetal.bundle: the Metal plugin of the Pixel 4a's Adreno 618, found by Metal's own discovery
 * (MTLRegisterDevices: IOAcceleratorES "MetalPluginName" = SweetMetal, "MetalPluginClassName" =
 * SweetMetalDevice, both published by xnu's SweetFramebuffer only with boot-arg sweetmetal=1; the
 * bundle is /System/Library/Extensions/SweetMetal.bundle; the class must subclass _MTLDevice and
 * answer -initWithAcceleratorPort:).
 *
 * The device is UniXNU's native Metal frontend (AdrenoMetal NativeBridgeMetalFrontend.mm, MIT): a
 * dynamic subclass of _MTLDevice whose objects are the metal2vulkan runtime over Turnip, which talks
 * to SweetGPU's NativeAdreno service (xnu 0174/0176). Turnip is libvulkan_freedreno.dylib beside
 * this executable in the bundle.
 *
 * Only the processes named in /var/jb/etc/sweetmetal.clients (one name per line, "*" for all) get
 * the device; every other one gets nil, which is what Metal had before the plugin existed (no
 * device, the renderers stay as they are). The window server is not to be listed until Metal draws
 * the system UI correctly.
 */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <os/log.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" id native_bridge_metal_open(const char *loader, const char *cache, uint64_t registryID,
    void (*missing)(const char *, const char *));
extern "C" kern_return_t IORegistryEntryGetRegistryEntryID(unsigned entry, uint64_t *entryID);
/* The frontend's telemetry reads UniXNU's shader-priming counters (their PrimeShaders.h); there is no priming here yet. */
extern "C" {
volatile uint32_t native_prime_compiling, native_prime_libraries_done, native_prime_libraries_total, native_prime_made, native_prime_rate;
}

@interface _MTLDevice : NSObject
@end

@interface SweetMetalDevice : _MTLDevice
- (id)initWithAcceleratorPort:(unsigned)port;
@end

static os_log_t
sweet_log(void)
{
	static os_log_t log;
	static dispatch_once_t once;
	dispatch_once(&once, ^{
		log = os_log_create("com.sunfish.SweetMetal", "plugin");
	});
	return log;
}

static void
missing(const char *cls, const char *sel)
{
	os_log(sweet_log(), "SweetMetal: %{public}s has no %{public}s", cls ? cls : "?", sel ? sel : "?");
}

/* Is this process in /var/jb/etc/sweetmetal.clients? */
static bool
listed(void)
{
	const char *me = getprogname();
	FILE *f = fopen("/var/jb/etc/sweetmetal.clients", "r");
	char line[256];
	bool yes = false;

	if (f == NULL) {
		return false;
	}
	while (!yes && fgets(line, sizeof(line), f) != NULL) {
		line[strcspn(line, "\r\n \t#")] = 0;
		yes = line[0] != 0 && (strcmp(line, "*") == 0 || strcmp(line, me) == 0);
	}
	fclose(f);
	return yes;
}

/* libvulkan_freedreno.dylib beside this executable. */
static bool
turnip_path(char *path, size_t size)
{
	Dl_info info = {};

	if (!dladdr((const void *)&turnip_path, &info) || info.dli_fname == NULL) {
		return false;
	}
	snprintf(path, size, "%s", info.dli_fname);
	char *slash = strrchr(path, '/');
	if (slash == NULL) {
		return false;
	}
	snprintf(slash + 1, size - (size_t)(slash + 1 - path), "libvulkan_freedreno.dylib");
	return access(path, R_OK) == 0;
}

@implementation SweetMetalDevice
- (id)initWithAcceleratorPort:(unsigned)port
{
	char turnip[1024], cache[512];
	uint64_t registryID = 0;

	if (!listed()) {
		return nil;
	}
	if (!turnip_path(turnip, sizeof(turnip))) {
		os_log_error(sweet_log(), "SweetMetal: no Turnip beside the plugin");
		return nil;
	}
	setenv("TU_DARWIN_SERVICE", "NativeAdrenoGpuPublished", 0);
	setenv("TU_DEBUG", "nocb", 0);  /* (UniXNU: concurrent binning stalled passes) */
	snprintf(cache, sizeof(cache), "/var/tmp/sweetmetal");
	mkdir(cache, 0777);
	strlcat(cache, "/", sizeof(cache));
	strlcat(cache, getprogname(), sizeof(cache));
	mkdir(cache, 0700);
	if (port != 0) {
		IORegistryEntryGetRegistryEntryID(port, &registryID);
	}
	id device = nil;
	@try {
		device = native_bridge_metal_open(turnip, cache, registryID, missing);
	} @catch (NSException *exception) {
		os_log_error(sweet_log(), "SweetMetal: %{public}@ opening the device", exception);
		device = nil;
	}
	os_log(sweet_log(), "SweetMetal: %{public}s gets %{public}s", getprogname(), device ? "the Adreno" : "no device");
	return device;
}
@end
