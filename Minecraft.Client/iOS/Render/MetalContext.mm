#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

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

// G3a: legacy GL display-list bridge. Upstream LevelRenderer::LevelRenderer
// builds geometry once via glNewList(id, GL_COMPILE) + Tesselator -> end()
// recording, then per-frame replays it via glCallList(id). On real consoles
// the GPU driver records & replays. On iOS we record DrawVertices payloads
// in CPU memory and replay them through our Metal hook.
//
// Until G3-step3 lands real Metal upload + draw, replay just bumps the
// draw counter so we can verify per-frame dispatch in the tick log.
struct DrawCmd {
    int                  prim;
    int                  count;
    int                  fmt;
    int                  shader;
    std::vector<uint8_t> data;
};

struct DisplayList {
    std::vector<DrawCmd> draws;
};

std::unordered_map<int, DisplayList> g_lists;
int                                   g_recording_list = 0;
std::atomic<int>                      g_next_list_id{1};

// Per-vertex stride in bytes. Tesselator's _array stores 8 ints / 32 bytes
// per vertex for the PF3_TF2_CB4_NB4_XW1 layout (see Tesselator::end's
// pColData += 8 stride). Other formats are guesses until we exercise them.
inline int vertex_stride(int fmt) {
    switch (fmt) {
        case 1: /* VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1        */ return 32;
        case 2: /* VERTEX_TYPE_PF3_TF2_CB4_NB4_XW1_TEXGEN */ return 32;
        case 3: /* VERTEX_TYPE_PS3_TS2_CS1                */ return 12;
        case 4: /* VERTEX_TYPE_COMPRESSED                 */ return 16;
        default:                                              return 32;
    }
}

// Immediate dispatch path. G3-step1 just bumps the counter so tick log
// reflects per-frame replay volume; G3-step3 lands the real MTLBuffer
// upload + drawPrimitives here.
inline void immediate_dispatch(int /*prim*/, int /*count*/,
                                const void* /*data*/, int /*fmt*/, int /*shader*/) {
    g_draw_count.fetch_add(1, std::memory_order_relaxed);
}

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

// G2a: Tesselator -> Metal hook. G3a: routes into the display-list
// recorder when glNewList is active; otherwise dispatches immediately.
// Counter bumps only on the immediate path so per-frame tick logs reflect
// actual draws, not the one-shot ctor recording.
extern "C" void mcle_metal_draw_vertices(int prim, int count,
                                          const void* data,
                                          int fmt, int shader) {
    if (g_recording_list != 0) {
        DrawCmd cmd;
        cmd.prim   = prim;
        cmd.count  = count;
        cmd.fmt    = fmt;
        cmd.shader = shader;
        const int sz = count * vertex_stride(fmt);
        const uint8_t* p = static_cast<const uint8_t*>(data);
        cmd.data.assign(p, p + sz);
        g_lists[g_recording_list].draws.push_back(std::move(cmd));
        return;
    }
    immediate_dispatch(prim, count, data, fmt, shader);
}

extern "C" unsigned long long mcle_metal_draw_count(void) {
    return g_draw_count.load(std::memory_order_relaxed);
}

// G3a: display-list bridge entry points called by probe_stub.cpp's
// glGenLists / glNewList / glEndList / glCallList / glDeleteLists.
extern "C" int mcle_glbridge_gen_lists(int range) {
    return g_next_list_id.fetch_add(range, std::memory_order_relaxed);
}

extern "C" void mcle_glbridge_begin_list(int id, int /*mode*/) {
    g_recording_list = id;
    g_lists[id] = DisplayList();
}

extern "C" void mcle_glbridge_end_list(void) {
    g_recording_list = 0;
}

extern "C" void mcle_glbridge_call_list(int id) {
    auto it = g_lists.find(id);
    if (it == g_lists.end()) return;
    for (const auto& cmd : it->second.draws) {
        immediate_dispatch(cmd.prim, cmd.count, cmd.data.data(),
                           cmd.fmt, cmd.shader);
    }
}

extern "C" void mcle_glbridge_release_lists(int id, int range) {
    for (int i = 0; i < range; i++) g_lists.erase(id + i);
}

// Diagnostics: number of recorded display lists (so we can correlate with
// the 191 ctor draws we saw before recording was wired).
extern "C" unsigned long long mcle_glbridge_list_count(void) {
    return static_cast<unsigned long long>(g_lists.size());
}

// G3b TEMP: replay every recorded list back through the immediate
// dispatch path. Used before the upstream renderSky/renderClouds calls
// drive glCallList naturally - parity comes once setLevel + level state
// is wired (G3e). Remove this helper at that point.
extern "C" void mcle_glbridge_replay_all_lists(void) {
    for (const auto& kv : g_lists) {
        for (const auto& cmd : kv.second.draws) {
            immediate_dispatch(cmd.prim, cmd.count, cmd.data.data(),
                               cmd.fmt, cmd.shader);
        }
    }
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
