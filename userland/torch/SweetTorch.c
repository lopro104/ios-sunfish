/*
 * SweetTorch.dylib: Control Center & Lock Screen Flashlight for iOS on Snapdragon (Pixel 4a & sweet).
 *
 * Direct control of kern.sweet_torch sysctl (milliamps per LED).
 * Hooks:
 * 1. SBUIFlashlightController (SpringBoardUI) - used by Control Center and Lock Screen flashlight buttons
 * 2. AVFlashlight (AVFoundation) - standard public/private API for flashlight
 * 3. BWFigCaptureDeviceVendor (CMCapture in mediaserverd) - media capture stack vendor
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <sys/sysctl.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>

#define SWEET_TORCH_FULL_MA     150
#define SWEET_TORCH_MIN_MA      15

static void
set_torch_ma(int ma)
{
	if (ma < 0) ma = 0;
	if (ma > 200) ma = 200;
	int err = sysctlbyname("kern.sweet_torch", NULL, NULL, &ma, sizeof(ma));
	if (err != 0) {
		syslog(LOG_ERR, "SweetTorch: kern.sweet_torch=%d failed: %d", ma, err);
	} else {
		syslog(LOG_NOTICE, "SweetTorch: set torch to %d mA", ma);
	}
}

/* -------------------------------------------------------------------------- */
/* SBUIFlashlightController Hooks (SpringBoard)                              */
/* -------------------------------------------------------------------------- */

static void (*orig_sb_turnOn)(id, SEL, id) = NULL;
static void (*orig_sb_turnOff)(id, SEL, id) = NULL;
static void (*orig_sb_powerOn)(id, SEL) = NULL;
static void (*orig_sb_powerOff)(id, SEL) = NULL;
static void (*orig_sb_setLevel)(id, SEL, unsigned long long) = NULL;
static void (*orig_sb_setFlashlightLevel)(id, SEL, unsigned long long) = NULL;

static void
hook_sb_turnOn(id self, SEL sel, id reason)
{
	syslog(LOG_NOTICE, "SweetTorch: SBUIFlashlightController turnFlashlightOnForReason");
	set_torch_ma(SWEET_TORCH_FULL_MA);
	if (orig_sb_turnOn) orig_sb_turnOn(self, sel, reason);
}

static void
hook_sb_turnOff(id self, SEL sel, id reason)
{
	syslog(LOG_NOTICE, "SweetTorch: SBUIFlashlightController turnFlashlightOffForReason");
	set_torch_ma(0);
	if (orig_sb_turnOff) orig_sb_turnOff(self, sel, reason);
}

static void
hook_sb_powerOn(id self, SEL sel)
{
	syslog(LOG_NOTICE, "SweetTorch: SBUIFlashlightController _turnPowerOn");
	set_torch_ma(SWEET_TORCH_FULL_MA);
	if (orig_sb_powerOn) orig_sb_powerOn(self, sel);
}

static void
hook_sb_powerOff(id self, SEL sel)
{
	syslog(LOG_NOTICE, "SweetTorch: SBUIFlashlightController _turnPowerOff");
	set_torch_ma(0);
	if (orig_sb_powerOff) orig_sb_powerOff(self, sel);
}

static void
hook_sb_setLevel(id self, SEL sel, unsigned long long level)
{
	syslog(LOG_NOTICE, "SweetTorch: SBUIFlashlightController setLevel: %llu", level);
	int ma = 0;
	if (level > 0) {
		ma = (int)(level * 35 + SWEET_TORCH_MIN_MA);
		if (ma > SWEET_TORCH_FULL_MA) ma = SWEET_TORCH_FULL_MA;
	}
	set_torch_ma(ma);
	if (orig_sb_setLevel) orig_sb_setLevel(self, sel, level);
}

static void
hook_sb_setFlashlightLevel(id self, SEL sel, unsigned long long level)
{
	syslog(LOG_NOTICE, "SweetTorch: SBUIFlashlightController _setFlashlightLevel: %llu", level);
	int ma = 0;
	if (level > 0) {
		ma = (int)(level * 35 + SWEET_TORCH_MIN_MA);
		if (ma > SWEET_TORCH_FULL_MA) ma = SWEET_TORCH_FULL_MA;
	}
	set_torch_ma(ma);
	if (orig_sb_setFlashlightLevel) orig_sb_setFlashlightLevel(self, sel, level);
}

static void
swizzle(Class cls, const char *sel_name, IMP hook_imp, IMP *orig_imp)
{
	SEL s = sel_registerName(sel_name);
	Method m = class_getInstanceMethod(cls, s);
	if (m != NULL) {
		if (orig_imp) *orig_imp = method_getImplementation(m);
		method_setImplementation(m, hook_imp);
		syslog(LOG_NOTICE, "SweetTorch: swizzled -[%s %s]", class_getName(cls), sel_name);
	} else {
		syslog(LOG_NOTICE, "SweetTorch: no -[%s %s]", class_getName(cls), sel_name);
	}
}

static void
init_springboard_ui(void)
{
	dlopen("/System/Library/PrivateFrameworks/SpringBoardUI.framework/SpringBoardUI", RTLD_NOW);
	Class cls = objc_getClass("SBUIFlashlightController");
	if (cls != NULL) {
		swizzle(cls, "turnFlashlightOnForReason:", (IMP)hook_sb_turnOn, (IMP *)&orig_sb_turnOn);
		swizzle(cls, "turnFlashlightOffForReason:", (IMP)hook_sb_turnOff, (IMP *)&orig_sb_turnOff);
		swizzle(cls, "_turnPowerOn", (IMP)hook_sb_powerOn, (IMP *)&orig_sb_powerOn);
		swizzle(cls, "_turnPowerOff", (IMP)hook_sb_powerOff, (IMP *)&orig_sb_powerOff);
		swizzle(cls, "setLevel:", (IMP)hook_sb_setLevel, (IMP *)&orig_sb_setLevel);
		swizzle(cls, "_setFlashlightLevel:", (IMP)hook_sb_setFlashlightLevel, (IMP *)&orig_sb_setFlashlightLevel);
	} else {
		syslog(LOG_ERR, "SweetTorch: no SBUIFlashlightController class");
	}
}

/* -------------------------------------------------------------------------- */
/* AVFlashlight Hooks                                                        */
/* -------------------------------------------------------------------------- */

static float av_torch_level;
static bool av_torch_power;

static void
av_torch_apply(void)
{
	int ma = 0;
	if (av_torch_power && av_torch_level > 0.0f) {
		ma = (int)(av_torch_level * SWEET_TORCH_FULL_MA + 0.5f);
		if (ma < SWEET_TORCH_MIN_MA) ma = SWEET_TORCH_MIN_MA;
	}
	set_torch_ma(ma);
}

static void
kvo(id self, const char *what, CFStringRef key)
{
	((void (*)(id, SEL, CFStringRef))objc_msgSend)(self, sel_registerName(what), key);
}

static BOOL ret_yes(id self, SEL sel) { return YES; }
static BOOL ret_no(id self, SEL sel) { return NO; }
static void ret_void(id self, SEL sel) {}

static float
av_level_get(id self, SEL sel)
{
	return av_torch_power ? av_torch_level : 0.0f;
}

static BOOL
av_level_set(id self, SEL sel, float level, CFErrorRef *error)
{
	if (error) *error = NULL;
	level = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
	kvo(self, "willChangeValueForKey:", CFSTR("flashlightLevel"));
	av_torch_level = level;
	av_torch_power = level > 0.0f || av_torch_power;
	av_torch_apply();
	kvo(self, "didChangeValueForKey:", CFSTR("flashlightLevel"));
	return YES;
}

static BOOL
av_power_on(id self, SEL sel, CFErrorRef *error)
{
	if (error) *error = NULL;
	av_torch_power = true;
	av_torch_apply();
	return YES;
}

static void
av_power_off(id self, SEL sel)
{
	kvo(self, "willChangeValueForKey:", CFSTR("flashlightLevel"));
	av_torch_power = false;
	av_torch_level = 0.0f;
	av_torch_apply();
	kvo(self, "didChangeValueForKey:", CFSTR("flashlightLevel"));
}

static void
init_avflashlight(void)
{
	dlopen("/System/Library/Frameworks/AVFoundation.framework/AVFoundation", RTLD_NOW);
	Class cls = objc_getClass("AVFlashlight");
	if (cls != NULL) {
		Method m = class_getClassMethod(cls, sel_registerName("hasFlashlight"));
		if (m) method_setImplementation(m, (IMP)ret_yes);

		swizzle(cls, "isAvailable", (IMP)ret_yes, NULL);
		swizzle(cls, "isOverheated", (IMP)ret_no, NULL);
		swizzle(cls, "flashlightLevel", (IMP)av_level_get, NULL);
		swizzle(cls, "setFlashlightLevel:withError:", (IMP)av_level_set, NULL);
		swizzle(cls, "turnPowerOnWithError:", (IMP)av_power_on, NULL);
		swizzle(cls, "turnPowerOff", (IMP)av_power_off, NULL);
		swizzle(cls, "_setupFlashlight", (IMP)ret_void, NULL);
		swizzle(cls, "_teardownFlashlight", (IMP)ret_void, NULL);
		swizzle(cls, "_reconnectToServer", (IMP)ret_void, NULL);
		syslog(LOG_NOTICE, "SweetTorch: hooked AVFlashlight in %s", getprogname());
	}
}

/* -------------------------------------------------------------------------- */
/* mediaserverd (BWFigCaptureDeviceVendor) Hooks                             */
/* -------------------------------------------------------------------------- */

static Class vendor_class, device_class, stream_class;
static IMP original_shared_vendor = NULL;

static int flashlight_register(id self, SEL sel, ...) { return 1; }
static id flashlight_copy_device(id self, SEL sel, int client, BOOL inform, int *error)
{
	if (error) *error = 0;
	return class_createInstance(device_class, 0);
}
static id flashlight_copy_stream(id self, SEL sel, int position, int type, id device)
{
	return (device != NULL && object_getClass(device) == device_class) ? class_createInstance(stream_class, 0) : NULL;
}
static void flashlight_take_back(id self, SEL sel, id device, int client, BOOL inform)
{
	set_torch_ma(0);
}
static BOOL flashlight_active_equals(id self, SEL sel, id device)
{
	return (device == NULL || object_getClass(device) == device_class);
}
static id flashlight_stream_properties(id self, SEL sel)
{
	static CFDictionaryRef none = NULL;
	if (!none) none = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	return (id)none;
}
static int flashlight_stream_set(id self, SEL sel, id key, id value)
{
	if (key != NULL && CFEqual((CFTypeRef)key, CFSTR("TorchLevel"))) {
		float level = 0.0f;
		if (value && CFGetTypeID((CFTypeRef)value) == CFNumberGetTypeID()) {
			CFNumberGetValue((CFNumberRef)value, kCFNumberFloatType, &level);
		}
		level = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
		int ma = 0;
		if (level > 0.0f) {
			ma = (int)(level * SWEET_TORCH_FULL_MA + 0.5f);
			if (ma < SWEET_TORCH_MIN_MA) ma = SWEET_TORCH_MIN_MA;
		}
		set_torch_ma(ma);
	}
	return 0;
}

static id mock_shared_vendor(id self, SEL sel)
{
	id vendor = NULL;
	if (original_shared_vendor) vendor = ((id (*)(id, SEL))original_shared_vendor)(self, sel);
	if (vendor) return vendor;
	static id ours = NULL;
	if (!ours) {
		ours = class_createInstance(vendor_class, 0);
		syslog(LOG_NOTICE, "SweetTorch: mediaserverd: created SweetTorchVendor for FigFlashlight");
	}
	return ours;
}

static void
init_mediaserverd(void)
{
	dlopen("/System/Library/PrivateFrameworks/CMCapture.framework/CMCapture", RTLD_NOW);
	Class cls = objc_getClass("BWFigCaptureDeviceVendor");
	if (cls == NULL) {
		syslog(LOG_ERR, "SweetTorch: mediaserverd: no BWFigCaptureDeviceVendor");
		return;
	}

	static const struct { const char *name, *types; IMP imp; } vmethods[] = {
		{ "registerClientWithPID:clientApplicationID:clientDescription:clientPriority:canStealFromClientsWithSamePriority:deviceSharingWithOtherClientsAllowed:deviceAvailabilityChangedHandler:", "i@:i@@iBB@?", (IMP)flashlight_register },
		{ "registerClientWithPID:clientDescription:clientPriority:canStealFromClientsWithSamePriority:deviceSharingWithOtherClientsAllowed:deviceAvailabilityChangedHandler:", "i@:i@iBB@?", (IMP)flashlight_register },
		{ "copyDeviceForClient:informClientWhenDeviceAvailableAgain:error:", "@@:iB^i", (IMP)flashlight_copy_device },
		{ "copyStreamForFlashlightWithPosition:deviceType:forDevice:", "@@:ii@", (IMP)flashlight_copy_stream },
		{ "takeBackDevice:forClient:informClientWhenDeviceAvailableAgain:", "v@:@iB", (IMP)flashlight_take_back },
		{ "activeDeviceEquals:", "B@:@", (IMP)flashlight_active_equals },
	};
	static const struct { const char *name, *types; IMP imp; } smethods[] = {
		{ "supportedProperties", "@@:", (IMP)flashlight_stream_properties },
		{ "setProperty:value:", "i@:@@", (IMP)flashlight_stream_set },
	};

	vendor_class = objc_allocateClassPair(objc_getClass("NSObject"), "SweetTorchVendor", 0);
	for (size_t i = 0; i < sizeof(vmethods) / sizeof(vmethods[0]); i++) {
		class_addMethod(vendor_class, sel_registerName(vmethods[i].name), vmethods[i].imp, vmethods[i].types);
	}
	objc_registerClassPair(vendor_class);

	device_class = objc_allocateClassPair(objc_getClass("NSObject"), "SweetTorchDevice", 0);
	objc_registerClassPair(device_class);

	stream_class = objc_allocateClassPair(objc_getClass("NSObject"), "SweetTorchStream", 0);
	for (size_t i = 0; i < sizeof(smethods) / sizeof(smethods[0]); i++) {
		class_addMethod(stream_class, sel_registerName(smethods[i].name), smethods[i].imp, smethods[i].types);
	}
	objc_registerClassPair(stream_class);

	Method m = class_getClassMethod(cls, sel_registerName("sharedCaptureDeviceVendor"));
	if (m != NULL) {
		original_shared_vendor = method_getImplementation(m);
		method_setImplementation(m, (IMP)mock_shared_vendor);
		syslog(LOG_NOTICE, "SweetTorch: mediaserverd: hooked sharedCaptureDeviceVendor");
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
	if (prog && strcmp(prog, "mediaserverd") == 0) {
		init_mediaserverd();
	} else {
		init_springboard_ui();
		init_avflashlight();
	}
}
