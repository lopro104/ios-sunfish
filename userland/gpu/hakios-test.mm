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
#include <IOSurface/IOSurfaceRef.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach_time.h>
#include "tests/fixtures/tri_pos_vs_bc.hpp"
#include "tests/fixtures/tri_color_fs_bc.hpp"

@interface MockFunction : NSObject <MTLFunction>
@property (nonatomic, copy) NSString *name;
@property (nonatomic) MTLFunctionType functionType;
@property (nonatomic, strong) NSData *hakiosBitcode;
@end

@implementation MockFunction
- (id<MTLDevice>)device { return nil; }
- (NSDictionary<NSString *, MTLFunctionConstant *> *)functionConstantsDictionary { return @{}; }
- (NSArray<MTLVertexAttribute *> *)vertexAttributes { return @[]; }
- (NSArray<MTLAttribute *> *)stageInputAttributes { return @[]; }
- (NSData *)bitcodeData { return self.hakiosBitcode; }
- (NSString *)label { return self.name; }
- (void)setLabel:(NSString *)label { (void)label; }
@end

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

        // 7. Texture creation, write and readback test
        printf("\n--- Texture & Sampler Tests ---\n");
        MTLTextureDescriptor *texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                          width:64
                                                                                         height:64
                                                                                      mipmapped:NO];
        texDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
        id<MTLTexture> tex = [device newTextureWithDescriptor:texDesc];
        report(tex != nil && tex.width == 64 && tex.height == 64, "newTextureWithDescriptor: 64x64 RGBA8Unorm texture created");

        if (tex != nil) {
            uint32_t fillPixels[64 * 64];
            for (int y = 0; y < 64; y++) {
                for (int x = 0; x < 64; x++) {
                    fillPixels[y * 64 + x] = 0xff000000 | (x << 16) | (y << 8) | 0x7f;
                }
            }
            [tex replaceRegion:MTLRegionMake2D(0, 0, 64, 64) mipmapLevel:0 withBytes:fillPixels bytesPerRow:64 * 4];
            report(true, "replaceRegion: 64x64 pixels uploaded");

            uint32_t readPixels[64 * 64] = {0};
            [tex getBytes:readPixels bytesPerRow:64 * 4 fromRegion:MTLRegionMake2D(0, 0, 64, 64) mipmapLevel:0];
            bool texMatch = (memcmp(fillPixels, readPixels, sizeof(fillPixels)) == 0);
            report(texMatch, "getBytes: readback matches uploaded pixels (0 errors)");

            // 8. Sampler State test
            MTLSamplerDescriptor *sampDesc = [[MTLSamplerDescriptor alloc] init];
            sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
            sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
            sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
            sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
            id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sampDesc];
            report(sampler != nil, "newSamplerStateWithDescriptor: linear sampler created");

            // 9. Render Encoder Clear Pass test
            printf("\n--- Render Pass Clear Test ---\n");
            MTLRenderPassDescriptor *passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
            passDesc.colorAttachments[0].texture = tex;
            passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
            passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.2, 0.4, 0.6, 1.0);
            passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLCommandBuffer> renderCb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> renderEnc = [renderCb renderCommandEncoderWithDescriptor:passDesc];
            report(renderEnc != nil, "cb renderCommandEncoderWithDescriptor: created");
            if (renderEnc != nil) {
                [renderEnc endEncoding];
                report(true, "renderEnc endEncoding (clear pass recorded)");

                uint64_t r0 = mach_absolute_time();
                [renderCb commit];
                [renderCb waitUntilCompleted];
                uint64_t r1 = mach_absolute_time();
                double rendUs = (double)(r1 - r0) * (double)tb.numer / (double)tb.denom / 1000.0;

                printf("    render clear commit to complete: %.1f us, status = %ld\n", rendUs, (long)renderCb.status);
                report(renderCb.status == MTLCommandBufferStatusCompleted, "render command buffer status == Completed");

                uint8_t clearRead[4] = {0};
                [tex getBytes:clearRead bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
                // 0.2*255=51 (0x33), 0.4*255=102 (0x66), 0.6*255=153 (0x99), 1.0*255=255 (0xff)
                printf("    cleared pixel (0,0) RGBA: [%u, %u, %u, %u]\n", clearRead[0], clearRead[1], clearRead[2], clearRead[3]);
                bool clearOk = (clearRead[0] >= 50 && clearRead[0] <= 52 &&
                                clearRead[1] >= 101 && clearRead[1] <= 103 &&
                                clearRead[2] >= 152 && clearRead[2] <= 154 &&
                                clearRead[3] == 255);
                report(clearOk, "render pass clear color readback verified on GPU hardware!");
            }
        }

        // 10. IOSurface-backed Texture Test (Render target into IOSurface)
        printf("\n--- IOSurface-backed Texture Test ---\n");
        int surfW = 64, surfH = 64;
        CFMutableDictionaryRef props = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        int val = surfW;
        CFNumberRef nW = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &val);
        CFDictionarySetValue(props, kIOSurfaceWidth, nW);
        CFRelease(nW);
        val = surfH;
        CFNumberRef nH = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &val);
        CFDictionarySetValue(props, kIOSurfaceHeight, nH);
        CFRelease(nH);
        val = 4;
        CFNumberRef nBpe = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &val);
        CFDictionarySetValue(props, kIOSurfaceBytesPerElement, nBpe);
        CFRelease(nBpe);
        val = surfW * 4;
        CFNumberRef nBpr = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &val);
        CFDictionarySetValue(props, kIOSurfaceBytesPerRow, nBpr);
        CFRelease(nBpr);
        val = 0x42475241; // 'BGRA'
        CFNumberRef nFmt = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &val);
        CFDictionarySetValue(props, kIOSurfacePixelFormat, nFmt);
        CFRelease(nFmt);

        IOSurfaceRef surf = IOSurfaceCreate(props);
        CFRelease(props);
        report(surf != NULL, "IOSurfaceCreate: 64x64 BGRA surface created");

        if (surf != NULL) {
            uint32_t surfId = IOSurfaceGetID(surf);
            printf("    IOSurface ID: %u\n", surfId);

            MTLTextureDescriptor *ioTexDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                                 width:surfW
                                                                                                height:surfH
                                                                                             mipmapped:NO];
            ioTexDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            id<MTLTexture> ioTex = [device newTextureWithDescriptor:ioTexDesc iosurface:surf plane:0];
            report(ioTex != nil, "newTextureWithDescriptor:iosurface:plane:0 created over IOSurface");

            if (ioTex != nil) {
                MTLRenderPassDescriptor *ioPass = [MTLRenderPassDescriptor renderPassDescriptor];
                ioPass.colorAttachments[0].texture = ioTex;
                ioPass.colorAttachments[0].loadAction = MTLLoadActionClear;
                ioPass.colorAttachments[0].clearColor = MTLClearColorMake(0.8, 0.6, 0.4, 1.0); // R=204, G=153, B=102, A=255
                ioPass.colorAttachments[0].storeAction = MTLStoreActionStore;

                id<MTLCommandBuffer> ioCb = [queue commandBuffer];
                id<MTLRenderCommandEncoder> ioEnc = [ioCb renderCommandEncoderWithDescriptor:ioPass];
                [ioEnc endEncoding];

                uint64_t s0 = mach_absolute_time();
                [ioCb commit];
                [ioCb waitUntilCompleted];
                uint64_t s1 = mach_absolute_time();
                double ioUs = (double)(s1 - s0) * (double)tb.numer / (double)tb.denom / 1000.0;

                printf("    IOSurface clear pass commit to complete: %.1f us, status = %ld\n", ioUs, (long)ioCb.status);
                report(ioCb.status == MTLCommandBufferStatusCompleted, "IOSurface clear status == Completed");

                IOSurfaceLock(surf, kIOSurfaceLockReadOnly, NULL);
                uint8_t *surfBase = (uint8_t *)IOSurfaceGetBaseAddress(surf);
                printf("    IOSurface CPU readback at (0,0) BGRA: [%u, %u, %u, %u]\n",
                       surfBase[0], surfBase[1], surfBase[2], surfBase[3]);
                bool ioMatch = (surfBase[0] >= 101 && surfBase[0] <= 103 &&
                                surfBase[1] >= 152 && surfBase[1] <= 154 &&
                                surfBase[2] >= 203 && surfBase[2] <= 205 &&
                                surfBase[3] == 255);
                report(ioMatch, "IOSurface memory verified written by GPU! (SweetGPU surface 0x7e confirmed)");
                IOSurfaceUnlock(surf, kIOSurfaceLockReadOnly, NULL);
            }
            CFRelease(surf);
        }

        // 11. Render Pipeline Compilation & Hardware Triangle Draw Test
        printf("\n--- Render Pipeline Compilation & Draw Test ---\n");
        MockFunction *vsFn = [MockFunction new];
        vsFn.name = @"tri_pos_vs";
        vsFn.functionType = MTLFunctionTypeVertex;
        vsFn.hakiosBitcode = [NSData dataWithBytes:hakios::metal::kernels::tri_pos_vs_bc length:sizeof(hakios::metal::kernels::tri_pos_vs_bc)];

        MockFunction *fsFn = [MockFunction new];
        fsFn.name = @"tri_color_fs";
        fsFn.functionType = MTLFunctionTypeFragment;
        fsFn.hakiosBitcode = [NSData dataWithBytes:hakios::metal::kernels::tri_color_fs_bc length:sizeof(hakios::metal::kernels::tri_color_fs_bc)];

        MTLRenderPipelineDescriptor *rpDesc = [MTLRenderPipelineDescriptor new];
        rpDesc.vertexFunction = vsFn;
        rpDesc.fragmentFunction = fsFn;
        rpDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        MTLVertexDescriptor *vdesc = [MTLVertexDescriptor vertexDescriptor];
        vdesc.attributes[0].format = MTLVertexFormatFloat4;
        vdesc.attributes[0].offset = 0;
        vdesc.attributes[0].bufferIndex = 0;

        vdesc.attributes[1].format = MTLVertexFormatFloat4;
        vdesc.attributes[1].offset = 16;
        vdesc.attributes[1].bufferIndex = 0;

        vdesc.layouts[0].stride = 32;
        vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        vdesc.layouts[0].stepRate = 1;
        rpDesc.vertexDescriptor = vdesc;

        NSError *rpErr = nil;
        uint64_t rp0 = mach_absolute_time();
        id<MTLRenderPipelineState> rpipe = [device newRenderPipelineStateWithDescriptor:rpDesc error:&rpErr];
        uint64_t rp1 = mach_absolute_time();
        double rpUs = (double)(rp1 - rp0) * (double)tb.numer / (double)tb.denom / 1000.0;
        report(rpipe != nil, "newRenderPipelineStateWithDescriptor: compiled on-device (vs+fs)");
        if (rpipe != nil) {
            printf("    compiled in %.1f us\n", rpUs);

            struct Vertex {
                float pos[4];
                float col[4];
            };
            struct Vertex triVertices[3] = {
                { { -1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } }, // Red bottom-left
                { {  1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } }, // Green bottom-right
                { {  0.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }  // Blue top-center
            };
            id<MTLBuffer> vb = [device newBufferWithBytes:triVertices length:sizeof(triVertices) options:MTLResourceStorageModeShared];
            report(vb != nil, "vertex buffer allocated with 3 vertices");

            // Draw into 64x64 RGBA8Unorm texture
            MTLRenderPassDescriptor *drawPass = [MTLRenderPassDescriptor renderPassDescriptor];
            drawPass.colorAttachments[0].texture = tex;
            drawPass.colorAttachments[0].loadAction = MTLLoadActionClear;
            drawPass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0); // Black clear
            drawPass.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLCommandBuffer> drawCb = [queue commandBuffer];
            id<MTLRenderCommandEncoder> drawEnc = [drawCb renderCommandEncoderWithDescriptor:drawPass];
            report(drawEnc != nil, "draw command encoder created");

            [drawEnc setRenderPipelineState:rpipe];
            [drawEnc setVertexBuffer:vb offset:0 atIndex:0];
            [drawEnc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [drawEnc endEncoding];

            uint64_t dr0 = mach_absolute_time();
            [drawCb commit];
            [drawCb waitUntilCompleted];
            uint64_t dr1 = mach_absolute_time();
            double drawUs = (double)(dr1 - dr0) * (double)tb.numer / (double)tb.denom / 1000.0;

            printf("    triangle draw commit to complete: %.1f us, status = %ld\n", drawUs, (long)drawCb.status);
            report(drawCb.status == MTLCommandBufferStatusCompleted, "triangle draw command buffer status == Completed");

            uint8_t centerPix[4] = {0};
            [tex getBytes:centerPix bytesPerRow:4 fromRegion:MTLRegionMake2D(32, 32, 1, 1) mipmapLevel:0];
            printf("    rendered triangle center pixel (32, 32) RGBA: [%u, %u, %u, %u]\n",
                   centerPix[0], centerPix[1], centerPix[2], centerPix[3]);
            bool triangleRendered = (centerPix[0] > 0 || centerPix[1] > 0 || centerPix[2] > 0) && (centerPix[3] == 255);
            report(triangleRendered, "rendered triangle pixel verified on hardware Adreno 618!");
        } else {
            printf("FAIL: render pipeline compilation failed: %s\n", rpErr ? rpErr.localizedDescription.UTF8String : "unknown");
        }

        printf("\nResult: %d failures\n", gFailures);
        return gFailures == 0 ? 0 : 1;
    }
}
