/*
 * SweetTorch.dylib: the Control Center flashlight button on the Pixel 4a (ElleKit/TweakInject,
 * filter SweetTorch.plist: SpringBoard).
 *
 * The button only shows when MobileGestalt's camera-flash is set (ADT /product/camera "flash",
 * tools/sweet-adt-flash.py). It then works an AVFlashlight, which asks mediaserverd's FigFlashlight,
 * which asks a camera this phone does not have: nothing lights. So AVFlashlight's methods are
 * replaced here by ones that set the kernel's torch (xnu 0151/0157 sysctl kern.sweet_torch,
 * milliamps per LED) directly. The method names are iOS 16.7's (userland/torch/sweet-objc-methods):
 *
 *   +hasFlashlight, -isAvailable                   YES
 *   -isOverheated                                  NO (the kernel caps the current)
 *   -flashlightLevel                               what was set, 0 to 1
 *   -setFlashlightLevel:withError:                 the level, as SWEET_TORCH_FULL_MA * level
 *   -turnPowerOnWithError:, -turnPowerOff          power; off is 0 mA
 *   -_setupFlashlight, -_teardownFlashlight,       nothing (no connection to mediaserverd)
 *   -_reconnectToServer
 *
 * Changes of the level are announced with KVO ("flashlightLevel"), as Control Center observes.
 * (HackiOS does this inside mediaserverd, at the code addresses of iOS 16.5; by name here instead.)
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <sys/sysctl.h>
#include <syslog.h>
#include <unistd.h>

#define SWEET_TORCH_FULL_MA     150     /* per LED at level 1 (Android's torch dt: 300, hardware: 500) */
#define SWEET_TORCH_MIN_MA      13

static float torch_level;
static bool torch_power;

static void
torch_apply(void)
{
	int ma = 0;

	if (torch_power && torch_level > 0) {
		ma = (int)(torch_level * SWEET_TORCH_FULL_MA + 0.5f);
		if (ma < SWEET_TORCH_MIN_MA) {
			ma = SWEET_TORCH_MIN_MA;
		}
	}
	if (sysctlbyname("kern.sweet_torch", NULL, NULL, &ma, sizeof(ma)) != 0) {
		syslog(LOG_ERR, "SweetTorch: kern.sweet_torch=%d failed", ma);
	}
}

static void
kvo(id self, const char *what, CFStringRef key)
{
	((void (*)(id, SEL, CFStringRef))objc_msgSend)(self, sel_registerName(what), key);
}

static BOOL
yes(id self, SEL sel)
{
	return YES;
}

static BOOL
no(id self, SEL sel)
{
	return NO;
}

static void
nothing(id self, SEL sel)
{
}

static float
level_get(id self, SEL sel)
{
	return torch_power ? torch_level : 0;
}

static BOOL
level_set(id self, SEL sel, float level, CFErrorRef *error)
{
	if (error != NULL) {
		*error = NULL;
	}
	level = level < 0 ? 0 : level > 1 ? 1 : level;
	kvo(self, "willChangeValueForKey:", CFSTR("flashlightLevel"));
	torch_level = level;
	torch_power = level > 0 || torch_power;
	torch_apply();
	kvo(self, "didChangeValueForKey:", CFSTR("flashlightLevel"));
	return YES;
}

static BOOL
power_on(id self, SEL sel, CFErrorRef *error)
{
	if (error != NULL) {
		*error = NULL;
	}
	torch_power = true;
	torch_apply();
	return YES;
}

static void
power_off(id self, SEL sel)
{
	kvo(self, "willChangeValueForKey:", CFSTR("flashlightLevel"));
	torch_power = false;
	torch_level = 0;
	torch_apply();
	kvo(self, "didChangeValueForKey:", CFSTR("flashlightLevel"));
}

static void
replace(Class cls, const char *selector, IMP imp)
{
	Method m = class_getInstanceMethod(cls, sel_registerName(selector));

	if (m == NULL) {
		syslog(LOG_ERR, "SweetTorch: no %s on %s; left alone", selector, class_getName(cls));
		return;
	}
	method_setImplementation(m, imp);
}

__attribute__((constructor))
static void
sweet_torch_init(void)
{
	dlopen("/System/Library/Frameworks/AVFoundation.framework/AVFoundation", RTLD_NOW);
	Class cls = objc_getClass("AVFlashlight");
	if (cls == NULL) {
		syslog(LOG_ERR, "SweetTorch: no AVFlashlight");
		return;
	}
	replace(object_getClass((id)cls), "hasFlashlight", (IMP)yes);
	replace(cls, "isAvailable", (IMP)yes);
	replace(cls, "isOverheated", (IMP)no);
	replace(cls, "flashlightLevel", (IMP)level_get);
	replace(cls, "setFlashlightLevel:withError:", (IMP)level_set);
	replace(cls, "turnPowerOnWithError:", (IMP)power_on);
	replace(cls, "turnPowerOff", (IMP)power_off);
	replace(cls, "_setupFlashlight", (IMP)nothing);
	replace(cls, "_teardownFlashlight", (IMP)nothing);
	replace(cls, "_reconnectToServer", (IMP)nothing);
	syslog(LOG_NOTICE, "SweetTorch: AVFlashlight drives kern.sweet_torch in %s", getprogname());
}
