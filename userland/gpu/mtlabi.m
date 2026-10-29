/*
 * mtlabi: what this iOS's Metal looks like to a plugin device. UniXNU's AdrenoMetal (made for
 * iPadOS 17.7.11) subclasses the private _MTLDevice and refuses unless its instance size is 536,
 * and declares the private protocols MTLDeviceSPI and MTLCommandQueueSPI. This prints those facts
 * for the running iOS, the instance variables of _MTLDevice, and what MTLCreateSystemDefaultDevice
 * returns (no GPU work).
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <objc/runtime.h>
#include <stdio.h>

int
main(void)
{
	@autoreleasepool {
		Class base = objc_getClass("_MTLDevice");
		printf("_MTLDevice: %s, instance size %zu (UniXNU expects 536)\n", base ? "present" : "MISSING",
		    base ? class_getInstanceSize(base) : 0);
		if (base) {
			unsigned count = 0;
			Ivar *ivars = class_copyIvarList(base, &count);
			for (unsigned i = 0; i < count; i++) {
				printf("  ivar %-40s offset %4td  %s\n", ivar_getName(ivars[i]), ivar_getOffset(ivars[i]),
				    ivar_getTypeEncoding(ivars[i]));
			}
			free(ivars);
			printf("  superclass %s\n", class_getName(class_getSuperclass(base)));
		}
		const char *protocols[] = {"MTLDevice", "MTLDeviceSPI", "MTLCommandQueueSPI", "MTLCommandQueue"};
		for (unsigned i = 0; i < 4; i++) {
			printf("protocol %-20s %s\n", protocols[i], objc_getProtocol(protocols[i]) ? "present" : "MISSING");
		}
		const char *classes[] = {"_MTLCommandQueue", "_MTLCommandBuffer", "MTLIOAccelDevice", "MTLCompilerConnection"};
		for (unsigned i = 0; i < 4; i++) {
			Class c = objc_getClass(classes[i]);
			printf("class %-24s %s %zu\n", classes[i], c ? "present" : "MISSING", c ? class_getInstanceSize(c) : 0);
		}
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		printf("MTLCreateSystemDefaultDevice: %s\n", device ? device.name.UTF8String : "nil");
	}
	return 0;
}
