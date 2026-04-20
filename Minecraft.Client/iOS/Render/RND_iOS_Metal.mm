// Triangle renderer. Kept as a visual fallback when no SWF is loaded so we
// can tell at a glance that the Metal pipeline is alive. Once GameSWF drives
// the frame end to end, this file becomes optional.
//
// All device / queue / drawable state lives in MetalContext now; this file
// just owns a pipeline state + the tick callback.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "MetalContext.h"
#include "RND_iOS_Stub.h"

// Forward-declared (defined in mcle_ios_ui). Avoids a circular include
// between the Render library and the UI library.
extern "C" void mcle_swf_tick(float dt);
extern "C" void mcle_swf_tick_with_viewport(float dt, int vp_w, int vp_h);
extern "C" void mcle_swf_draw_test_rect(int vp_w, int vp_h);
extern "C" int  mcle_swf_is_ready(void);
extern "C" int  mcle_swf_has_movie(void);

#include <atomic>

extern "C" id<MTLDevice> mcle_metal_shared_device_objc(void);

namespace {

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
            float3(0.95, 0.32, 0.32),
            float3(0.32, 0.95, 0.45),
            float3(0.32, 0.55, 0.95),
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

id<MTLRenderPipelineState> g_pipeline = nil;
std::atomic<bool> g_ready{false};

bool build_pipeline() {
    id<MTLDevice> device = mcle_metal_shared_device_objc();
    if (!device) return false;

    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:kTriangleShaderSource
                                              options:nil
                                                error:&err];
    if (!lib) { NSLog(@"[triangle] library build failed: %@", err); return false; }

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = [lib newFunctionWithName:@"triangle_vert"];
    desc.fragmentFunction = [lib newFunctionWithName:@"triangle_frag"];
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    g_pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_pipeline) { NSLog(@"[triangle] pipeline build failed: %@", err); return false; }
    return true;
}

} // namespace

extern "C" int mcle_render_init(void* metal_layer, int width, int height) {
    mcle_metal_ensure_device();
    mcle_metal_attach_layer(metal_layer, width, height);

    if (!build_pipeline()) return 1;
    g_ready.store(true, std::memory_order_release);
    NSLog(@"[mcle_render] init ok: %dx%d", width, height);
    return 0;
}

extern "C" void mcle_render_resize(int width, int height) {
    mcle_metal_attach_layer(nullptr, width, height);
}

extern "C" void mcle_render_frame(void) {
    if (!g_ready.load(std::memory_order_acquire)) return;

    // Dark charcoal clear, matches the original shell so the user can see
    // that Metal is alive even if every draw call below is a no-op.
    if (mcle_metal_frame_begin(0.07f, 0.07f, 0.09f, 1.0f) != 0) return;

    id<MTLRenderCommandEncoder> enc =
        (__bridge id<MTLRenderCommandEncoder>)mcle_metal_current_encoder();
    if (enc && g_pipeline) {
        [enc setRenderPipelineState:g_pipeline];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }

    // GameSWF drawing happens here. If no movie is loaded, it is a no-op.
    int vw = 0, vh = 0;
    mcle_metal_current_size(&vw, &vh);
    mcle_swf_tick_with_viewport(1.0f / 60.0f, vw, vh);

    // Synthetic test rect only when no movie is loaded, so a real SWF
    // does not get painted over.
    if (mcle_swf_is_ready() && !mcle_swf_has_movie()) {
        mcle_swf_draw_test_rect(vw, vh);
    }

    mcle_metal_frame_end();
}

extern "C" void mcle_render_shutdown(void) {
    g_ready.store(false, std::memory_order_release);
    g_pipeline = nil;
}
