/*
 * mtldisc: does Metal's own discovery (MTLCreateSystemDefaultDevice) find the Adreno through
 * SweetMetal.bundle (boot-arg sweetmetal=1, the process listed in /var/jb/etc/sweetmetal.clients)?
 * Then a blit fill of 1 MB on the GPU, read back by the CPU. Exit 0 only if every byte is right.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>

int
main(void)
{
	@autoreleasepool {
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		printf("MTLCreateSystemDefaultDevice: %s\n", device ? device.name.UTF8String : "nil");
		if (device == nil) {
			return 1;
		}
		const NSUInteger bytes = 1 << 20;
		id<MTLBuffer> buffer = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
		memset(buffer.contents, 0, bytes);
		id<MTLCommandQueue> queue = [device newCommandQueue];
		id<MTLCommandBuffer> cb = [queue commandBuffer];
		id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
		[blit fillBuffer:buffer range:NSMakeRange(0, bytes) value:0xa5];
		[blit endEncoding];
		[cb commit];
		[cb waitUntilCompleted];
		const uint8_t *p = (const uint8_t *)buffer.contents;
		NSUInteger bad = 0;
		for (NSUInteger i = 0; i < bytes; i++) {
			bad += p[i] != 0xa5;
		}
		printf("blit fill: status %ld, %lu of %lu bytes wrong\n", (long)cb.status, (unsigned long)bad, (unsigned long)bytes);
		return bad == 0 && cb.status == MTLCommandBufferStatusCompleted ? 0 : 1;
	}
}
