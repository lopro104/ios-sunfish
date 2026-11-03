/*
 * SweetAnimRate.dylib: the frame rates animations ask for, up to the panel's 60 (ElleKit, filter: SpringBoard, Preferences).
 * The idea and the measurements are HakiOS's (hakios-xnu-kernel 7caa4576, 3639b98a, inject_sb.c).
 *
 * 1. UIKit's UIViewInProcessAnimationManager, which runs the animations of the process itself (a scroll view's page
 *    settling among them), asks its CADisplayLink for 20..30. A power saving on a ProMotion iPhone; here it made the end of
 *    a page swipe 30 a second. The rates asked of those display links are dropped; others keep theirs (the clock's 10, the
 *    motion effects' 32: they would only wake SpringBoard more).
 * 2. Through Metal the window server keeps to the range each animation asks for, and one asking for nothing goes at 24
 *    a second. Every animation added to a layer asks for 48..60 here: on a copy, since one already added is read-only and
 *    setting it aborted SpringBoard (HakiOS).
 *
 * Objective-C method replacements only.
 */
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <string.h>

static id (*original_link_with)(id, SEL, id, SEL);
static void (*original_set_fps)(id, SEL, NSInteger);
static void (*original_set_range)(id, SEL, CAFrameRateRange);
static void (*original_add)(id, SEL, CAAnimation *, NSString *);
static char full_rate;

static id
link_with(id self, SEL cmd, id target, SEL action)
{
	id link = original_link_with(self, cmd, target, action);
	if (link != nil && target != nil && strcmp(object_getClassName(target), "UIViewInProcessAnimationManager") == 0) {
		objc_setAssociatedObject(link, &full_rate, (id)kCFBooleanTrue, OBJC_ASSOCIATION_ASSIGN);
	}
	return link;
}

static void
set_fps(id self, SEL cmd, NSInteger fps)
{
	if (objc_getAssociatedObject(self, &full_rate) == nil) {
		original_set_fps(self, cmd, fps);
	}
}

static void
set_range(id self, SEL cmd, CAFrameRateRange range)
{
	if (objc_getAssociatedObject(self, &full_rate) == nil) {
		original_set_range(self, cmd, range);
	}
}

static void
add_animation(id self, SEL cmd, CAAnimation *animation, NSString *key)
{
	if (animation != nil && [animation respondsToSelector:@selector(setPreferredFrameRateRange:)]) {
		CAAnimation *copy = [animation copy];
		copy.preferredFrameRateRange = CAFrameRateRangeMake(48, 60, 60);
		original_add(self, cmd, copy, key);
		return;
	}
	original_add(self, cmd, animation, key);
}

__attribute__((constructor)) static void
sweet_anim_rate(void)
{
	Class link = objc_getClass("CADisplayLink"), layer = objc_getClass("CALayer");
	Method m;

	if (link != Nil) {
		m = class_getClassMethod(link, @selector(displayLinkWithTarget:selector:));
		if (m != NULL) {
			original_link_with = (void *)method_setImplementation(m, (IMP)link_with);
		}
		m = class_getInstanceMethod(link, @selector(setPreferredFramesPerSecond:));
		if (m != NULL) {
			original_set_fps = (void *)method_setImplementation(m, (IMP)set_fps);
		}
		m = class_getInstanceMethod(link, @selector(setPreferredFrameRateRange:));
		if (m != NULL) {
			original_set_range = (void *)method_setImplementation(m, (IMP)set_range);
		}
	}
	if (layer != Nil) {
		m = class_getInstanceMethod(layer, @selector(addAnimation:forKey:));
		if (m != NULL) {
			original_add = (void *)method_setImplementation(m, (IMP)add_animation);
		}
	}
}
