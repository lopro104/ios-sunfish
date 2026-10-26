/*
 * sweet-lsq: what LaunchServices knows about application extensions (plugins), asked from inside.
 * PosterBoard showed no wallpaper and an empty gallery: it starts no poster extension at all, and
 * the LaunchServices database names none (grep of the csstore).
 *
 *   sweet-lsq                      every plugin LaunchServices has: count, then id + extension point
 *   sweet-lsq POINT                only those of extension point POINT (com.apple.posterkit.provider)
 *   sweet-lsq --register PATH...   register the bundles at PATH (an .app, or an .appex) with lsd
 *
 * LSApplicationWorkspace and LSPlugInKitProxy are private (MobileCoreServices); they are reached
 * through the Objective-C runtime.
 */
#import <Foundation/Foundation.h>
#import <objc/message.h>

static id
workspace(void)
{
	Class c = NSClassFromString(@"LSApplicationWorkspace");
	return c ? ((id (*)(Class, SEL))objc_msgSend)(c, NSSelectorFromString(@"defaultWorkspace")) : nil;
}

static NSString *
str(id object, NSString *selector)
{
	SEL s = NSSelectorFromString(selector);
	if (![object respondsToSelector:s]) {
		return @"?";
	}
	id v = ((id (*)(id, SEL))objc_msgSend)(object, s);
	return v ? [v description] : @"-";
}

int
main(int argc, char *argv[])
{
	@autoreleasepool {
		id ws = workspace();
		if (ws == nil) {
			fprintf(stderr, "sweet-lsq: no LSApplicationWorkspace\n");
			return 1;
		}
		if (argc > 2 && strcmp(argv[1], "--register") == 0) {
			for (int i = 2; i < argc; i++) {
				NSURL *url = [NSURL fileURLWithPath:@(argv[i])];
				BOOL ok = ((BOOL (*)(id, SEL, id))objc_msgSend)(ws, NSSelectorFromString(@"registerPlugin:"), url);
				printf("registerPlugin %s: %s\n", argv[i], ok ? "yes" : "no");
			}
			return 0;
		}
		NSArray *plugins = ((id (*)(id, SEL))objc_msgSend)(ws, NSSelectorFromString(@"installedPlugins"));
		NSString *point = argc > 1 ? @(argv[1]) : nil;
		unsigned shown = 0;
		for (id p in plugins) {
			NSString *ep = str(p, @"protocol");
			if (point != nil && ![ep isEqualToString:point]) {
				continue;
			}
			shown++;
			printf("%s  %s  %s\n", str(p, @"pluginIdentifier").UTF8String, ep.UTF8String,
			    str(p, @"bundleURL").UTF8String);
		}
		printf("sweet-lsq: %lu plugins known, %u shown%s%s\n", (unsigned long)plugins.count, shown,
		    point ? " for " : "", point ? point.UTF8String : "");
	}
	return 0;
}
