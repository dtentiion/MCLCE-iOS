// Minimal Metal backend wired behind the mcle_render_* interface.
// Draws a single triangle so we can confirm the GPU pipeline end to end.
// Real rendering (translated shaders, vertex buffers, textures, lighting)
// layers on top of this.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "RND_iOS_Stub.h"

#include <atomic>

namespace {

// Inline MSL. Kept here on purpose; once the real translated shaders are
// bundled we compile them from .metallib at runtime and retire this block.
NSString* const kTriangleShaderSource = @R"(
    #include <metal_stdlib>
    using namespace metal;

    struct v2f {
        float4 position [[position]];
        float3 color;
    };

    vertex v2f triangle_vert(uint vid [[vertex_id]]) {
        constexpr float2 positions[3] = {
            float2( 0.0,  0.6),
            float2(-0.6, -0.5),
            float2( 0.6, -0.5),
        };
        constexpr float3 colors[3] = {
            float3(0.95, 0.32, 0.32),  // red
            float3(0.32, 0.95, 0.45),  // green
            float3(0.32, 0.55, 0.95),  // blue
        };
        v2f o;
        o.position = float4(positions[vid], 0.0, 1.0);
        o.color    = colors[vid];
        return o;
    }

    fragment float4 triangle_frag(v2f in [[stage_in]]) {
        return float4(in.color, 1.0);
    }
)";

struct RendererState {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLRenderPipelineState> pipeline;
    CAMetalLayer* layer;       // weak; owned by the UIView / MTKView
    int width;
    int height;
    bool initialized;
};

RendererState g;
std::atomic<bool> g_ready{false};

bool build_pipeline(NSError** err) {
    id<MTLLibrary> lib = [g.device newLibraryWithSource:kTriangleShaderSource
                                                options:nil
                                                  error:err];
    if (!lib) return false;

    id<MTLFunction> vfn = [lib newFunctionWithName:@"triangle_vert"];
    id<MTLFunction> ffn = [lib newFunctionWithName:@"triangle_frag"];
    if (!vfn || !ffn) return false;

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vfn;
    desc.fragmentFunction = ffn;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    g.pipeline = [g.device newRenderPipelineStateWithDescriptor:desc error:err];
    return g.pipeline != nil;
}

} // namespace

extern "C" int mcle_render_init(void* metal_layer_or_glcontext, int width, int height) {
    if (g.initialized) return 0;

    g.device = MTLCreateSystemDefaultDevice();
    if (!g.device) {
        NSLog(@"[mcle_render] Metal not available on this device");
        return 1;
    }

    g.queue = [g.device newCommandQueue];
    if (!g.queue) {
        NSLog(@"[mcle_render] failed to create command queue");
        return 2;
    }

    // The view controller hands us the CAMetalLayer from its MTKView. If it
    // passes a plain CALayer we promote it; otherwise assume it is already
    // a CAMetalLayer.
    CALayer* layer = (__bridge CALayer*)metal_layer_or_glcontext;
    if ([layer isKindOfClass:[CAMetalLayer class]]) {
        g.layer = (CAMetalLayer*)layer;
    } else {
        NSLog(@"[mcle_render] expected a CAMetalLayer, got %@", NSStringFromClass([layer class]));
        // Not fatal; drawables will just not display.
    }

    if (g.layer) {
        g.layer.device = g.device;
        g.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g.layer.framebufferOnly = YES;
        g.layer.drawableSize = CGSizeMake(width, height);
    }

    NSError* err = nil;
    if (!build_pipeline(&err)) {
        NSLog(@"[mcle_render] pipeline build failed: %@", err);
        return 3;
    }

    g.width = width;
    g.height = height;
    g.initialized = true;
    g_ready.store(true, std::memory_order_release);

    NSLog(@"[mcle_render] Metal init ok: %s, %dx%d",
        [[g.device name] UTF8String], width, height);
    return 0;
}

extern "C" void mcle_render_resize(int width, int height) {
    g.width = width;
    g.height = height;
    if (g.layer) {
        g.layer.drawableSize = CGSizeMake(width, height);
    }
}

extern "C" void mcle_render_frame(void) {
    if (!g_ready.load(std::memory_order_acquire) || !g.layer) return;

    id<CAMetalDrawable> drawable = [g.layer nextDrawable];
    if (!drawable) return;

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture = drawable.texture;
    rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.07, 0.07, 0.09, 1.0);

    id<MTLCommandBuffer> cmd = [g.queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
    [enc setRenderPipelineState:g.pipeline];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];

    [cmd presentDrawable:drawable];
    [cmd commit];
}

extern "C" void mcle_render_shutdown(void) {
    g_ready.store(false, std::memory_order_release);
    g.pipeline = nil;
    g.queue = nil;
    g.layer = nil;
    g.device = nil;
    g.initialized = false;
}
