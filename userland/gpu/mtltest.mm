/*
 * mtltest: Apple's Metal API on the Pixel 4a's Adreno 618, through UniXNU's native Metal frontend
 * (AdrenoMetal NativeBridgeMetalFrontend.mm: a dynamic subclass of _MTLDevice whose objects are the
 * metal2vulkan runtime over Turnip and SweetGPU's NativeAdreno service, xnu 0174/0176).
 *
 *   mtltest [pairs]
 *
 * 1. the device (native_bridge_metal_open, not Metal's discovery: nothing else on the phone sees it);
 * 2. compute through the public API: the runtime's fixture kernel scale_and_offset over 1000 floats;
 * 3. QuartzCore's own shaders (its default.metallib, what the window server draws with): the library,
 *    then render pipeline states for up to `pairs` (default 40) vertex/fragment pairs.
 * Selectors a client sends that the device lacks are printed as "missing:".
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

extern "C" id native_bridge_metal_open(const char *loader, const char *cache, uint64_t registryID,
    void (*missing)(const char *, const char *));
extern "C" id native_bridge_metal_library_with_air(const void *bytes, size_t length);

static void
missing(const char *cls, const char *sel)
{
	printf("missing: [%s %s]\n", cls ? cls : "?", sel ? sel : "?");
}

static uint64_t
now_us(void)
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000;
}

int
main(int argc, char **argv)
{
	unsigned pairs = argc > 1 ? (unsigned)atoi(argv[1]) : 40;
	int failures = 0;

	setenv("TU_DARWIN_SERVICE", "NativeAdrenoGpuPublished", 0);
	setenv("TU_DEBUG", "nocb", 0);
	mkdir("/var/tmp/m2v-mtl", 0755);
	@autoreleasepool {
		uint64_t t = now_us();
		id<MTLDevice> device = native_bridge_metal_open("/var/jb/usr/lib/libvulkan_freedreno.dylib", "/var/tmp/m2v-mtl", 0, missing);
		if (device == nil) {
			printf("device: none\n");
			return 1;
		}
		printf("device: %s (%llu ms)\n", device.name.UTF8String, (now_us() - t) / 1000);

		NSData *air = [NSData dataWithContentsOfFile:@"/var/jb/usr/share/m2v/runtime_examples.air"];
		id<MTLLibrary> fixture = air ? native_bridge_metal_library_with_air(air.bytes, air.length) : nil;
		id<MTLFunction> kernel = [fixture newFunctionWithName:@"scale_and_offset"];
		NSError *error = nil;
		id<MTLComputePipelineState> compute = kernel ? [device newComputePipelineStateWithFunction:kernel error:&error] : nil;
		if (compute == nil) {
			printf("compute pipeline: none (%s)\n", error.description.UTF8String ?: "no fixture");
			failures++;
		} else {
			enum { N = 1000 };
			id<MTLBuffer> in = [device newBufferWithLength:N * 4 options:MTLResourceStorageModeShared];
			id<MTLBuffer> out = [device newBufferWithLength:N * 4 options:MTLResourceStorageModeShared];
			float *src = (float *)in.contents, scale = 2.0f;
			for (int i = 0; i < N; i++) {
				src[i] = (float)i;
			}
			memset(out.contents, 0, N * 4);
			id<MTLCommandQueue> queue = [device newCommandQueue];
			id<MTLCommandBuffer> cb = [queue commandBuffer];
			id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
			[enc setComputePipelineState:compute];
			[enc setBuffer:in offset:0 atIndex:0];
			[enc setBuffer:out offset:0 atIndex:1];
			[enc setBytes:&scale length:4 atIndex:2];
			[enc dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
			[enc endEncoding];
			t = now_us();
			[cb commit];
			[cb waitUntilCompleted];
			const float *dst = (const float *)out.contents;
			int bad = 0;
			for (int i = 0; i < N; i++) {
				bad += dst[i] != 3.0f * i;
			}
			printf("compute: status %ld, %d of %d wrong (%llu us)\n", (long)cb.status, bad, N, now_us() - t);
			failures += bad != 0 || cb.status != MTLCommandBufferStatusCompleted;
		}

		t = now_us();
		id<MTLLibrary> qc = [device newLibraryWithURL:[NSURL fileURLWithPath:@"/System/Library/Frameworks/QuartzCore.framework/default.metallib"]
		    error:&error];
		if (qc == nil) {
			printf("QuartzCore library: none (%s)\n", error.description.UTF8String);
			return 1;
		}
		NSArray<NSString *> *names = qc.functionNames;
		printf("QuartzCore library: %lu functions (%llu ms)\n", (unsigned long)names.count, (now_us() - t) / 1000);
		unsigned tried = 0, made = 0;
		uint64_t slowest = 0, total = 0;
		for (NSString *name in names) {
			if (tried >= pairs) {
				break;
			}
			if ([name rangeOfString:@"_vert_"].location == NSNotFound) {
				continue;
			}
			NSString *fragmentName = [name stringByReplacingOccurrencesOfString:@"_vert_" withString:@"_frag_"];
			if (![names containsObject:fragmentName]) {
				continue;
			}
			tried++;
			MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
			d.vertexFunction = [qc newFunctionWithName:name];
			d.fragmentFunction = [qc newFunctionWithName:fragmentName];
			d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
			t = now_us();
			id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:d error:&error];
			uint64_t took = now_us() - t;
			total += took;
			slowest = took > slowest ? took : slowest;
			if (state) {
				made++;
			} else {
				printf("  refused %s: %.160s\n", name.UTF8String, error.localizedDescription.UTF8String);
			}
		}
		printf("QuartzCore pipelines: %u of %u made, %llu ms total, slowest %llu ms\n", made, tried, total / 1000, slowest / 1000);
	}
	return failures ? 1 : 0;
}
