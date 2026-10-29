/*
 * SweetMetal.bundle: the Metal plugin of the Pixel 4a's Adreno 618, found by Metal's own discovery
 * (MTLRegisterDevices: IOAcceleratorES "MetalPluginName" = SweetMetal, "MetalPluginClassName" =
 * SweetMetalDevice, both published by xnu's SweetFramebuffer only with boot-arg sweetmetal=1; the
 * bundle is /System/Library/Extensions/SweetMetal.bundle; the class must subclass _MTLDevice and
 * answer -initWithAcceleratorPort:).
 *
 * The device is UniXNU's native Metal frontend (AdrenoMetal NativeBridgeMetalFrontend.mm, MIT): a
 * dynamic subclass of _MTLDevice whose objects are the metal2vulkan runtime over Turnip, which talks
 * to SweetGPU's NativeAdreno service (xnu 0174/0176). It is SweetMetalCore.dylib beside this
 * executable, opened only in a listed process: loaded into every process that asks for Metal (its
 * classes and categories on Metal's own), it made SpringBoard die every few seconds although the
 * device was nil there. Turnip is libvulkan_freedreno.dylib beside it.
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

typedef id (*open_fn)(const char *loader, const char *cache, uint64_t registryID, void (*missing)(const char *, const char *));
extern "C" kern_return_t IORegistryEntryGetRegistryEntryID(unsigned entry, uint64_t *entryID);

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

/* `name` beside this executable. */
static bool
beside(const char *name, char *path, size_t size)
{
	Dl_info info = {};

	if (!dladdr((const void *)&beside, &info) || info.dli_fname == NULL) {
		return false;
	}
	snprintf(path, size, "%s", info.dli_fname);
	char *slash = strrchr(path, '/');
	if (slash == NULL) {
		return false;
	}
	snprintf(slash + 1, size - (size_t)(slash + 1 - path), "%s", name);
	return access(path, R_OK) == 0;
}

@implementation SweetMetalDevice
- (id)initWithAcceleratorPort:(unsigned)port
{
	char turnip[1024], core[1024], cache[512];
	uint64_t registryID = 0;

	/*
	 * Returning nil or another object releases self, and -[_MTLDevice dealloc] of one never initialized
	 * crashed (dispatch_release of its queues, inside CAMetalContextCreate: backboardd in a loop on the
	 * first sweetmetal=1 boot). Initializing it ([super init]) in every process that asks for Metal
	 * killed SpringBoard instead (sig 9 every few seconds). So self is never initialized and never
	 * released: one leaked, empty object per process that asks.
	 */
	(void)CFBridgingRetain(self);
	if (!listed()) {
		return nil;
	}
	if (!beside("libvulkan_freedreno.dylib", turnip, sizeof(turnip)) || !beside("SweetMetalCore.dylib", core, sizeof(core))) {
		os_log_error(sweet_log(), "SweetMetal: no Turnip or SweetMetalCore beside the plugin");
		return nil;
	}
	void *handle = dlopen(core, RTLD_NOW | RTLD_LOCAL);
	open_fn open_device = handle ? (open_fn)dlsym(handle, "native_bridge_metal_open") : NULL;
	if (open_device == NULL) {
		os_log_error(sweet_log(), "SweetMetal: %{public}s", dlerror());
		return nil;
	}
	setenv("TU_DARWIN_SERVICE", "NativeAdrenoGpuPublished", 0);
	setenv("TU_DEBUG", "nocb", 0);  /* (UniXNU: concurrent binning stalled passes) */
	/*
	 * The runtime refuses to open without a writable shader-cache directory. backboardd runs as mobile:
	 * a root-made /var/tmp/sweetmetal (0755) gave it none and so no device on the first try. The
	 * process's own cache directory first (as UniXNU's plugin does), else one under /var/tmp per user.
	 */
	cache[0] = 0;
	if (confstr(_CS_DARWIN_USER_CACHE_DIR, cache, sizeof(cache) - 32) != 0 && cache[0] != 0) {
		strlcat(cache, "sweetmetal", sizeof(cache));
		mkdir(cache, 0700);
	}
	if (cache[0] == 0 || access(cache, W_OK) != 0) {
		snprintf(cache, sizeof(cache), "/var/tmp/sweetmetal-%u", (unsigned)getuid());
		mkdir(cache, 0700);
	}
	if (access(cache, W_OK) != 0) {
		os_log_error(sweet_log(), "SweetMetal: no writable cache directory for %{public}s", getprogname());
		return nil;
	}
	if (port != 0) {
		IORegistryEntryGetRegistryEntryID(port, &registryID);
	}
	id device = nil;
	@try {
		device = open_device(turnip, cache, registryID, missing);
	} @catch (NSException *exception) {
		os_log_error(sweet_log(), "SweetMetal: %{public}@ opening the device", exception);
		device = nil;
	}
	os_log(sweet_log(), "SweetMetal: %{public}s gets %{public}s", getprogname(), device ? "the Adreno" : "no device");
	return device;
}
@end
