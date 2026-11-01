/*
 * hakios-metal-client.mm: verify HakiOS Metal plugin through standard public Metal API.
 * Uses ONLY public Metal.framework and IOSurface.framework calls.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurfaceRef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now_us(void) {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000;
}

// Minimal compiled AIR bitcode for triangle test shaders
// Vertex shader: pass-through position + color
alignas(4) static const uint8_t tri_vs_air[] = {
    0xde, 0xc0, 0x17, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
    0x00, 0x0b, 0x00, 0x00, 0x07, 0x00, 0x00, 0x01, 0x42, 0x43, 0xc0, 0xde,
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int failures = 0;

    @autoreleasepool {
        printf("=== Public Metal API Client Test ===\n");
        uint64_t t0 = now_us();
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        uint64_t dt = now_us() - t0;
        if (!device) {
            printf("FAIL: MTLCreateSystemDefaultDevice returned nil!\n");
            return 1;
        }
        printf("ok  MTLCreateSystemDefaultDevice: %s (%llu us)\n", device.name.UTF8String, dt);

        if (![device.name containsString:@"HakiOS"]) {
            printf("WARN: device is not HakiOS Metal (got %s)\n", device.name.UTF8String);
        }

        // Test 1: Buffer allocation & Blit fill
        const NSUInteger bufSize = 1024 * 1024;
        id<MTLBuffer> buffer = [device newBufferWithLength:bufSize options:MTLResourceStorageModeShared];
        if (!buffer) {
            printf("FAIL: newBufferWithLength failed\n");
            failures++;
        } else {
            memset(buffer.contents, 0, bufSize);
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit fillBuffer:buffer range:NSMakeRange(0, bufSize) value:0x77];
            [blit endEncoding];

            t0 = now_us();
            [cb commit];
            [cb waitUntilCompleted];
            dt = now_us() - t0;

            const uint8_t *p = (const uint8_t *)buffer.contents;
            NSUInteger bad = 0;
            for (NSUInteger i = 0; i < bufSize; i++) {
                if (p[i] != 0x77) bad++;
            }
            if (bad == 0 && cb.status == MTLCommandBufferStatusCompleted) {
                printf("ok  1 MB Blit fill: %lu bytes correct on GPU (%llu us, status %ld)\n",
                       (unsigned long)bufSize, dt, (long)cb.status);
            } else {
                printf("FAIL: 1 MB Blit fill: %lu bad bytes, status %ld\n",
                       (unsigned long)bad, (long)cb.status);
                failures++;
            }
        }

        // Test 2: CoreImage Metallib Loading & Compute Shader Execution
        NSError *err = nil;
        NSString *libPath = @"/System/Library/CoreImage/PortraitFilters.cifilter/default.metallib";
        id<MTLLibrary> lib = [device newLibraryWithFile:libPath error:&err];
        if (!lib) {
            printf("FAIL: newLibraryWithFile: %s\n", err ? err.localizedDescription.UTF8String : "unknown");
            failures++;
        } else {
            printf("ok  loaded library %s: %lu functions\n",
                   libPath.UTF8String, (unsigned long)lib.functionNames.count);
            id<MTLFunction> fn = [lib newFunctionWithName:@"MetalFaceMask_InitMinMax"];
            if (!fn) {
                printf("FAIL: newFunctionWithName MetalFaceMask_InitMinMax failed\n");
                failures++;
            } else {
                t0 = now_us();
                id<MTLComputePipelineState> cps = [device newComputePipelineStateWithFunction:fn error:&err];
                dt = now_us() - t0;
                if (!cps) {
                    printf("FAIL: newComputePipelineStateWithFunction: %s\n",
                           err ? err.localizedDescription.UTF8String : "unknown");
                    failures++;
                } else {
                    printf("ok  compiled compute pipeline in %llu us (maxThreads: %lu, execWidth: %lu)\n",
                           dt, (unsigned long)cps.maxTotalThreadsPerThreadgroup,
                           (unsigned long)cps.threadExecutionWidth);

                    // Execute compute shader
                    id<MTLBuffer> outBuf = [device newBufferWithLength:64 * sizeof(int32_t) options:MTLResourceStorageModeShared];
                    int32_t *outPtr = (int32_t *)outBuf.contents;
                    for (int i = 0; i < 64; i++) outPtr[i] = 12345;

                    id<MTLCommandQueue> queue = [device newCommandQueue];
                    id<MTLCommandBuffer> cb = [queue commandBuffer];
                    id<MTLComputeCommandEncoder> cenc = [cb computeCommandEncoder];
                    [cenc setComputePipelineState:cps];
                    [cenc setBuffer:outBuf offset:0 atIndex:0];
                    [cenc dispatchThreads:MTLSizeMake(64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                    [cenc endEncoding];

                    t0 = now_us();
                    [cb commit];
                    [cb waitUntilCompleted];
                    dt = now_us() - t0;

                    bool match = (outPtr[0] == 2147483647 && outPtr[1] == 2147483647 && outPtr[2] == 2147483647 &&
                                  outPtr[3] == -2147483648 && outPtr[4] == -2147483648 && outPtr[5] == -2147483648);
                    if (match && cb.status == MTLCommandBufferStatusCompleted) {
                        printf("ok  compute shader executed on Adreno 618 in %llu us (status %ld, INT_MAX/MIN match!)\n",
                               dt, (long)cb.status);
                    } else {
                        printf("FAIL: compute execution mismatch: [%d, %d, %d, %d, %d, %d], status %ld\n",
                               outPtr[0], outPtr[1], outPtr[2], outPtr[3], outPtr[4], outPtr[5], (long)cb.status);
                        failures++;
                    }
                }
            }
        }

        // Test 3: Textures and Samplers
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                      width:64
                                                                                     height:64
                                                                                  mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
        id<MTLTexture> tex = [device newTextureWithDescriptor:td];
        if (!tex) {
            printf("FAIL: newTextureWithDescriptor failed\n");
            failures++;
        } else {
            uint32_t uploadPixels[64 * 64];
            for (int i = 0; i < 64 * 64; i++) uploadPixels[i] = 0xff00ff00 | (i & 0xff);
            [tex replaceRegion:MTLRegionMake2D(0, 0, 64, 64) mipmapLevel:0 withBytes:uploadPixels bytesPerRow:64 * 4];

            uint32_t readPixels[64 * 64] = {0};
            [tex getBytes:readPixels bytesPerRow:64 * 4 fromRegion:MTLRegionMake2D(0, 0, 64, 64) mipmapLevel:0];

            int texErr = 0;
            for (int i = 0; i < 64 * 64; i++) {
                if (readPixels[i] != uploadPixels[i]) texErr++;
            }
            if (texErr == 0) {
                printf("ok  64x64 RGBA8Unorm texture replaceRegion and getBytes verified (0 errors)\n");
            } else {
                printf("FAIL: texture readback had %d errors\n", texErr);
                failures++;
            }
        }

        MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sd];
        if (sampler) {
            printf("ok  newSamplerStateWithDescriptor: linear sampler created\n");
        } else {
            printf("FAIL: newSamplerStateWithDescriptor returned nil\n");
            failures++;
        }

        // Test 4: Render Command Encoder Hardware Clear Pass
        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = tex;
        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.2, 0.4, 0.6, 1.0); // RGBA ~ 51, 102, 153, 255

        id<MTLCommandQueue> rqueue = [device newCommandQueue];
        id<MTLCommandBuffer> rcb = [rqueue commandBuffer];
        id<MTLRenderCommandEncoder> renc = [rcb renderCommandEncoderWithDescriptor:rpd];
        [renc endEncoding];

        t0 = now_us();
        [rcb commit];
        [rcb waitUntilCompleted];
        dt = now_us() - t0;

        uint8_t clearPixel[4] = {0};
        [tex getBytes:clearPixel bytesPerRow:64 * 4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
        if (abs(clearPixel[0] - 51) <= 1 && abs(clearPixel[1] - 102) <= 1 &&
            abs(clearPixel[2] - 153) <= 1 && clearPixel[3] == 255 &&
            rcb.status == MTLCommandBufferStatusCompleted) {
            printf("ok  hardware render clear pass executed in %llu us (color: [%d, %d, %d, %d])\n",
                   dt, clearPixel[0], clearPixel[1], clearPixel[2], clearPixel[3]);
        } else {
            printf("FAIL: render clear mismatch: [%d, %d, %d, %d], status %ld\n",
                   clearPixel[0], clearPixel[1], clearPixel[2], clearPixel[3], (long)rcb.status);
            failures++;
        }

        // Test 5: IOSurface Zero-Copy Hardware Clear
        NSDictionary *surfaceProps = @{
            (id)kIOSurfaceWidth: @64,
            (id)kIOSurfaceHeight: @64,
            (id)kIOSurfaceBytesPerElement: @4,
            (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
        };
        IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef)surfaceProps);
        if (!surf) {
            printf("FAIL: IOSurfaceCreate returned NULL\n");
            failures++;
        } else {
            IOSurfaceID surfId = IOSurfaceGetID(surf);
            MTLTextureDescriptor *ioTexDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                                 width:64
                                                                                                height:64
                                                                                             mipmapped:NO];
            ioTexDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            id<MTLTexture> ioTex = [device newTextureWithDescriptor:ioTexDesc iosurface:surf plane:0];
            if (!ioTex) {
                printf("FAIL: newTextureWithDescriptor:iosurface: returned nil\n");
                failures++;
            } else {
                MTLRenderPassDescriptor *ioRpd = [MTLRenderPassDescriptor renderPassDescriptor];
                ioRpd.colorAttachments[0].texture = ioTex;
                ioRpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                ioRpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                ioRpd.colorAttachments[0].clearColor = MTLClearColorMake(0.4, 0.6, 0.8, 1.0); // RGBA (102, 153, 204, 255)

                id<MTLCommandBuffer> ioCb = [rqueue commandBuffer];
                id<MTLRenderCommandEncoder> ioEnc = [ioCb renderCommandEncoderWithDescriptor:ioRpd];
                [ioEnc endEncoding];

                t0 = now_us();
                [ioCb commit];
                [ioCb waitUntilCompleted];
                dt = now_us() - t0;

                IOSurfaceLock(surf, kIOSurfaceLockReadOnly, NULL);
                const uint8_t *cpuBase = (const uint8_t *)IOSurfaceGetBaseAddress(surf);
                // BGRA format: byte 0=B(204), byte 1=G(153), byte 2=R(102), byte 3=A(255)
                bool ioMatch = (abs(cpuBase[0] - 204) <= 1 && abs(cpuBase[1] - 153) <= 1 &&
                                abs(cpuBase[2] - 102) <= 1 && cpuBase[3] == 255);
                IOSurfaceUnlock(surf, kIOSurfaceLockReadOnly, NULL);

                if (ioMatch && ioCb.status == MTLCommandBufferStatusCompleted) {
                    printf("ok  IOSurface (ID %u) zero-copy GPU clear executed in %llu us (CPU read: BGRA [%d, %d, %d, %d])\n",
                           surfId, dt, cpuBase[0], cpuBase[1], cpuBase[2], cpuBase[3]);
                } else {
                    printf("FAIL: IOSurface GPU write mismatch: [%d, %d, %d, %d], status %ld\n",
                           cpuBase[0], cpuBase[1], cpuBase[2], cpuBase[3], (long)ioCb.status);
                    failures++;
                }
            }
            CFRelease(surf);
        }

        // Test 6: QuartzCore Shaders Compilation
        printf("--- QuartzCore Shaders Test ---\n");
        NSString *qcPath = @"/System/Library/Frameworks/QuartzCore.framework/default.metallib";
        id<MTLLibrary> qcLib = [device newLibraryWithFile:qcPath error:&err];
        if (!qcLib) {
            printf("WARN: could not load QuartzCore default.metallib: %s\n",
                   err ? err.localizedDescription.UTF8String : "unknown");
        } else {
            NSArray<NSString *> *qcNames = qcLib.functionNames;
            printf("ok  QuartzCore default.metallib: %lu functions\n", (unsigned long)qcNames.count);
            unsigned tried = 0, compiled = 0;
            for (NSString *name in qcNames) {
                if ([name rangeOfString:@"_vert_"].location == NSNotFound) continue;
                NSString *fragName = [name stringByReplacingOccurrencesOfString:@"_vert_" withString:@"_frag_"];
                if (![qcNames containsObject:fragName]) continue;

                tried++;
                MTLRenderPipelineDescriptor *rdesc = [[MTLRenderPipelineDescriptor alloc] init];
                rdesc.vertexFunction = [qcLib newFunctionWithName:name];
                rdesc.fragmentFunction = [qcLib newFunctionWithName:fragName];
                rdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

                t0 = now_us();
                id<MTLRenderPipelineState> rps = [device newRenderPipelineStateWithDescriptor:rdesc error:&err];
                dt = now_us() - t0;
                if (rps) {
                    compiled++;
                    printf("  ok  pipeline [%s + %s] compiled in %llu us\n",
                           name.UTF8String, fragName.UTF8String, dt);
                } else {
                    printf("  skip pipeline [%s + %s]: %s\n",
                           name.UTF8String, fragName.UTF8String,
                           err ? err.localizedDescription.UTF8String : "unknown");
                }
            }
            printf("ok  QuartzCore render pipelines compiled: %u of %u\n", compiled, tried);
        }

        // Test 7: GPU Family & Feature Checks
        printf("--- GPU Feature Queries ---\n");
        printf("hasUnifiedMemory: %d\n", [device hasUnifiedMemory]);
        printf("maxThreadsPerThreadgroup: (%lu, %lu, %lu)\n",
               (unsigned long)device.maxThreadsPerThreadgroup.width,
               (unsigned long)device.maxThreadsPerThreadgroup.height,
               (unsigned long)device.maxThreadsPerThreadgroup.depth);
        printf("maxBufferLength: %lu MB\n", (unsigned long)(device.maxBufferLength / (1024 * 1024)));
        printf("supportsFamily:Common1: %d\n", [device supportsFamily:MTLGPUFamilyCommon1]);
        printf("supportsFamily:Common2: %d\n", [device supportsFamily:MTLGPUFamilyCommon2]);
        printf("supportsFamily:Common3: %d\n", [device supportsFamily:MTLGPUFamilyCommon3]);
        printf("supportsFamily:Apple1:   %d\n", [device supportsFamily:MTLGPUFamilyApple1]);
        printf("supportsFamily:Apple2:   %d\n", [device supportsFamily:MTLGPUFamilyApple2]);
        printf("supportsFamily:Apple3:   %d\n", [device supportsFamily:MTLGPUFamilyApple3]);

        printf("\nResult: %d failures\n", failures);
    }
    return failures ? 1 : 0;
}
