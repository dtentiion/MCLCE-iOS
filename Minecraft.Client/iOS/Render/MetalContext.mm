#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>

#include "MetalContext.h"

namespace {

std::atomic<unsigned long long> g_draw_count{0};

struct Ctx {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    __weak CAMetalLayer* layer = nil;

    int width = 0;
    int height = 0;

    id<CAMetalDrawable> drawable = nil;
    id<MTLCommandBuffer> cmd = nil;
    id<MTLRenderCommandEncoder> enc = nil;

    bool inFrame = false;
};

Ctx g;

} // namespace

extern "C" int mcle_metal_ensure_device(void) {
    if (g.device) return 0;
    g.device = MTLCreateSystemDefaultDevice();
    if (!g.device) {
        NSLog(@"[mcle_metal] no Metal device");
        return 1;
    }
    g.queue = [g.device newCommandQueue];
    if (!g.queue) {
        NSLog(@"[mcle_metal] failed to create command queue");
        return 2;
    }
    NSLog(@"[mcle_metal] device=%@", g.device.name);
    return 0;
}

extern "C" int mcle_metal_available(void) {
    return g.device ? 1 : 0;
}

extern "C" void mcle_metal_attach_layer(void* ca_metal_layer, int width, int height) {
    if (mcle_metal_ensure_device() != 0) return;

    g.width = width;
    g.height = height;

    // Null layer = resize-only call. Keep whatever layer was attached before
    // and just update the drawable size on it.
    CAMetalLayer* layer = (__bridge CAMetalLayer*)ca_metal_layer;
    if (layer) {
        g.layer = layer;
        layer.device = g.device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
    }

    if (g.layer) {
        g.layer.drawableSize = CGSizeMake(width, height);
    }
}

extern "C" int mcle_metal_frame_begin(float r, float gn, float b, float a) {
    if (!g.device || !g.layer) return 1;
    if (g.inFrame) return 0;

    g.drawable = [g.layer nextDrawable];
    if (!g.drawable) return 2;

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = g.drawable.texture;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(r, gn, b, a);

    g.cmd = [g.queue commandBuffer];
    g.enc = [g.cmd renderCommandEncoderWithDescriptor:rpd];
    g.inFrame = true;
    return 0;
}

extern "C" void* mcle_metal_current_encoder(void) {
    return g.inFrame ? (__bridge void*)g.enc : NULL;
}

extern "C" void mcle_metal_current_size(int* out_width, int* out_height) {
    if (out_width)  *out_width  = g.width;
    if (out_height) *out_height = g.height;
}

extern "C" void mcle_metal_frame_end(void) {
    if (!g.inFrame) return;
    [g.enc endEncoding];
    [g.cmd presentDrawable:g.drawable];
    [g.cmd commit];
    g.enc = nil;
    g.cmd = nil;
    g.drawable = nil;
    g.inFrame = false;
}

// G2a: Tesselator -> Metal hook counter. Real vertex buffer upload + draw
// lands in G3 once we wire upstream LevelRenderer to invoke this path.
extern "C" void mcle_metal_draw_vertices(int /*prim*/, int /*count*/,
                                          const void* /*data*/,
                                          int /*fmt*/, int /*shader*/) {
    g_draw_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" unsigned long long mcle_metal_draw_count(void) {
    return g_draw_count.load(std::memory_order_relaxed);
}

// Internal helpers for the renderers in this library.
#ifdef __cplusplus
extern "C" {
#endif

// Not exported via the header but used inside the Render library to share
// the raw device for pipeline creation.
id<MTLDevice> mcle_metal_shared_device_objc(void);

#ifdef __cplusplus
}
#endif

id<MTLDevice> mcle_metal_shared_device_objc(void) {
    return g.device;
}
