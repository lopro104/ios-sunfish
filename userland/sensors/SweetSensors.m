/*
 * SweetSensors.dylib: Sensors & UI polishing inside SpringBoard (ElleKit tweak).
 *
 * Provides:
 *  1. Dynamic Display Orientation & Auto-Rotation:
 *     - "com.sweet.rotate.portrait" (1)
 *     - "com.sweet.rotate.landscape" (3)
 *     - "com.sweet.rotate.landscape_left" (4)
 *     - "com.sweet.rotate.upside_down" (2)
 *     - Unlocks Home Screen rotation (SBUIController / SBOrientationLockManager)
 *  2. Ambient Light / Hardware Brightness sync:
 *     - Maps backlight changes to physical hardware brightness via kern.sweet_brightness (DCS 0x51 over DSI0).
 *  3. Proximity Detection / Screen Sleep:
 *     - "com.sweet.prox.near": pocket/ear detection, blanks display cleanly.
 *     - "com.sweet.prox.far": clears pocket/ear mode, restores display and brightness.
 *  4. Remote wake & unlock:
 *     - "com.sweet.wake": unblanks display
 *     - "com.sweet.unlock": unblanks and unlocks SpringBoard
 */

#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>
#include <notify.h>
#include <syslog.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <sys/sysctl.h>

typedef NSUInteger UIInterfaceOrientationMask;

static void (*bks_set_blanked)(BOOL) = NULL;
static void (*bks_will_unblank)(void) = NULL;

static void
log_msg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    syslog(LOG_NOTICE, "%s", buf);
    FILE *f = fopen("/private/var/tmp/sweetsensors.log", "a");
    if (f) {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

static void
set_hw_brightness(float factor)
{
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;

    // Map 0.0 .. 1.0 to 20 .. 1800 (safe hardware PWM/DCS range)
    int hw_val = (int)(factor * 1780.0f + 20.0f);
    int current = 0;
    size_t sz = sizeof(current);
    if (sysctlbyname("kern.sweet_brightness", &current, &sz, &hw_val, sizeof(hw_val)) == 0) {
        log_msg("SweetSensors: synced brightness factor %.2f -> kern.sweet_brightness=%d", factor, hw_val);
    }
}

static void
rotate_to_orientation(NSInteger target)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        log_msg("SweetSensors: rotating to orientation %ld", (long)target);

        // 1. Update UIDevice orientation
        Class uiDeviceClass = objc_getClass("UIDevice");
        if (uiDeviceClass) {
            id currentDevice = ((id (*)(id, SEL))objc_msgSend)((id)uiDeviceClass, sel_registerName("currentDevice"));
            if ([currentDevice respondsToSelector:sel_registerName("setOrientation:animated:")]) {
                log_msg("SweetSensors: calling UIDevice setOrientation:animated:");
                ((void (*)(id, SEL, NSInteger, BOOL))objc_msgSend)(currentDevice, sel_registerName("setOrientation:animated:"), target, YES);
            }
        }

        // 2. iOS 16 Geometry Request on UIWindowScene
        Class geomClass = objc_getClass("UIWindowSceneGeometryPreferencesIOS");
        Class uiAppClass = objc_getClass("UIApplication");
        if (uiAppClass) {
            id app = ((id (*)(id, SEL))objc_msgSend)((id)uiAppClass, sel_registerName("sharedApplication"));
            if ([app respondsToSelector:sel_registerName("connectedScenes")]) {
                NSSet *scenes = ((id (*)(id, SEL))objc_msgSend)(app, sel_registerName("connectedScenes"));
                for (id scene in scenes) {
                    if (geomClass && [scene respondsToSelector:sel_registerName("requestGeometryUpdateWithPreferences:errorHandler:")]) {
                        UIInterfaceOrientationMask mask = (1 << target);
                        id geom = ((id (*)(id, SEL, NSUInteger))objc_msgSend)([geomClass alloc], sel_registerName("initWithInterfaceOrientations:"), mask);
                        log_msg("SweetSensors: calling requestGeometryUpdateWithPreferences on scene %p", scene);
                        ((void (*)(id, SEL, id, id))objc_msgSend)(scene, sel_registerName("requestGeometryUpdateWithPreferences:errorHandler:"), geom, nil);
                    }
                    if ([scene respondsToSelector:sel_registerName("_updateClientSettingsToInterfaceOrientation:withAnimationDuration:")]) {
                        ((void (*)(id, SEL, NSInteger, double))objc_msgSend)(scene, sel_registerName("_updateClientSettingsToInterfaceOrientation:withAnimationDuration:"), target, 0.3);
                    }
                }
            }
        }

        // 3. Update SpringBoard UIController
        Class sbuicCls = objc_getClass("SBUIController");
        if (sbuicCls && [sbuicCls respondsToSelector:sel_registerName("sharedInstance")]) {
            id sbuic = ((id (*)(id, SEL))objc_msgSend)((id)sbuicCls, sel_registerName("sharedInstance"));
            if ([sbuic respondsToSelector:sel_registerName("setInterfaceOrientation:")]) {
                log_msg("SweetSensors: calling SBUIController setInterfaceOrientation:");
                ((void (*)(id, SEL, NSInteger))objc_msgSend)(sbuic, sel_registerName("setInterfaceOrientation:"), target);
            }
        }
    });
}

static void
handle_proximity(int state)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        log_msg("SweetSensors: proximity changed -> %d (%s)", state, state ? "NEAR" : "FAR");
        if (state != 0) {
            // Object in proximity (pocket or ear): blank display
            if (bks_set_blanked) bks_set_blanked(YES);
            int zero = 0;
            (void)sysctlbyname("kern.sweet_brightness", NULL, NULL, &zero, sizeof(zero));
        } else {
            // Object clear: wake display and restore brightness
            if (bks_will_unblank) bks_will_unblank();
            if (bks_set_blanked) bks_set_blanked(NO);
            int normal = 500;
            (void)sysctlbyname("kern.sweet_brightness", NULL, NULL, &normal, sizeof(normal));
        }
    });
}

static void
unlock_device(void)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        log_msg("SweetSensors: unlock requested");
        if (bks_will_unblank) bks_will_unblank();
        if (bks_set_blanked) bks_set_blanked(NO);

        Class lsmCls = objc_getClass("SBLockScreenManager");
        if (lsmCls && [lsmCls respondsToSelector:sel_registerName("sharedInstance")]) {
            id lsm = ((id (*)(id, SEL))objc_msgSend)((id)lsmCls, sel_registerName("sharedInstance"));
            if ([lsm respondsToSelector:sel_registerName("unlockUIFromSource:withOptions:")]) {
                BOOL ok = ((BOOL (*)(id, SEL, long, id))objc_msgSend)(lsm, sel_registerName("unlockUIFromSource:withOptions:"), 24, nil);
                log_msg("SweetSensors: unlockUIFromSource:24 returned %d", ok);
            }
        }
    });
}

// Hooking backlight factor changes
static void (*original_set_backlight_factor)(id self, SEL cmd, float factor, id reason);

static void
custom_set_backlight_factor(id self, SEL cmd, float factor, id reason)
{
    set_hw_brightness(factor);
    if (original_set_backlight_factor) {
        original_set_backlight_factor(self, cmd, factor, reason);
    }
}

// Hooking orientation lock to allow auto-rotation
static BOOL
fake_is_locked(__unused id self, __unused SEL cmd)
{
    return NO;
}

static NSInteger
fake_home_screen_rotation_style(__unused id self, __unused SEL cmd)
{
    return 2; // Allow rotation
}

// Allow any UIViewController to autorotate freely
static BOOL
fake_should_autorotate(__unused id self, __unused SEL cmd)
{
    return YES;
}

static NSUInteger
fake_supported_orientations(__unused id self, __unused SEL cmd)
{
    return 30; // UIInterfaceOrientationMaskAll
}

__attribute__((constructor)) static void
sweet_sensors_init(void)
{
    const char *prog = getprogname();
    log_msg("SweetSensors: initializing in %s (PID %d)...", prog ? prog : "(null)", getpid());

    // Hook UIViewController to allow rotation everywhere
    Class vcCls = objc_getClass("UIViewController");
    if (vcCls) {
        Method m1 = class_getInstanceMethod(vcCls, sel_registerName("shouldAutorotate"));
        if (m1) method_setImplementation(m1, (IMP)fake_should_autorotate);
        Method m2 = class_getInstanceMethod(vcCls, sel_registerName("supportedInterfaceOrientations"));
        if (m2) method_setImplementation(m2, (IMP)fake_supported_orientations);
    }

    void *bbs = dlopen("/System/Library/PrivateFrameworks/BackBoardServices.framework/BackBoardServices", RTLD_NOW);
    if (bbs) {
        bks_set_blanked = (void (*)(BOOL))dlsym(bbs, "BKSDisplayServicesSetScreenBlanked");
        bks_will_unblank = (void (*)(void))dlsym(bbs, "BKSDisplayServicesWillUnblank");
        log_msg("SweetSensors: loaded BackBoardServices functions: blank=%p unblank=%p", bks_set_blanked, bks_will_unblank);
    }

    // 1. Hook SBBacklightController to sync backlight factor with hardware brightness
    Class blcCls = objc_getClass("SBBacklightController");
    if (blcCls) {
        Method m = class_getInstanceMethod(blcCls, sel_registerName("setBacklightFactor:source:"));
        if (!m) {
            m = class_getInstanceMethod(blcCls, sel_registerName("setBacklightFactor:"));
        }
        if (m) {
            log_msg("SweetSensors: hooked SBBacklightController setBacklightFactor");
            original_set_backlight_factor = (void *)method_setImplementation(m, (IMP)custom_set_backlight_factor);
        }
    }

    // 2. Unlock orientation lock & Home Screen rotation
    Class lockMgrCls = objc_getClass("SBOrientationLockManager");
    if (lockMgrCls) {
        Method m1 = class_getInstanceMethod(lockMgrCls, sel_registerName("isLocked"));
        if (m1) method_setImplementation(m1, (IMP)fake_is_locked);
        Method m2 = class_getInstanceMethod(lockMgrCls, sel_registerName("isUserLocked"));
        if (m2) method_setImplementation(m2, (IMP)fake_is_locked);
    }
    Class sbuicCls = objc_getClass("SBUIController");
    if (sbuicCls) {
        Method mRot = class_getInstanceMethod(sbuicCls, sel_registerName("homeScreenRotationStyle"));
        if (mRot) method_setImplementation(mRot, (IMP)fake_home_screen_rotation_style);
    }

    // 3. Darwin notifications for remote control & rotation
    int token = 0;
    notify_register_dispatch("com.sweet.rotate.portrait", &token, dispatch_get_main_queue(), ^(__unused int t) {
        rotate_to_orientation(1);
    });
    notify_register_dispatch("com.sweet.rotate.upside_down", &token, dispatch_get_main_queue(), ^(__unused int t) {
        rotate_to_orientation(2);
    });
    notify_register_dispatch("com.sweet.rotate.landscape", &token, dispatch_get_main_queue(), ^(__unused int t) {
        rotate_to_orientation(3);
    });
    notify_register_dispatch("com.sweet.rotate.landscape_left", &token, dispatch_get_main_queue(), ^(__unused int t) {
        rotate_to_orientation(4);
    });

    notify_register_dispatch("com.sweet.prox.near", &token, dispatch_get_main_queue(), ^(__unused int t) {
        handle_proximity(1);
    });
    notify_register_dispatch("com.sweet.prox.far", &token, dispatch_get_main_queue(), ^(__unused int t) {
        handle_proximity(0);
    });

    notify_register_dispatch("com.sweet.wake", &token, dispatch_get_main_queue(), ^(__unused int t) {
        if (bks_will_unblank) bks_will_unblank();
        if (bks_set_blanked) bks_set_blanked(NO);
        int normal = 500;
        (void)sysctlbyname("kern.sweet_brightness", NULL, NULL, &normal, sizeof(normal));
    });

    notify_register_dispatch("com.sweet.unlock", &token, dispatch_get_main_queue(), ^(__unused int t) {
        unlock_device();
    });

    log_msg("SweetSensors: registered all notifications successfully");
}
