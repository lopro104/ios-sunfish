/*
 * SweetPageTest.dylib: a repeatable load for measuring the UI, with nobody at the phone (ElleKit, filter: SpringBoard).
 * On the Darwin notification com.sweet.pagetest.start SpringBoard flips between home-screen pages 0 and 1 every 0.7 s,
 * 30 times, with its own page animation; com.sweet.pagetest.stop ends it early. Does nothing otherwise.
 *   /var/jb/usr/bin/notifyutil -p com.sweet.pagetest.start
 * No hooks. A measuring tool: take it off the phone when done.
 */
#import <Foundation/Foundation.h>
#include <notify.h>
#include <objc/message.h>
#include <syslog.h>

static int left, page;

static id
root_folder(void)
{
	Class c = NSClassFromString(@"SBIconController");
	id controller = [c respondsToSelector:@selector(sharedInstance)] ? [c performSelector:@selector(sharedInstance)] : nil;
	id manager = [controller respondsToSelector:@selector(iconManager)] ? [controller performSelector:@selector(iconManager)] : nil;
	return [manager respondsToSelector:@selector(rootFolderController)] ? [manager performSelector:@selector(rootFolderController)] : nil;
}

static void
flip(void)
{
	if (left-- <= 0) {
		return;
	}
	id folder = root_folder();
	SEL set = NSSelectorFromString(@"setCurrentPageIndex:animated:");
	if (![folder respondsToSelector:set]) {
		syslog(LOG_ERR, "SweetPageTest: no root folder controller (%s)", folder ? object_getClassName(folder) : "nil");
		return;
	}
	page = !page;
	((void (*)(id, SEL, long, BOOL))objc_msgSend)(folder, set, page, YES);
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 700 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
		flip();
	});
}

__attribute__((constructor)) static void
init(void)
{
	int token;

	notify_register_dispatch("com.sweet.pagetest.start", &token, dispatch_get_main_queue(), ^(int t) {
		syslog(LOG_ERR, "SweetPageTest: start");
		left = 30;
		flip();
	});
	notify_register_dispatch("com.sweet.pagetest.stop", &token, dispatch_get_main_queue(), ^(int t) {
		left = 0;
	});
}
