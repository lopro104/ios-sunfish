/*
 * hakios-test: test HakiosMetal Objective-C driver layer on Adreno 618.
 * Tests:
 * 1. Device creation (HakiosMetalDevice subclass of _MTLDevice)
 * 2. Buffer allocation in SweetGPU memory (write-combined)
 * 3. Command queue and command buffer lifecycle
 * 4. Blit command encoder: 1 MB fill on GPU (CP_MEMCPY / CP_WAIT)
 * 5. Commit, fence completion, and memory readback verification
 * 6. Metallib parsing & function reflection on system libraries
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach_time.h>

static int gFailures = 0;

static void report(bool ok, const char *msg) {
    printf("%s  %s\n", ok ? "ok" : "FAIL", msg);
    if (!ok) gFailures++;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        printf("=== HakiosMetal Objective-C Driver Test ===\n");

        Class cls = objc_getClass("HakiosMetalDevice");
        if (cls == Nil) {
            printf("FAIL: HakiosMetalDevice class not registered\n");
            return 1;
        }
        report(cls != Nil, "HakiosMetalDevice class registered");

        id<MTLDevice> device = ((id (*)(id, SEL, unsigned int))objc_msgSend)([cls alloc], sel_registerName("initWithAcceleratorPort:"), 7);
        if (device == nil) {
            printf("FAIL: could not instantiate HakiosMetalDevice\n");
            return 1;
        }
        report(device != nil, "instantiated HakiosMetalDevice via initWithAcceleratorPort:");
        printf("device name: %s\n", device.name.UTF8String);
        printf("maxThreadsPerThreadgroup: (%lu, %lu, %lu)\n",
               (unsigned long)device.maxThreadsPerThreadgroup.width,
               (unsigned long)device.maxThreadsPerThreadgroup.height,
               (unsigned long)device.maxThreadsPerThreadgroup.depth);

        // 1. Buffer test
        const NSUInteger bytes = 1024 * 1024; // 1 MB
        id<MTLBuffer> buffer = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        report(buffer != nil && buffer.length == bytes, "newBufferWithLength: 1 MB allocated");
        if (buffer == nil) return 1;

        memset(buffer.contents, 0, bytes);
        report(((uint8_t *)buffer.contents)[0] == 0 && ((uint8_t *)buffer.contents)[bytes - 1] == 0, "buffer zeroed by CPU");

        // 2. Queue & CommandBuffer test
        id<MTLCommandQueue> queue = [device newCommandQueue];
        report(queue != nil, "newCommandQueue: queue created");
        if (queue == nil) return 1;

        id<MTLCommandBuffer> cb = [queue commandBuffer];
        report(cb != nil, "queue commandBuffer: command buffer created");
        if (cb == nil) return 1;

        // 3. Blit fill test
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        report(blit != nil, "cb blitCommandEncoder: blit encoder created");
        if (blit == nil) return 1;

        [blit fillBuffer:buffer range:NSMakeRange(0, bytes) value:0xa5];
        [blit endEncoding];
        report(true, "fillBuffer: 1 MB with 0xa5 recorded");

        uint64_t t0 = mach_absolute_time();
        [cb commit];
        [cb waitUntilCompleted];
        uint64_t t1 = mach_absolute_time();

        mach_timebase_info_data_t tb;
        mach_timebase_info(&tb);
        double us = (double)(t1 - t0) * (double)tb.numer / (double)tb.denom / 1000.0;

        printf("commit to complete: %.1f us, status = %ld\n", us, (long)cb.status);
        report(cb.status == MTLCommandBufferStatusCompleted, "command buffer status == MTLCommandBufferStatusCompleted");

        // 4. Verify readback
        const uint8_t *p = (const uint8_t *)buffer.contents;
        NSUInteger bad = 0;
        for (NSUInteger i = 0; i < bytes; i++) {
            if (p[i] != 0xa5) bad++;
        }
        report(bad == 0, "1 MB blit fill readback: 0 bytes wrong");
        if (bad > 0) {
            printf("FAIL: %lu of %lu bytes incorrect!\n", (unsigned long)bad, (unsigned long)bytes);
        }

        // 5. System metallib test
        const char *metallibPath = "/System/Library/CoreImage/PortraitFilters.cifilter/default.metallib";
        NSError *error = nil;
        id<MTLLibrary> lib = [device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:metallibPath]] error:&error];
        if (lib != nil) {
            NSArray<NSString *> *names = [lib functionNames];
            printf("ok  loaded %s: %lu functions\n", metallibPath, (unsigned long)names.count);
            for (NSUInteger i = 0; i < names.count && i < 5; i++) {
                id<MTLFunction> fn = [lib newFunctionWithName:names[i]];
                printf("    [%lu] %s (type %lu)\n", (unsigned long)i, names[i].UTF8String, (unsigned long)fn.functionType);
            }

            // 6. Compute Pipeline & Dispatch test
            id<MTLFunction> fn = [lib newFunctionWithName:@"MetalFaceMask_InitMinMax"];
            if (fn != nil) {
                NSError *pipeErr = nil;
                uint64_t c0 = mach_absolute_time();
                id<MTLComputePipelineState> computePipe = [device newComputePipelineStateWithFunction:fn error:&pipeErr];
                uint64_t c1 = mach_absolute_time();
                double compUs = (double)(c1 - c0) * (double)tb.numer / (double)tb.denom / 1000.0;

                report(computePipe != nil, "newComputePipelineStateWithFunction: MetalFaceMask_InitMinMax compiled on-device");
                if (computePipe != nil) {
                    printf("    compiled in %.1f us (maxTotalThreadsPerThreadgroup: %lu, threadExecutionWidth: %lu)\n",
                           compUs,
                           (unsigned long)[computePipe maxTotalThreadsPerThreadgroup],
                           (unsigned long)[computePipe threadExecutionWidth]);

                    id<MTLBuffer> compBuf = [device newBufferWithLength:256 options:MTLResourceStorageModeShared];
                    memset(compBuf.contents, 0, 256);

                    id<MTLCommandBuffer> compCb = [queue commandBuffer];
                    id<MTLComputeCommandEncoder> compEnc = [compCb computeCommandEncoder];
                    report(compEnc != nil, "cb computeCommandEncoder: compute encoder created");

                    [compEnc setComputePipelineState:computePipe];
                    [compEnc setBuffer:compBuf offset:0 atIndex:0];
                    [compEnc dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [compEnc endEncoding];

                    uint64_t d0 = mach_absolute_time();
                    [compCb commit];
                    [compCb waitUntilCompleted];
                    uint64_t d1 = mach_absolute_time();
                    double dispUs = (double)(d1 - d0) * (double)tb.numer / (double)tb.denom / 1000.0;

                    printf("    compute dispatch commit to complete: %.1f us, status = %ld\n", dispUs, (long)compCb.status);
                    report(compCb.status == MTLCommandBufferStatusCompleted, "compute command buffer status == Completed");

                    int32_t *words = (int32_t *)compBuf.contents;
                    // MetalFaceMask_InitMinMax writes INT_MAX, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN
                    bool wordsOk = (words[0] == 0x7fffffff && words[1] == 0x7fffffff && words[2] == 0x7fffffff &&
                                    words[3] == (int32_t)0x80000000 && words[4] == (int32_t)0x80000000 && words[5] == (int32_t)0x80000000);
                    report(wordsOk, "MetalFaceMask_InitMinMax compute output verified: INT_MAX x3, INT_MIN x3 exact match on GPU!");
                    printf("    output: [%d, %d, %d, %d, %d, %d]\n", words[0], words[1], words[2], words[3], words[4], words[5]);
                } else {
                    printf("FAIL: pipeline compile failed: %s\n", pipeErr ? pipeErr.localizedDescription.UTF8String : "unknown");
                }
            }
        } else {
            printf("note: %s could not be loaded: %s\n", metallibPath, error ? error.localizedDescription.UTF8String : "unknown error");
        }

        printf("\nResult: %d failures\n", gFailures);
        return gFailures == 0 ? 0 : 1;
    }
}
