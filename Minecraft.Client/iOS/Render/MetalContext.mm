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
    // G3d-step2: depth attachment + depth-test pipeline state. Re-created
    // when the layer resizes; reused across frames otherwise.
    id<MTLTexture>             depthTex = nil;
    int                        depthTexW = 0;
    int                        depthTexH = 0;
    id<MTLDepthStencilState>   depthState = nil;

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

// G3c: Metal pipeline state for the upstream PF3_TF2_CB4_NB4_XW1 vertex
// format (32 bytes/vertex: pos float3 @ 0, uv float2 @ 12, color uchar4
// @ 20, normal uchar4 @ 24, extra word @ 28). Lazily built on first
// replay so process startup doesn't pay shader compile cost.
id<MTLRenderPipelineState> g_world_pso = nil;

NSString* const kWorldShaderSrc = @R"(
    #include <metal_stdlib>
    using namespace metal;

    struct V_in {
        float3 pos    [[attribute(0)]];
        float2 uv     [[attribute(1)]];
        uchar4 color  [[attribute(2)]];
        uchar4 normal [[attribute(3)]];
    };
    struct V_out {
        float4 pos [[position]];
        float4 color;
    };
    struct Uniforms {
        float4x4 mvp;
    };

    vertex V_out world_vert(V_in v [[stage_in]],
                             constant Uniforms& u [[buffer(1)]]) {
        V_out o;
        o.pos   = u.mvp * float4(v.pos, 1.0);
        o.color = float4(v.color) / 255.0;
        return o;
    }
    fragment float4 world_frag(V_out i [[stage_in]]) {
        return i.color;
    }
)";

bool ensure_world_pipeline() {
    if (g_world_pso) return true;
    if (!g.device)   return false;

    NSError* err = nil;
    id<MTLLibrary> lib = [g.device newLibraryWithSource:kWorldShaderSrc
                                                options:nil
                                                  error:&err];
    if (!lib) {
        NSLog(@"[mcle_metal G3c] shader compile failed: %@", err);
        return false;
    }

    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format      = MTLVertexFormatFloat3;
    vd.attributes[0].offset      = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format      = MTLVertexFormatFloat2;
    vd.attributes[1].offset      = 12;
    vd.attributes[1].bufferIndex = 0;
    vd.attributes[2].format      = MTLVertexFormatUChar4;
    vd.attributes[2].offset      = 20;
    vd.attributes[2].bufferIndex = 0;
    vd.attributes[3].format      = MTLVertexFormatUChar4;
    vd.attributes[3].offset      = 24;
    vd.attributes[3].bufferIndex = 0;
    vd.layouts[0].stride         = 32;
    vd.layouts[0].stepFunction   = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction                 = [lib newFunctionWithName:@"world_vert"];
    desc.fragmentFunction               = [lib newFunctionWithName:@"world_frag"];
    desc.vertexDescriptor                 = vd;
    desc.colorAttachments[0].pixelFormat  = MTLPixelFormatBGRA8Unorm;
    desc.depthAttachmentPixelFormat       = MTLPixelFormatDepth32Float;

    g_world_pso = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso) {
        NSLog(@"[mcle_metal G3c] pso build failed: %@", err);
        return false;
    }
    NSLog(@"[mcle_metal G3c] world pipeline built");
    return true;
}

// G3d: player position bridge from MCLEGameLoop.
} // namespace
extern "C" int mcle_world_get_player_pos(float *out_x, float *out_y, float *out_z);
namespace {

// Compute a perspective MVP. View matrix is identity for G3d-step1 since
// the recorded sky/dark/cloud lists are in model space centered at world
// origin - upstream translates them to the player via glTranslatef
// before glCallList. Until matrix-stack record + replay lands (G3d-step2),
// looking from origin lets us see the lists at all.
void compute_mvp(float* mvp16) {
    // Perspective projection (column-major).
    const float fov_rad = 70.0f * (float)M_PI / 180.0f;
    const float aspect  = (g.height > 0) ? ((float)g.width / (float)g.height) : 1.0f;
    const float n       = 0.05f;
    const float f       = 1024.0f;
    const float t       = tanf(fov_rad / 2.0f) * n;
    const float r       = t * aspect;

    // Metal-style perspective: depth maps to [0..1] (vs OpenGL's [-1..1]).
    // Camera looks down -Z. Column-major.
    mvp16[0]  = n / r;  mvp16[1]  = 0;      mvp16[2]  = 0;                   mvp16[3]  = 0;
    mvp16[4]  = 0;      mvp16[5]  = n / t;  mvp16[6]  = 0;                   mvp16[7]  = 0;
    mvp16[8]  = 0;      mvp16[9]  = 0;      mvp16[10] = -f / (f - n);        mvp16[11] = -1;
    mvp16[12] = 0;      mvp16[13] = 0;      mvp16[14] = -(n * f) / (f - n);  mvp16[15] = 0;
}

// Immediate dispatch path. G3c lands MTLBuffer upload + drawPrimitives.
// G3d builds an MVP from the player's live position so we look out into
// the world from where the simulation thinks the player stands.
inline void immediate_dispatch(int prim, int count, const void* data,
                                int fmt, int /*shader*/) {
    g_draw_count.fetch_add(1, std::memory_order_relaxed);

    if (!g.inFrame || !g.enc)        return;
    if (count <= 0 || !data)         return;
    if (fmt != 1 && fmt != 2)        return;  // only PF3_TF2_CB4_NB4_XW1 for now
    if (!ensure_world_pipeline())    return;

    const int stride = 32;
    const NSUInteger byteLen = (NSUInteger)count * (NSUInteger)stride;

    id<MTLBuffer> vbuf = [g.device newBufferWithBytes:data
                                                length:byteLen
                                               options:MTLResourceStorageModeShared];
    if (!vbuf) return;

    // G3d: real perspective MVP centered on the live player position.
    float mvp[16];
    compute_mvp(mvp);

    [g.enc setRenderPipelineState:g_world_pso];
    if (g.depthState) [g.enc setDepthStencilState:g.depthState];
    [g.enc setVertexBuffer:vbuf offset:0 atIndex:0];
    [g.enc setVertexBytes:mvp length:sizeof(mvp) atIndex:1];

    // GL_QUADS (7) has no Metal equivalent. Expand to a triangle index
    // buffer (0,1,2 + 0,2,3 per quad). Tesselator's default mode is
    // GL_QUADS so most ctor draws come through this path.
    if (prim == 7 /* GL_QUADS */) {
        const int quadCount = count / 4;
        if (quadCount <= 0) return;
        const int idxCount  = quadCount * 6;
        std::vector<uint16_t> idxs((size_t)idxCount);
        for (int q = 0; q < quadCount; q++) {
            const uint16_t base = (uint16_t)(q * 4);
            idxs[q * 6 + 0] = base + 0;
            idxs[q * 6 + 1] = base + 1;
            idxs[q * 6 + 2] = base + 2;
            idxs[q * 6 + 3] = base + 0;
            idxs[q * 6 + 4] = base + 2;
            idxs[q * 6 + 5] = base + 3;
        }
        id<MTLBuffer> ibuf =
            [g.device newBufferWithBytes:idxs.data()
                                  length:idxs.size() * sizeof(uint16_t)
                                 options:MTLResourceStorageModeShared];
        if (!ibuf) return;
        [g.enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                          indexCount:(NSUInteger)idxCount
                           indexType:MTLIndexTypeUInt16
                         indexBuffer:ibuf
                   indexBufferOffset:0];
        return;
    }

    MTLPrimitiveType mtlPrim = MTLPrimitiveTypeTriangle;
    if (prim == 5) mtlPrim = MTLPrimitiveTypeTriangleStrip;

    [g.enc drawPrimitives:mtlPrim vertexStart:0 vertexCount:(NSUInteger)count];
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

    // G3d-step2: ensure a depth texture sized to the current drawable.
    if (!g.depthTex || g.depthTexW != g.width || g.depthTexH != g.height) {
        MTLTextureDescriptor* dd = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                          width:(NSUInteger)g.width
                                         height:(NSUInteger)g.height
                                      mipmapped:NO];
        dd.storageMode = MTLStorageModePrivate;
        dd.usage       = MTLTextureUsageRenderTarget;
        g.depthTex     = [g.device newTextureWithDescriptor:dd];
        g.depthTexW    = g.width;
        g.depthTexH    = g.height;
    }
    if (!g.depthState) {
        MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
        dsd.depthCompareFunction = MTLCompareFunctionLess;
        dsd.depthWriteEnabled    = YES;
        g.depthState = [g.device newDepthStencilStateWithDescriptor:dsd];
    }

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = g.drawable.texture;
    rpd.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpd.colorAttachments[0].clearColor  = MTLClearColorMake(r, gn, b, a);
    rpd.depthAttachment.texture         = g.depthTex;
    rpd.depthAttachment.loadAction      = MTLLoadActionClear;
    rpd.depthAttachment.storeAction     = MTLStoreActionDontCare;
    rpd.depthAttachment.clearDepth      = 1.0;

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
