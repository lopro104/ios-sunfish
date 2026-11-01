/*
 * SweetTorch.dylib: Mock SBUIFlashlightController & AVFlashlight via ElleKit MSHookFunction
 *
 * Direct control of kern.sweet_torch sysctl on Pixel 4a (sunfish) and sweet.
 * Completely eliminates CoverSheet -> mediaserverd turnstile deadlock.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include <syslog.h>
#include <unistd.h>

#define SWEET_TORCH_SYSCTL      "kern.sweet_torch"
#define SWEET_TORCH_FULL_MA     150
#define SWEET_TORCH_MIN_MA      15

static void
torch_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	FILE *f = fopen("/tmp/torch.log", "a");
	if (f) {
		vfprintf(f, fmt, ap);
		fprintf(f, "\n");
		fflush(f);
		fclose(f);
	}
	va_end(ap);
}

static void
set_torch_ma(int ma)
{
	if (ma < 0) ma = 0;
	if (ma > 200) ma = 200;
	int err = sysctlbyname(SWEET_TORCH_SYSCTL, NULL, NULL, &ma, sizeof(ma));
	if (err != 0) {
		torch_log("SweetTorch: kern.sweet_torch=%d failed: %d", ma, err);
	} else {
		torch_log("SweetTorch: set torch to %d mA", ma);
	}
}

static void
hook_method_fn(Class cls, const char *sel_name, bool is_class_method, IMP hook_imp, IMP *orig_imp)
{
	void (*hook_fn)(void *, void *, void **) = (void (*)(void *, void *, void **))dlsym(RTLD_DEFAULT, "MSHookFunction");
	if (!cls || !hook_fn) {
		torch_log("SweetTorch: hook_method_fn missing cls or MSHookFunction");
		return;
	}
	SEL s = sel_registerName(sel_name);
	Method m = is_class_method ? class_getClassMethod(cls, s) : class_getInstanceMethod(cls, s);
	if (m != NULL) {
		IMP imp = method_getImplementation(m);
		hook_fn((void *)imp, (void *)hook_imp, (void **)orig_imp);
		torch_log("SweetTorch: MSHookFunction hooked %c[%s %s] at %p", is_class_method ? '+' : '-', class_getName(cls), sel_name, imp);
	} else {
		torch_log("SweetTorch: method not found: %c[%s %s]", is_class_method ? '+' : '-', class_getName(cls), sel_name);
	}
}

static void
hook_message_ex(Class cls, const char *sel_name, bool is_class_method, IMP hook_imp, IMP *orig_imp)
{
	void (*hook_msg)(Class, SEL, IMP, IMP *) = (void (*)(Class, SEL, IMP, IMP *))dlsym(RTLD_DEFAULT, "MSHookMessageEx");
	if (!cls) return;
	Class target_cls = is_class_method ? object_getClass((id)cls) : cls;
	SEL s = sel_registerName(sel_name);
	if (hook_msg) {
		hook_msg(target_cls, s, hook_imp, orig_imp);
		torch_log("SweetTorch: MSHookMessageEx hooked %c[%s %s]", is_class_method ? '+' : '-', class_getName(cls), sel_name);
	}
}

/* -------------------------------------------------------------------------- */
/* Platform Binary Spoofing (CoverSheet / SpringBoard SBStaticInitializer)    */
/* -------------------------------------------------------------------------- */

static uint32_t (*orig_SecTaskGetCodeSignStatus)(void *task) = NULL;

static uint32_t
hook_SecTaskGetCodeSignStatus(void *task)
{
	uint32_t s = orig_SecTaskGetCodeSignStatus ? orig_SecTaskGetCodeSignStatus(task) : 0;
	return s | 0x04000001; // CS_PLATFORM_BINARY | CS_VALID
}

static void
init_platform_binary(void)
{
	void (*hook_fn)(void *, void *, void **) = (void (*)(void *, void *, void **))dlsym(RTLD_DEFAULT, "MSHookFunction");
	if (hook_fn) {
		void *sym = dlsym(RTLD_DEFAULT, "SecTaskGetCodeSignStatus");
		if (sym) {
			hook_fn(sym, (void *)hook_SecTaskGetCodeSignStatus, (void **)&orig_SecTaskGetCodeSignStatus);
			torch_log("SweetTorch: hooked SecTaskGetCodeSignStatus");
		}
	}
}

/* -------------------------------------------------------------------------- */
/* TCC Privacy Hooks                                                          */
/* -------------------------------------------------------------------------- */

static void
hook_TCCAccessRequest(CFStringRef service, CFDictionaryRef options, void (^completionHandler)(Boolean))
{
	torch_log("SweetTorch: TCCAccessRequest intercepted");
	if (completionHandler) {
		completionHandler(true);
	}
}

static int
hook_TCCAccessPreflight(CFStringRef service, CFDictionaryRef options)
{
	return 0; // 0 = kTCCAccessPreflightAuthorized
}

static void
init_tcc_hooks(void)
{
	void (*hook_fn)(void *, void *, void **) = (void (*)(void *, void *, void **))dlsym(RTLD_DEFAULT, "MSHookFunction");
	if (!hook_fn) return;

	dlopen("/System/Library/PrivateFrameworks/TCC.framework/TCC", RTLD_NOW);
	void *req = dlsym(RTLD_DEFAULT, "TCCAccessRequest");
	if (req) {
		hook_fn(req, (void *)hook_TCCAccessRequest, NULL);
		torch_log("SweetTorch: hooked TCCAccessRequest");
	}
	void *pre = dlsym(RTLD_DEFAULT, "TCCAccessPreflight");
	if (pre) {
		hook_fn(pre, (void *)hook_TCCAccessPreflight, NULL);
		torch_log("SweetTorch: hooked TCCAccessPreflight");
	}
}

/* -------------------------------------------------------------------------- */
/* BackBoardServices Touch Delivery Observation Fix                           */
/* -------------------------------------------------------------------------- */

static void
hook_bks_exit_suppress(void)
{
	torch_log("SweetTorch: BackBoardServices exit(0) suppressed! SpringBoard staying alive.");
}

static void
hook_bks_nop(id self, SEL sel)
{
	torch_log("SweetTorch: BKSTouchDeliveryObservationService _connectToTouchDeliveryService suppressed!");
}

static void
init_bks_hooks(void)
{
	void (*hook_fn)(void *, void *, void **) = (void (*)(void *, void *, void **))dlsym(RTLD_DEFAULT, "MSHookFunction");
	if (!hook_fn) return;

	dlopen("/System/Library/PrivateFrameworks/BackBoardServices.framework/BackBoardServices", RTLD_NOW);
	Class bksCls = objc_getClass("BKSHIDEventDeliveryManager");
	Dl_info info;
	if (bksCls && dladdr((void *)bksCls, &info) && info.dli_fbase) {
		char *fbase = (char *)info.dli_fbase;
		void *fn1 = fbase + 0x3b718; // "shell service invalidation -- I'm out"
		void *fn2 = fbase + 0x3cc54; // "backboardd must be going down, exiting"
		if (*(uint32_t *)fn1 == 0xd100c3ff) {
			hook_fn(fn1, (void *)hook_bks_exit_suppress, NULL);
			torch_log("SweetTorch: hooked BackBoardServices fn1 (0x3b718)");
		}
		if (*(uint32_t *)fn2 == 0xd100c3ff) {
			hook_fn(fn2, (void *)hook_bks_exit_suppress, NULL);
			torch_log("SweetTorch: hooked BackBoardServices fn2 (0x3cc54)");
		}
	}

	Class obsCls = objc_getClass("BKSTouchDeliveryObservationService");
	if (obsCls) {
		hook_method_fn(obsCls, "_connectToTouchDeliveryService", false, (IMP)hook_bks_nop, NULL);
		hook_message_ex(obsCls, "_connectToTouchDeliveryService", false, (IMP)hook_bks_nop, NULL);
		torch_log("SweetTorch: hooked BKSTouchDeliveryObservationService _connectToTouchDeliveryService");
	}
}

/* -------------------------------------------------------------------------- */
/* BaseBoard Container Fallback Fix                                           */
/* -------------------------------------------------------------------------- */

static id
hook_bs_defaultPath(id self, SEL sel)
{
	Class urlCls = objc_getClass("NSURL");
	return ((id (*)(id, SEL, id))objc_msgSend)((id)urlCls, sel_registerName("fileURLWithPath:"), (id)CFSTR("/var/mobile"));
}

static id
hook_bs_libraryPath(id self, SEL sel)
{
	Class urlCls = objc_getClass("NSURL");
	return ((id (*)(id, SEL, id))objc_msgSend)((id)urlCls, sel_registerName("fileURLWithPath:"), (id)CFSTR("/var/mobile/Library"));
}

static id
hook_bs_cachesPath(id self, SEL sel)
{
	Class urlCls = objc_getClass("NSURL");
	return ((id (*)(id, SEL, id))objc_msgSend)((id)urlCls, sel_registerName("fileURLWithPath:"), (id)CFSTR("/var/mobile/Library/Caches"));
}

static void
init_baseboard_paths(void)
{
	dlopen("/System/Library/PrivateFrameworks/BaseBoard.framework/BaseBoard", RTLD_NOW);
	Class bs_cls = objc_getClass("BSSystemContainerForCurrentProcessPathProvider");
	if (bs_cls) {
		hook_method_fn(bs_cls, "defaultPath", false, (IMP)hook_bs_defaultPath, NULL);
		hook_method_fn(bs_cls, "libraryPath", false, (IMP)hook_bs_libraryPath, NULL);
		hook_method_fn(bs_cls, "cachesPath", false, (IMP)hook_bs_cachesPath, NULL);
		hook_message_ex(bs_cls, "defaultPath", false, (IMP)hook_bs_defaultPath, NULL);
		hook_message_ex(bs_cls, "libraryPath", false, (IMP)hook_bs_libraryPath, NULL);
		hook_message_ex(bs_cls, "cachesPath", false, (IMP)hook_bs_cachesPath, NULL);
	}
}

/* -------------------------------------------------------------------------- */
/* Flashlight Mock State & Observers                                          */
/* -------------------------------------------------------------------------- */

static bool sb_torch_on = false;
static unsigned long long sb_torch_level = 1;
static id sb_observers = nil;
static id g_mock_controller = nil;

static void
sb_notify_observers(void)
{
	unsigned long long lvl = sb_torch_on ? sb_torch_level : 0;
	torch_log("SweetTorch: notifying observers of level %llu", lvl);

	if (sb_observers) {
		id copy = ((id (*)(id, SEL))objc_msgSend)(sb_observers, sel_registerName("copy"));
		if (copy) {
			unsigned long count = ((unsigned long (*)(id, SEL))objc_msgSend)(copy, sel_registerName("count"));
			for (unsigned long i = 0; i < count; i++) {
				id item = ((id (*)(id, SEL, unsigned long))objc_msgSend)(copy, sel_registerName("objectAtIndex:"), i);
				if (item) {
					if (((BOOL (*)(id, SEL, SEL))objc_msgSend)(item, sel_registerName("respondsToSelector:"), sel_registerName("flashlightLevelDidChange:"))) {
						((void (*)(id, SEL, unsigned long long))objc_msgSend)(item, sel_registerName("flashlightLevelDidChange:"), lvl);
					}
					if (((BOOL (*)(id, SEL, SEL))objc_msgSend)(item, sel_registerName("respondsToSelector:"), sel_registerName("flashlightAvailabilityDidChange:"))) {
						((void (*)(id, SEL, BOOL))objc_msgSend)(item, sel_registerName("flashlightAvailabilityDidChange:"), YES);
					}
				}
			}
			((void (*)(id, SEL))objc_msgSend)(copy, sel_registerName("release"));
		}
	}
}

static id
hook_sb_sharedInstance(id self, SEL sel)
{
	torch_log("SweetTorch: +[SBUIFlashlightController sharedInstance] called! Returning mock singleton.");
	if (!g_mock_controller) {
		Class cls = objc_getClass("SBUIFlashlightController");
		id obj = ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("alloc"));
		struct objc_super sup = {
			.receiver = obj,
			.super_class = objc_getClass("NSObject")
		};
		g_mock_controller = ((id (*)(struct objc_super *, SEL))objc_msgSendSuper)(&sup, sel_registerName("init"));
		torch_log("SweetTorch: mock singleton initialized: %p", g_mock_controller);
	}
	return g_mock_controller;
}

static id
hook_sb_init(id self, SEL sel)
{
	torch_log("SweetTorch: -[SBUIFlashlightController init] called!");
	if (!g_mock_controller) {
		g_mock_controller = self;
	}
	return g_mock_controller ? g_mock_controller : self;
}

static void
hook_sb_addObserver(id self, SEL sel, id obs)
{
	torch_log("SweetTorch: SBUIFlashlightController addObserver: %p", obs);
	if (!sb_observers) {
		Class cls = objc_getClass("NSMutableArray");
		sb_observers = ((id (*)(id, SEL))objc_msgSend)((id)cls, sel_registerName("new"));
	}
	if (obs && sb_observers) {
		BOOL contains = ((BOOL (*)(id, SEL, id))objc_msgSend)(sb_observers, sel_registerName("containsObject:"), obs);
		if (!contains) {
			((void (*)(id, SEL, id))objc_msgSend)(sb_observers, sel_registerName("addObject:"), obs);
		}
		if (((BOOL (*)(id, SEL, SEL))objc_msgSend)(obs, sel_registerName("respondsToSelector:"), sel_registerName("flashlightAvailabilityDidChange:"))) {
			((void (*)(id, SEL, BOOL))objc_msgSend)(obs, sel_registerName("flashlightAvailabilityDidChange:"), YES);
		}
		if (((BOOL (*)(id, SEL, SEL))objc_msgSend)(obs, sel_registerName("respondsToSelector:"), sel_registerName("flashlightLevelDidChange:"))) {
			((void (*)(id, SEL, unsigned long long))objc_msgSend)(obs, sel_registerName("flashlightLevelDidChange:"), sb_torch_on ? sb_torch_level : 0);
		}
	}
}

static void
hook_sb_removeObserver(id self, SEL sel, id obs)
{
	if (sb_observers && obs) {
		((void (*)(id, SEL, id))objc_msgSend)(sb_observers, sel_registerName("removeObject:"), obs);
	}
}

static void
hook_sb_removeAllObservers(id self, SEL sel)
{
	if (sb_observers) {
		((void (*)(id, SEL))objc_msgSend)(sb_observers, sel_registerName("removeAllObjects"));
	}
}

static void
hook_sb_turnOn(id self, SEL sel, id reason)
{
	torch_log("SweetTorch: turnFlashlightOn");
	sb_torch_on = true;
	if (sb_torch_level == 0) sb_torch_level = 1;
	sb_notify_observers();
	set_torch_ma(SWEET_TORCH_FULL_MA);
}

static void
hook_sb_turnOff(id self, SEL sel, id reason)
{
	torch_log("SweetTorch: turnFlashlightOff");
	sb_torch_on = false;
	sb_notify_observers();
	set_torch_ma(0);
}

static void
hook_sb_setLevel(id self, SEL sel, unsigned long long level)
{
	torch_log("SweetTorch: setLevel: %llu", level);
	sb_torch_on = (level > 0);
	if (level > 0) sb_torch_level = level;
	sb_notify_observers();

	int ma = 0;
	if (sb_torch_on) {
		ma = (int)(sb_torch_level * 35 + SWEET_TORCH_MIN_MA);
		if (ma > SWEET_TORCH_FULL_MA) ma = SWEET_TORCH_FULL_MA;
	}
	set_torch_ma(ma);
}

static unsigned long long hook_sb_loadLevel(id self, SEL sel) { return sb_torch_level ? sb_torch_level : 1; }
static void hook_sb_storeLevel(id self, SEL sel, unsigned long long level) { if (level > 0) sb_torch_level = level; }
static BOOL hook_sb_isAvailable(id self, SEL sel) { return YES; }
static BOOL hook_sb_isOverheated(id self, SEL sel) { return NO; }
static unsigned long long hook_sb_level(id self, SEL sel) { return sb_torch_on ? sb_torch_level : 0; }
static unsigned long long hook_sb_numLevels(id self, SEL sel) { return 4; }
static void hook_sb_nop(id self, SEL sel) {}

static void
init_flashlight_hooks(void)
{
	// Satisfy Apple assertion in +[SpringBoardUI load] (<rdar://problem/26143166>)
	Class nsobj = objc_getClass("NSObject");
	if (!objc_getClass("SearchUI") && nsobj) {
		Class searchUI = objc_allocateClassPair(nsobj, "SearchUI", 0);
		if (searchUI) objc_registerClassPair(searchUI);
	}

	Class cls = objc_getClass("SBUIFlashlightController");
	if (cls == NULL) {
		dlopen("/System/Library/PrivateFrameworks/SpringBoardUI.framework/SpringBoardUI", RTLD_NOW);
		cls = objc_getClass("SBUIFlashlightController");
	}

	if (cls != NULL) {
		// Hook +sharedInstance via MSHookFunction on implementation
		hook_method_fn(cls, "sharedInstance", true, (IMP)hook_sb_sharedInstance, NULL);
		hook_message_ex(cls, "sharedInstance", true, (IMP)hook_sb_sharedInstance, NULL);

		hook_method_fn(cls, "init", false, (IMP)hook_sb_init, NULL);
		hook_message_ex(cls, "init", false, (IMP)hook_sb_init, NULL);

		hook_method_fn(cls, "observeValueForKeyPath:ofObject:change:context:", false, (IMP)hook_sb_nop, NULL);
		hook_message_ex(cls, "observeValueForKeyPath:ofObject:change:context:", false, (IMP)hook_sb_nop, NULL);

		hook_method_fn(cls, "addObserver:", false, (IMP)hook_sb_addObserver, NULL);
		hook_method_fn(cls, "removeObserver:", false, (IMP)hook_sb_removeObserver, NULL);
		hook_method_fn(cls, "removeAllObservers", false, (IMP)hook_sb_removeAllObservers, NULL);
		hook_method_fn(cls, "turnFlashlightOnForReason:", false, (IMP)hook_sb_turnOn, NULL);
		hook_method_fn(cls, "turnFlashlightOffForReason:", false, (IMP)hook_sb_turnOff, NULL);
		hook_method_fn(cls, "_turnPowerOn", false, (IMP)hook_sb_turnOn, NULL);
		hook_method_fn(cls, "_turnPowerOff", false, (IMP)hook_sb_turnOff, NULL);
		hook_method_fn(cls, "setLevel:", false, (IMP)hook_sb_setLevel, NULL);
		hook_method_fn(cls, "_setFlashlightLevel:", false, (IMP)hook_sb_setLevel, NULL);
		hook_method_fn(cls, "_loadFlashlightLevel", false, (IMP)hook_sb_loadLevel, NULL);
		hook_method_fn(cls, "_storeFlashlightLevel:", false, (IMP)hook_sb_storeLevel, NULL);
		hook_method_fn(cls, "isAvailable", false, (IMP)hook_sb_isAvailable, NULL);
		hook_method_fn(cls, "isOverheated", false, (IMP)hook_sb_isOverheated, NULL);
		hook_method_fn(cls, "level", false, (IMP)hook_sb_level, NULL);
		class_addMethod(cls, sel_registerName("numberOfFlashLevels"), (IMP)hook_sb_numLevels, "Q@:");
		hook_method_fn(cls, "numberOfFlashLevels", false, (IMP)hook_sb_numLevels, NULL);
		hook_method_fn(cls, "warmUp", false, (IMP)hook_sb_nop, NULL);
		hook_method_fn(cls, "coolDown", false, (IMP)hook_sb_nop, NULL);

		// Also MSHookMessageEx for all
		hook_message_ex(cls, "addObserver:", false, (IMP)hook_sb_addObserver, NULL);
		hook_message_ex(cls, "removeObserver:", false, (IMP)hook_sb_removeObserver, NULL);
		hook_message_ex(cls, "removeAllObservers", false, (IMP)hook_sb_removeAllObservers, NULL);
		hook_message_ex(cls, "turnFlashlightOnForReason:", false, (IMP)hook_sb_turnOn, NULL);
		hook_message_ex(cls, "turnFlashlightOffForReason:", false, (IMP)hook_sb_turnOff, NULL);
		hook_message_ex(cls, "_turnPowerOn", false, (IMP)hook_sb_turnOn, NULL);
		hook_message_ex(cls, "_turnPowerOff", false, (IMP)hook_sb_turnOff, NULL);
		hook_message_ex(cls, "setLevel:", false, (IMP)hook_sb_setLevel, NULL);
		hook_message_ex(cls, "_setFlashlightLevel:", false, (IMP)hook_sb_setLevel, NULL);
		hook_message_ex(cls, "_loadFlashlightLevel", false, (IMP)hook_sb_loadLevel, NULL);
		hook_message_ex(cls, "_storeFlashlightLevel:", false, (IMP)hook_sb_storeLevel, NULL);
		hook_message_ex(cls, "isAvailable", false, (IMP)hook_sb_isAvailable, NULL);
		hook_message_ex(cls, "isOverheated", false, (IMP)hook_sb_isOverheated, NULL);
		hook_message_ex(cls, "level", false, (IMP)hook_sb_level, NULL);
		hook_message_ex(cls, "numberOfFlashLevels", false, (IMP)hook_sb_numLevels, NULL);
	}
}

/* -------------------------------------------------------------------------- */
/* AVFlashlight Hooks (AVFCapture)                                            */
/* -------------------------------------------------------------------------- */

static id
hook_avf_init(id self, SEL sel)
{
	torch_log("SweetTorch: AVFlashlight init intercepted!");
	return self;
}

static BOOL hook_avf_hasFlashlight(id self, SEL sel) { return YES; }

static void
init_avflashlight_hooks(void)
{
	dlopen("/System/Library/PrivateFrameworks/AVFCapture.framework/AVFCapture", RTLD_NOW);
	Class cls = objc_getClass("AVFlashlight");
	if (cls != NULL) {
		hook_method_fn(cls, "hasFlashlight", true, (IMP)hook_avf_hasFlashlight, NULL);
		hook_method_fn(cls, "init", false, (IMP)hook_avf_init, NULL);
		hook_method_fn(cls, "_setupFlashlight", false, (IMP)hook_sb_nop, NULL);
		hook_method_fn(cls, "_teardownFlashlight", false, (IMP)hook_sb_nop, NULL);
		hook_method_fn(cls, "_reconnectToServer", false, (IMP)hook_sb_nop, NULL);

		hook_message_ex(cls, "hasFlashlight", true, (IMP)hook_avf_hasFlashlight, NULL);
		hook_message_ex(cls, "init", false, (IMP)hook_avf_init, NULL);
		hook_message_ex(cls, "_setupFlashlight", false, (IMP)hook_sb_nop, NULL);
		hook_message_ex(cls, "_teardownFlashlight", false, (IMP)hook_sb_nop, NULL);
		hook_message_ex(cls, "_reconnectToServer", false, (IMP)hook_sb_nop, NULL);
		torch_log("SweetTorch: AVFlashlight hooks installed!");
	}
}

static void
sweet_atexit_handler(void)
{
	void *callstack[32];
	int frames = backtrace(callstack, 32);
	char **strs = backtrace_symbols(callstack, frames);
	torch_log("SweetTorch: atexit triggered! Frame count: %d", frames);
	if (strs) {
		for (int i = 0; i < frames; ++i) {
			torch_log("  [%02d] %s", i, strs[i]);
		}
		free(strs);
	}
}

/* -------------------------------------------------------------------------- */
/* Constructor Entrypoint                                                    */
/* -------------------------------------------------------------------------- */

__attribute__((constructor))
static void
sweet_torch_init(void)
{
	const char *prog = getprogname();
	torch_log("SweetTorch: ctor called in %s (pid=%d)", prog ? prog : "unknown", getpid());
	unlink("/var/mobile/.eksafemode");

	if (prog && strcmp(prog, "SpringBoard") == 0) {
		atexit(sweet_atexit_handler);
		init_platform_binary();
		init_tcc_hooks();
		init_bks_hooks();
		init_baseboard_paths();
		init_flashlight_hooks();
		init_avflashlight_hooks();
	}
}
