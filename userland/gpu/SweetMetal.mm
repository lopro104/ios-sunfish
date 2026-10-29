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
#include <pthread.h>
#include <os/log.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef id (*open_fn)(const char *loader, const char *cache, uint64_t registryID, void (*missing)(const char *, const char *));
typedef size_t (*table_fn)(char *out, size_t size);

/* The frontend's telemetry table (UniXNU's GPU8 rows: commit-to-complete times, pipelines, caches...) into <cache>/telemetry.txt every 5 s. */
struct telemetry_job {
	table_fn table;
	char path[600];
};

static void *
telemetry_thread(void *arg)
{
	struct telemetry_job *job = (struct telemetry_job *)arg;
	static char text[8192];

	for (;;) {
		sleep(5);
		size_t n = job->table(text, sizeof(text));
		FILE *f = fopen(job->path, "w");
		if (f != NULL) {
			fwrite(text, 1, n, f);
			fclose(f);
		}
	}
	return NULL;
}
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
	/*
	 * Turnip's TU_DEBUG: "nocb" (UniXNU: concurrent binning stalled passes), or the first line of the file
	 * tu_debug beside this executable (e.g. "nocb,sysmem": no tiles, straight to memory) for experiments.
	 */
	char options[256] = "nocb", file[1024];
	if (beside("tu_debug", file, sizeof(file))) {
		FILE *f = fopen(file, "r");
		if (f != NULL) {
			if (fgets(options, sizeof(options), f) == NULL) {
				strlcpy(options, "nocb", sizeof(options));
			}
			options[strcspn(options, "\r\n")] = 0;
			fclose(f);
		}
	}
	setenv("TU_DEBUG", options, 0);
	/* the file surface_gpu beside this executable: the frontend tells the kernel which surfaces are being drawn (xnu 0186) */
	if (beside("surface_gpu", file, sizeof(file))) {
		setenv("SWEETMETAL_SURFACE_GPU", "1", 0);
	}
	/*
	 * The file sync_commit beside this executable: the render server's command buffers are waited for in
	 * commit (frontend patch), until completed ("1", or an empty file) or until submitted ("2").
	 */
	if (beside("sync_commit", file, sizeof(file))) {
		char mode[8] = "1";
		FILE *f = fopen(file, "r");
		if (f != NULL) {
			if (fgets(mode, sizeof(mode), f) == NULL || (mode[0] != '1' && mode[0] != '2')) {
				strlcpy(mode, "1", sizeof(mode));
			}
			mode[1] = 0;
			fclose(f);
		}
		setenv("SWEETMETAL_SYNC_COMMIT", mode, 0);
		os_log(sweet_log(), "SweetMetal: SWEETMETAL_SYNC_COMMIT=%{public}s", mode);
	}
	os_log(sweet_log(), "SweetMetal: TU_DEBUG=%{public}s", getenv("TU_DEBUG"));
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
	table_fn table = handle ? (table_fn)dlsym(handle, "native_bridge_metal_telemetry") : NULL;
	if (device != nil && table != NULL) {
		struct telemetry_job *job = (struct telemetry_job *)calloc(1, sizeof(*job));
		pthread_t thread;
		if (job != NULL) {
			job->table = table;
			snprintf(job->path, sizeof(job->path), "%s/telemetry.txt", cache);
			if (pthread_create(&thread, NULL, telemetry_thread, job) == 0) {
				pthread_detach(thread);
			}
		}
	}
	return device;
}
@end
