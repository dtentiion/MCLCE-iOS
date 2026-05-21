#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <mach/mach_time.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    id<MTLDepthStencilState>   depthState        = nil;  // less + write
    id<MTLDepthStencilState>   depthStateNoTest  = nil;  // always + no write
    id<MTLDepthStencilState>   depthStateNoWrite = nil;  // less + no write (sky dome)

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
    // G5: chunk's world translation (xRenderOffs/yRenderOffs/zRenderOffs)
    // captured at record time. Applied as a translate on top of current
    // modelview at replay so geometry tracks the camera correctly.
    float                translate[3] = {0, 0, 0};
    bool                 hasTranslate = false;
    // G5: also capture bound texture so chunks sample terrain.png instead of
    // whatever happens to be bound at replay time.
    unsigned int         texId = 0;
};

struct DisplayList {
    std::vector<DrawCmd> draws;
};

// Shared by every thread that compiles or replays a display list.
// Workers spawned by LevelRenderer::staticCtor build chunk meshes into
// here in parallel; the main thread also writes during sky/cloud list
// capture and reads when replaying. shared_mutex so the render
// thread's frequent reads run in parallel and writers only block when
// they actually need to insert/erase.
std::unordered_map<int, DisplayList> g_lists;
std::shared_mutex                     g_lists_mu;
thread_local int                      g_recording_list = 0;
// Each compiling thread buffers its DrawCmds locally and splices them
// into g_lists in one shot at glEndList. Avoids hammering g_lists_mu
// on every per-tile draw - per-chunk locks instead of per-draw.
thread_local DisplayList              g_recording_buffer;
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
id<MTLRenderPipelineState> g_world_pso        = nil;

// G5: pipeline state for VERTEX_TYPE_COMPRESSED (fmt=4, 16 bytes/vertex):
// short3 pos*1024 @ 0, short packed_5_6_5_color @ 6, short2 uv*8192 @ 8,
// short2 lightUV @ 12. This is what chunk rebuild emits when
// useCompactVertices(true) is set in Tesselator.
id<MTLRenderPipelineState> g_world_pso_compact = nil;

// Blending variants. Same vertex layouts and shaders as above, but with
// the color attachment configured for srcAlpha/oneMinusSrcAlpha blending.
// Used when glEnable(GL_BLEND) is in effect (sky / sun / moon / cloud
// passes). For terrain (alpha=1 pixels), srcAlpha-over blending degenerates
// to plain replace, so toggling between these two pipelines is safe.
id<MTLRenderPipelineState> g_world_pso_blend         = nil;
id<MTLRenderPipelineState> g_world_pso_compact_blend = nil;
// Additive blending variant (srcAlpha, ONE). Upstream uses this for the
// sun and stars - black border pixels (rgb=0) contribute nothing to dst,
// only the bright sun disc adds color. Without this we'd see the sun's
// black border replacing the sky.
id<MTLRenderPipelineState> g_world_pso_additive         = nil;
id<MTLRenderPipelineState> g_world_pso_compact_additive = nil;

// No-cutout variants for the sun/moon/sunrise pass. renderSky's
// glDisable(GL_ALPHA_TEST) is honored by switching to a pipeline whose
// fragment shader skips the discard - this lets the semi-transparent
// moon body pixels survive into the additive blend.
id<MTLRenderPipelineState> g_world_pso_blend_nocut    = nil;
id<MTLRenderPipelineState> g_world_pso_additive_nocut = nil;

// Depth-only variants. renderAdvancedClouds runs a two-pass loop where
// pass 0 calls glBlendFunc(GL_ZERO, GL_ONE) - source contributes nothing,
// destination preserved - so color buffer is untouched while depth still
// gets the cloud geometry. Without this variant the over-blend pipeline
// runs for pass 0 too and we end up drawing clouds twice per frame, which
// is what produced the visible flat checkered cloud sheet on top of the
// proper 3D cube layer.
id<MTLRenderPipelineState> g_world_pso_depth_only         = nil;
id<MTLRenderPipelineState> g_world_pso_compact_depth_only = nil;

// Cached encoder state. Reset to "unknown" at frame begin so the first
// dispatch issues the actual setCullMode / setFrontFacingWinding call.
MTLCullMode g_lastCullMode = (MTLCullMode)-1;
MTLWinding  g_lastWinding  = (MTLWinding)-1;
// glEnable/glDisable(GL_DEPTH_TEST) writes here. Default: enabled (matches
// upstream initial GL state). Reset to true at frame begin.
bool        g_depth_test_enabled = true;
// glDepthMask writes here. Default: enabled (matches upstream initial GL
// state). renderSky calls glDepthMask(false) before drawing the sky dome
// so the dome doesn't write depth - sun/moon then pass the depth test
// against terrain only and aren't occluded by the dome. Reset to true at
// frame begin.
bool        g_depth_write_enabled = true;
id<MTLDepthStencilState> g_lastDepthState = nil;

// glEnable/glDisable(GL_ALPHA_TEST) writes here. Default on - chunks
// rely on the cutout for foliage / flowers. renderSky's
// glDisable(GL_ALPHA_TEST) before sun/moon flips it off so their
// semi-transparent texture bodies aren't discarded, then re-enables
// for subsequent chunk passes. Threshold 0.1 matches upstream's
// glAlphaFunc(GL_GREATER, 0.1).
bool g_alpha_test_enabled = true;

// glEnable/glDisable(GL_TEXTURE_2D) writes here. Default on. renderSky
// disables texturing for the sky dome (skyList), sunrise gradient,
// starList, and darkList - those draws are vertex-color only. Without
// honoring this flag the fragment shader still samples whatever texture
// is bound at the time. skyList has no per-vertex UVs (only t->vertex,
// not t->vertexUV), so undefined UV bytes get interpolated against
// whichever texture is current - producing a chaotic textured slab
// stuck around the player. When this flag is off, immediate_dispatch
// binds the 1x1 white texture so sample * vertex_color collapses to
// vertex_color (parity with fixed-function GL).
bool g_texture_2d_enabled = true;

// Fog state. Mirrors upstream GameRenderer::setupFog which calls
// glFog(GL_FOG_COLOR), glFogi(GL_FOG_MODE), glFogf(GL_FOG_START/END).
// Default off (matches GL initial state). Fragment shader applies
// linear fog over eye-space depth.
bool  g_fog_enabled = false;
float g_fog_r = 0.0f, g_fog_g = 0.0f, g_fog_b = 0.0f;
float g_fog_start = 0.0f;
float g_fog_end   = 1.0f;

// glEnable/glDisable(GL_BLEND) tracking. Default off (matches upstream
// initial GL state). When on, immediate_dispatch picks the blending
// pipeline variant (srcAlpha/oneMinusSrcAlpha) so transparent edges of
// sky/sun/moon/cloud sprites blend with the framebuffer instead of
// writing rgba=0 directly. Reset to off at frame begin.
bool g_blend_enabled  = false;
// Blend func tracking: 0 = srcAlpha/oneMinusSrcAlpha (default over),
// 1 = srcAlpha/ONE (additive, used by sun + stars),
// 2 = GL_ZERO/GL_ONE (depth-only, cloud pass 0). Set by glBlendFunc.
int  g_blend_func_mode = 0;
id<MTLRenderPipelineState> g_lastWorldPso = nil;

extern "C" void mcle_glbridge_set_depth_test(int enabled) {
    g_depth_test_enabled = (enabled != 0);
}

extern "C" void mcle_glbridge_set_texture_2d(int enabled) {
    g_texture_2d_enabled = (enabled != 0);
}

extern "C" void mcle_glbridge_set_depth_write(int enabled) {
    g_depth_write_enabled = (enabled != 0);
}

extern "C" void mcle_glbridge_set_fog_enabled(int enabled) {
    g_fog_enabled = (enabled != 0);
}
extern "C" void mcle_glbridge_set_fog_color(float r, float gr, float b, float /*a*/) {
    g_fog_r = r; g_fog_g = gr; g_fog_b = b;
}
extern "C" void mcle_glbridge_set_fog_start(float v) { g_fog_start = v; }
extern "C" void mcle_glbridge_set_fog_end(float v)   { g_fog_end   = v; }

// Lightmap bridge. MCLEGameLoop computes each of the 256 entries from
// upstream Level::getSkyDarken + dimension->brightnessRamp and writes
// them via mcle_lightmap_set_entry, then calls mcle_lightmap_upload to
// push the CPU buffer to the GPU texture. Pattern mirrors upstream's
// GameRenderer::updateLightTexture (GameRenderer.cpp:849-946) which
// fills a 256-int buffer then calls Textures::replaceTextureDirect.
//
// Forward-declare the two pieces of state these functions touch since
// the actual definitions live further down with the other texture
// globals (and the definitions are in the right place for init order).
extern uint8_t        g_lightmap_pixels[16*16*4];
extern id<MTLTexture> g_lightmap_texture;
extern "C" void mcle_lightmap_set_entry(int idx, float r, float g, float b) {
    if (idx < 0 || idx >= 256) return;
    int ir = (int)(r * 255.0f); if (ir < 0) ir = 0; if (ir > 255) ir = 255;
    int ig = (int)(g * 255.0f); if (ig < 0) ig = 0; if (ig > 255) ig = 255;
    int ib = (int)(b * 255.0f); if (ib < 0) ib = 0; if (ib > 255) ib = 255;
    g_lightmap_pixels[idx*4+0] = (uint8_t)ir;
    g_lightmap_pixels[idx*4+1] = (uint8_t)ig;
    g_lightmap_pixels[idx*4+2] = (uint8_t)ib;
    g_lightmap_pixels[idx*4+3] = 255;
}
extern "C" void mcle_lightmap_upload(void) {
    if (!g_lightmap_texture) return;
    [g_lightmap_texture replaceRegion:MTLRegionMake2D(0, 0, 16, 16)
                          mipmapLevel:0
                            withBytes:g_lightmap_pixels
                          bytesPerRow:16*4];
}

extern "C" void mcle_glbridge_set_alpha_test(int enabled) {
    g_alpha_test_enabled = (enabled != 0);
    // Diagnostic: confirm upstream's glEnable/glDisable(GL_ALPHA_TEST)
    // is actually reaching the shim. If renderSky's glDisable(...) call
    // never logs here, the shim isn't being wired up correctly.
    {
        static int s_count = 0;
        if (s_count < 30) {
            extern int mcle_log_msg(const char *);
            char buf[80];
            snprintf(buf, sizeof(buf), "ATEST_TOGGLE enabled=%d", enabled);
            mcle_log_msg(buf);
            s_count++;
        }
    }
}

extern "C" void mcle_glbridge_set_blend_enabled(int enabled) {
    g_blend_enabled = (enabled != 0);
}

// (Diagnostic getter mcle_glbridge_get_modelview lives below the
// anonymous-namespace block where g_modelview_stack is declared.)

// glBlendFunc routing: pick additive (srcAlpha, ONE) vs over
// (srcAlpha, oneMinusSrcAlpha) based on the requested factors.
// GL_ZERO=0, GL_ONE=1, GL_SRC_ALPHA=0x0302, GL_ONE_MINUS_SRC_ALPHA=0x0303.
extern "C" void mcle_glbridge_set_blend_func(int src, int dst) {
    if (src == 0x0302 /*GL_SRC_ALPHA*/ && dst == 0x0001 /*GL_ONE*/) {
        g_blend_func_mode = 1; // additive (sun, stars)
    } else if (src == 0x0000 /*GL_ZERO*/ && dst == 0x0001 /*GL_ONE*/) {
        g_blend_func_mode = 2; // depth-only (renderAdvancedClouds pass 0)
    } else {
        g_blend_func_mode = 0; // over (clouds, vignette, default)
    }
}

// G4: 1x1 white default texture + linear sampler. Bound on every world
// draw so the fragment shader's texture sample stays well-defined even
// for currently-untextured paths (sky / dark / star / sunset gradient).
// Real textures from glBindTexture override this in G4-step2.
id<MTLTexture>      g_default_texture = nil;
id<MTLSamplerState> g_default_sampler  = nil;
// Lightmap is a 16x16 lookup table sampled per-fragment via the
// interpolated tex2 UV. We want LINEAR filtering on this one (not
// nearest like the terrain atlas) so a fragment whose UV lands
// between two cells gets a smooth mix, producing soft shadow
// gradients across block faces instead of sharp per-cell edges.
id<MTLSamplerState> g_lightmap_sampler = nil;
id<MTLTexture>      g_current_texture = nil;

// 16x16 lightmap. Upstream rebuilds this every frame via
// GameRenderer::updateLightTexture (GameRenderer.cpp:849-946) from
// Level::getSkyDarken + dimension->brightnessRamp. Indexed by
// (skyLevel << 4) | blockLevel. Chunk vertices reference it through
// the secondary tex2 UV. Init to all-white as a no-op fallback before
// the first real update.
id<MTLTexture> g_lightmap_texture       = nil;
uint8_t        g_lightmap_pixels[16*16*4];
// 1x1 white bound at fragment unit 1 for fmt=1 sky/sun/moon/cloud
// dispatches. Their vertex tex2 is uninitialized so we hide the
// lightmap entirely for those passes by sampling a constant 1.0.
id<MTLTexture> g_lightmap_white_texture = nil;

// G4-step2: GL texture object registry. glGenTextures hands out IDs;
// glBindTexture(target, id) sets the active texture; glTexImage2D
// uploads data to the bound texture. IDs are dense small integers
// matching legacy GL semantics.
std::unordered_map<unsigned int, id<MTLTexture>> g_gl_textures;
std::atomic<unsigned int>                         g_next_tex_id{1};
unsigned int                                      g_bound_tex_id = 0;

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
        float2 uv;
        float2 lightmapUV;
        float  fogDist;  // eye-space depth for distance fog
    };
    struct MVPUniforms {
        float4x4 mvp;
        // Texture matrix (active GL_TEXTURE stack top). Multiplied into
        // per-vertex UV so renderAdvancedClouds's per-cell UV-translate
        // (and any future water/lava animation) actually moves the
        // sampled coords. Identity matrix = no-op = parity with shaders
        // that don't touch it.
        float4x4 texMatrix;
    };
    struct ColorUniforms {
        float4 currentColor;
    };
    // FogParams.fogColor.w doubles as the fog-enabled flag (1.0 = on,
    // 0.0 = off). Parity with upstream's GL_FOG state: when fogEnabled
    // is 0 the mix factor collapses to 0 and the fragment passes through
    // unchanged.
    struct FogParams {
        float4 fogColor;        // rgb + enabled
        float4 fogRange;        // start in x, end in y, modeLinear in z
        float4 modelviewRow2;   // row 2 of modelview for eye-z calc
    };

    vertex V_out world_vert(V_in v [[stage_in]],
                             constant MVPUniforms&   m [[buffer(1)]],
                             constant ColorUniforms& c [[buffer(2)]],
                             constant FogParams&     f [[buffer(3)]]) {
        V_out o;
        o.pos   = m.mvp * float4(v.pos, 1.0);
        // Tesselator::color packs as (r<<24)|(g<<16)|(b<<8)|a. On
        // little-endian that stores ABGR in memory order, so uchar4
        // reads as (A,B,G,R). Swizzle .wzyx to get RGBA. Without this
        // the sunrise gradient fan vertices end up with R written to
        // alpha and A written to red - the fan ends up B-dominant with
        // high alpha and renders as the bright blue carpet visible
        // opposite the sun at sunrise/sunset.
        float4 vc = float4(v.color).wzyx / 255.0;
        o.color = vc * c.currentColor;
        // Apply texture matrix (identity by default, translates per
        // tile cell when renderAdvancedClouds is active).
        float4 tcoord = m.texMatrix * float4(v.uv, 0.0, 1.0);
        o.uv    = tcoord.xy;
        // fmt=1 vertices (sky/sun/moon/clouds) have no real lightmap UV.
        // Dispatch binds a 1x1 white lightmap for those passes so the
        // sample is a no-op. Routing the UV to (0.5, 0.5) keeps it inside
        // the texture for any sampler clamp mode.
        o.lightmapUV = float2(0.5, 0.5);
        o.fogDist = abs(dot(f.modelviewRow2, float4(v.pos, 1.0)));
        return o;
    }

    // Apply linear distance fog. factor goes 0 at fogStart to 1 at
    // fogEnd, then mixes output toward fog color. fogColor.w gates the
    // effect off entirely when GL_FOG is disabled.
    float3 apply_fog(float3 rgb, float fogDist, constant FogParams& f) {
        float t = clamp((fogDist - f.fogRange.x) /
                        (f.fogRange.y - f.fogRange.x), 0.0, 1.0);
        return mix(rgb, f.fogColor.rgb, t * f.fogColor.w);
    }

    fragment float4 world_frag(V_out i           [[stage_in]],
                                texture2d<float>     tex      [[texture(0)]],
                                texture2d<float>     lightmap [[texture(1)]],
                                sampler              texSamp  [[sampler(0)]],
                                sampler              lmSamp   [[sampler(1)]],
                                constant FogParams&  f        [[buffer(3)]]) {
        float4 t = tex.sample(texSamp, i.uv);
        if (t.a < 0.1) discard_fragment();
        float4 outc = i.color * t;
        // Lightmap sampled with the dedicated linear-filter sampler so
        // fragments whose UV lands between two cells get a smooth mix.
        // Gives soft shadow gradients across block faces.
        float3 lm = lightmap.sample(lmSamp, i.lightmapUV).rgb;
        outc.rgb *= lm;
        outc.rgb = apply_fog(outc.rgb, i.fogDist, f);
        return outc;
    }

    fragment float4 world_frag_nocut(V_out i           [[stage_in]],
                                      texture2d<float>     tex      [[texture(0)]],
                                      texture2d<float>     lightmap [[texture(1)]],
                                      sampler              texSamp  [[sampler(0)]],
                                      sampler              lmSamp   [[sampler(1)]],
                                      constant FogParams&  f        [[buffer(3)]]) {
        float4 t = tex.sample(texSamp, i.uv);
        float4 outc = i.color * t;
        float3 lm = lightmap.sample(lmSamp, i.lightmapUV).rgb;
        outc.rgb *= lm;
        outc.rgb = apply_fog(outc.rgb, i.fogDist, f);
        return outc;
    }
)";

// G5: shader for fmt=4 compact vertex format. Position is int16x3 scaled
// by 1024, primary UV is int16x2 scaled by 8192. Packed color is signed
// int16 with bits 15-11=A 10-5=R 4-0=G (per Tesselator.cpp:774). UVs go
// through to fragment shader for atlas sampling.
NSString* const kWorldShaderSrcCompact = @R"(
    #include <metal_stdlib>
    using namespace metal;

    struct V_in_c {
        short3 pos    [[attribute(0)]];
        short  pcol   [[attribute(1)]];
        short2 uv     [[attribute(2)]];
        short2 tex2   [[attribute(3)]];
    };
    struct V_out {
        float4 pos [[position]];
        float4 color;
        float2 uv;
        float2 lightmapUV;
        float  fogDist;
    };
    struct MVPUniforms   { float4x4 mvp; float4x4 texMatrix; };
    struct ColorUniforms { float4 currentColor; };
    struct FogParams {
        float4 fogColor;
        float4 fogRange;
        float4 modelviewRow2;
    };

    vertex V_out world_vert_compact(V_in_c v [[stage_in]],
                                     constant MVPUniforms&   m [[buffer(1)]],
                                     constant ColorUniforms& c [[buffer(2)]],
                                     constant FogParams&     f [[buffer(3)]]) {
        V_out o;
        float3 pos = float3(v.pos) / 1024.0;
        float2 uv  = float2(v.uv) / 8192.0;
        o.pos   = m.mvp * float4(pos, 1.0);
        // Apply texture matrix to the decoded UV (no-op identity for
        // chunks, translates for cloud cube cells).
        float4 tcoord = m.texMatrix * float4(uv, 0.0, 1.0);
        uv = tcoord.xy;
        int packed = int(v.pcol) + 32768;
        float src_r = float((packed >> 11) & 0x1F) / 31.0;
        float src_g = float((packed >>  5) & 0x3F) / 63.0;
        float src_b = float((packed      ) & 0x1F) / 31.0;
        o.color = float4(src_r, src_g, src_b, 1.0) * c.currentColor;
        o.uv    = uv;
        // Lightmap UV. The chunk vertex's tex2 carries two pre-scaled
        // lightmap UV components: tex2.x holds blockLevel * 16 and
        // tex2.y holds skyLevel * 16 (so level 15 == 240, level 0 ==
        // 0). The +8 nudges each component to the centre of its 1/16
        // wide cell so nearest sampling lands cleanly on the right
        // entry. Mirrors upstream's PSVita compact-vertex path
        // (Tesselator.cpp:943-947) which writes the same scaled pair.
        float u = (float(v.tex2.x) + 8.0) / 256.0;
        float vlm = (float(v.tex2.y) + 8.0) / 256.0;
        o.lightmapUV = float2(u, vlm);
        o.fogDist = abs(dot(f.modelviewRow2, float4(pos, 1.0)));
        return o;
    }
)";

bool ensure_world_pipeline_compact() {
    if (g_world_pso_compact) return true;
    if (!g.device)           return false;

    NSError* err = nil;
    id<MTLLibrary> lib = [g.device newLibraryWithSource:kWorldShaderSrcCompact
                                                options:nil
                                                  error:&err];
    if (!lib) {
        NSLog(@"[mcle_metal G5c] compact shader compile failed: %@", err);
        return false;
    }

    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format      = MTLVertexFormatShort3;
    vd.attributes[0].offset      = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format      = MTLVertexFormatShort;
    vd.attributes[1].offset      = 6;
    vd.attributes[1].bufferIndex = 0;
    vd.attributes[2].format      = MTLVertexFormatShort2;
    vd.attributes[2].offset      = 8;
    vd.attributes[2].bufferIndex = 0;
    vd.attributes[3].format      = MTLVertexFormatShort2;
    vd.attributes[3].offset      = 12;
    vd.attributes[3].bufferIndex = 0;
    vd.layouts[0].stride         = 16;
    vd.layouts[0].stepFunction   = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction                  = [lib newFunctionWithName:@"world_vert_compact"];
    // Reuse world_frag from the main shader - it just samples the bound
    // texture and modulates with vertex color.
    {
        NSError* ferr = nil;
        id<MTLLibrary> mainLib = [g.device newLibraryWithSource:kWorldShaderSrc
                                                        options:nil
                                                          error:&ferr];
        if (!mainLib) {
            NSLog(@"[mcle_metal G5c] could not reload main lib for fragment shader: %@", ferr);
            return false;
        }
        desc.fragmentFunction = [mainLib newFunctionWithName:@"world_frag"];
    }
    desc.vertexDescriptor                 = vd;
    desc.colorAttachments[0].pixelFormat  = MTLPixelFormatBGRA8Unorm;
    desc.depthAttachmentPixelFormat       = MTLPixelFormatDepth32Float;

    g_world_pso_compact = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_compact) {
        NSLog(@"[mcle_metal G5c] compact pso build failed: %@", err);
        return false;
    }

    // Blending variant: same vertex/fragment/layout, color attachment now
    // does srcAlpha/oneMinusSrcAlpha blending. Picked when GL_BLEND is on.
    desc.colorAttachments[0].blendingEnabled             = YES;
    desc.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    g_world_pso_compact_blend = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_compact_blend) {
        NSLog(@"[mcle_metal G5c] compact blend pso build failed: %@", err);
        return false;
    }

    // Additive variant: GL_SRC_ALPHA, GL_ONE. Sun + stars use this so
    // black border pixels contribute nothing to dst (no black box around sun).
    desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    g_world_pso_compact_additive = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_compact_additive) {
        NSLog(@"[mcle_metal G5c] compact additive pso build failed: %@", err);
        return false;
    }

    // Depth-only variant: writeMask=none so the color attachment is not
    // touched. renderAdvancedClouds' pass-0 glBlendFunc(GL_ZERO, GL_ONE)
    // routes here.
    desc.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
    g_world_pso_compact_depth_only = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_compact_depth_only) {
        NSLog(@"[mcle_metal G5c] compact depth-only pso build failed: %@", err);
        return false;
    }
    desc.colorAttachments[0].writeMask = MTLColorWriteMaskAll;

    NSLog(@"[mcle_metal G5c] compact pipelines ready (no-blend + blend + additive + depth-only)");
    return true;
}

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

    // Blending variant for sky/sun/moon/clouds. Same shaders + vertex
    // layout, color attachment now does srcAlpha-over-oneMinusSrcAlpha.
    desc.colorAttachments[0].blendingEnabled             = YES;
    desc.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
    desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    g_world_pso_blend = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_blend) {
        NSLog(@"[mcle_metal G3c] blend pso build failed: %@", err);
        return false;
    }

    // Additive variant for sun / stars (srcAlpha, ONE).
    desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    g_world_pso_additive = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_additive) {
        NSLog(@"[mcle_metal G3c] additive pso build failed: %@", err);
        return false;
    }

    // No-cutout additive variant for sun/moon (renderSky disables alpha
    // test before this pass). Same blend factors, but fragment shader is
    // world_frag_nocut which skips the t.a < 0.1 discard - lets
    // semi-transparent moon body pixels survive into the additive blend.
    desc.fragmentFunction = [lib newFunctionWithName:@"world_frag_nocut"];
    g_world_pso_additive_nocut = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_additive_nocut) {
        NSLog(@"[mcle_metal G3c] additive nocut pso build failed: %@", err);
        return false;
    }

    // No-cutout over-blend variant for the sunrise gradient triangle fan
    // (over-blend + alpha test off).
    desc.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    g_world_pso_blend_nocut = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_blend_nocut) {
        NSLog(@"[mcle_metal G3c] blend nocut pso build failed: %@", err);
        return false;
    }

    // Depth-only variant: writeMask=none so the color attachment stays
    // untouched while depth is still written. renderAdvancedClouds pass 0
    // sets glBlendFunc(GL_ZERO, GL_ONE) - a depth pre-pass with zero color
    // contribution. Restore the fragment function to world_frag and the
    // writeMask back to All afterwards so the next pipeline build (none
    // here for fmt=1, but parity for future variants) starts from a
    // known state.
    desc.fragmentFunction = [lib newFunctionWithName:@"world_frag"];
    desc.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
    g_world_pso_depth_only = [g.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (!g_world_pso_depth_only) {
        NSLog(@"[mcle_metal G3c] depth-only pso build failed: %@", err);
        return false;
    }
    desc.colorAttachments[0].writeMask = MTLColorWriteMaskAll;

    // G4: build the 1x1 white default texture + linear sampler.
    if (!g_default_texture) {
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                width:1
                                                               height:1
                                                            mipmapped:NO];
        td.usage       = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        g_default_texture = [g.device newTextureWithDescriptor:td];
        const uint8_t white[4] = { 255, 255, 255, 255 };
        [g_default_texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                              mipmapLevel:0
                                withBytes:white
                              bytesPerRow:4];
    }
    if (!g_default_sampler) {
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        // Minecraft textures are pixel-art; nearest-neighbor preserves the
        // crisp 16x16 pixel grid. Linear would blur block faces.
        sd.minFilter    = MTLSamplerMinMagFilterNearest;
        sd.magFilter    = MTLSamplerMinMagFilterNearest;
        sd.sAddressMode = MTLSamplerAddressModeRepeat;
        sd.tAddressMode = MTLSamplerAddressModeRepeat;
        g_default_sampler = [g.device newSamplerStateWithDescriptor:sd];
    }
    if (!g_lightmap_sampler) {
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter    = MTLSamplerMinMagFilterLinear;
        sd.magFilter    = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        g_lightmap_sampler = [g.device newSamplerStateWithDescriptor:sd];
    }
    // Lightmap: 16x16 RGBA, all-white until first mcle_lightmap_update.
    // Same Tesselator-driven secondary UV that upstream GameRenderer
    // uploads at line 944. The 1x1 white sibling stands in for sky-pass
    // fmt=1 binds where there's no real lightmap UV in the vertex data.
    if (!g_lightmap_texture) {
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                width:16
                                                               height:16
                                                            mipmapped:NO];
        td.usage       = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        g_lightmap_texture = [g.device newTextureWithDescriptor:td];
        for (int i = 0; i < 16*16; i++) {
            g_lightmap_pixels[i*4+0] = 255;
            g_lightmap_pixels[i*4+1] = 255;
            g_lightmap_pixels[i*4+2] = 255;
            g_lightmap_pixels[i*4+3] = 255;
        }
        [g_lightmap_texture replaceRegion:MTLRegionMake2D(0, 0, 16, 16)
                              mipmapLevel:0
                                withBytes:g_lightmap_pixels
                              bytesPerRow:16*4];
    }
    if (!g_lightmap_white_texture) {
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                width:1
                                                               height:1
                                                            mipmapped:NO];
        td.usage       = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        g_lightmap_white_texture = [g.device newTextureWithDescriptor:td];
        const uint8_t white[4] = { 255, 255, 255, 255 };
        [g_lightmap_white_texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                                    mipmapLevel:0
                                      withBytes:white
                                    bytesPerRow:4];
    }
    NSLog(@"[mcle_metal G3c/G4] world pipeline + default texture built");
    return true;
}

// G3d-step3: legacy GL matrix stack. Two stacks (modelview + projection),
// glMatrixMode picks active one. immediate_dispatch sends projection *
// modelview as the MVP uniform.

struct Mat4 { float m[16]; };

inline Mat4 mat_identity() {
    return Mat4{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
}

inline void mat_mul(float* out, const float* a, const float* b) {
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            float s = 0;
            for (int k = 0; k < 4; k++) {
                s += a[i + k * 4] * b[k + j * 4];
            }
            out[i + j * 4] = s;
        }
    }
}

inline void mat_translate(float* m, float x, float y, float z) {
    const float t[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1};
    float r[16];
    mat_mul(r, m, t);
    memcpy(m, r, sizeof(r));
}

inline void mat_rotate(float* m, float angle_deg, float ax, float ay, float az) {
    const float a   = angle_deg * (float)M_PI / 180.0f;
    const float c   = cosf(a);
    const float s   = sinf(a);
    const float n   = sqrtf(ax * ax + ay * ay + az * az);
    if (n > 0.0f) { ax /= n; ay /= n; az /= n; }
    const float omc = 1.0f - c;
    const float r[16] = {
        ax*ax*omc +    c,  ay*ax*omc + az*s,  az*ax*omc - ay*s,  0,
        ax*ay*omc - az*s,  ay*ay*omc +    c,  az*ay*omc + ax*s,  0,
        ax*az*omc + ay*s,  ay*az*omc - ax*s,  az*az*omc +    c,  0,
        0,                 0,                 0,                 1,
    };
    float result[16];
    mat_mul(result, m, r);
    memcpy(m, result, sizeof(result));
}

inline void mat_scale(float* m, float sx, float sy, float sz) {
    for (int i = 0; i < 4; i++) {
        m[i + 0]  *= sx;
        m[i + 4]  *= sy;
        m[i + 8]  *= sz;
    }
}

constexpr int kMatrixModeModelview  = 0;
constexpr int kMatrixModeProjection = 1;
constexpr int kMatrixModeTexture    = 2;
constexpr int kGL_MODELVIEW         = 0x1700;
constexpr int kGL_PROJECTION        = 0x1701;
constexpr int kGL_TEXTURE           = 0x1702;

// Per-thread matrix state. Each worker that compiles a chunk needs its
// own GL fixed-function stacks - upstream's Chunk::rebuild walks the
// modelview/projection/texture stacks the same way the main thread
// does during sky capture. Without thread_local each worker would
// stomp the main thread's stacks.
thread_local int                g_matrix_mode = kMatrixModeModelview;
thread_local std::vector<Mat4>  g_modelview_stack { mat_identity() };
thread_local std::vector<Mat4>  g_projection_stack{ mat_identity() };
// Texture matrix stack. Used by renderAdvancedClouds (the 3D cube
// cloud path) to scroll the cloud UV pattern per frame. Upstream calls
// glMatrixMode(GL_TEXTURE) + glLoadIdentity + glTranslatef to feed each
// cube cell its own UV offset, then resets back when done. Each
// dispatch reads the top of this stack into a vertex-shader uniform
// that multiplies the per-vertex UV.
thread_local std::vector<Mat4>  g_texture_stack  { mat_identity() };

// G3f: GL_CURRENT_COLOR for fixed-function modulate. Vertex color is
// multiplied with this in the world vertex shader so glColor3f /
// glColor4f take effect across replayed display lists. Matches D3D9
// fixed-function modulate semantics (Xbox 360 / PS3) for parity.
float g_current_color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

inline std::vector<Mat4>& current_stack() {
    switch (g_matrix_mode) {
        case kMatrixModeProjection: return g_projection_stack;
        case kMatrixModeTexture:    return g_texture_stack;
        default:                    return g_modelview_stack;
    }
}
inline float* current_matrix_data() { return current_stack().back().m; }

} // namespace

// Diagnostic getter: copies the current modelview matrix into out16. Used
// by MCLEGameLoop's MV_CKPT to confirm the matrix flow before renderSky.
extern "C" void mcle_glbridge_get_modelview(float *out16) {
    if (!out16) return;
    const Mat4 &m = g_modelview_stack.back();
    for (int i = 0; i < 16; i++) out16[i] = m.m[i];
}

// Companion getter for the projection stack. Used by Frustum::calculateFrustum
// (Frustum.cpp:56) which reads both matrices via glGetFloat(GL_PROJECTION_MATRIX)
// / glGetFloat(GL_MODELVIEW_MATRIX) to compute the 6 clip planes. With a
// no-op shim those planes stay zero and FrustumData::cubeInFrustum rejects
// every cube - matters for renderAdvancedClouds which inner-loop-skips on
// every cube otherwise.
extern "C" void mcle_glbridge_get_projection(float *out16) {
    if (!out16) return;
    const Mat4 &m = g_projection_stack.back();
    for (int i = 0; i < 16; i++) out16[i] = m.m[i];
}

// Public matrix bridge - probe_stub.cpp's gl* matrix stubs forward here.
extern "C" void mcle_glbridge_matrix_mode(int mode) {
    switch (mode) {
        case kGL_PROJECTION: g_matrix_mode = kMatrixModeProjection; break;
        case kGL_TEXTURE:    g_matrix_mode = kMatrixModeTexture;    break;
        default:             g_matrix_mode = kMatrixModeModelview;  break;
    }
    // Diagnostic: log the first few GL_TEXTURE switches. If this doesn't
    // fire even once during cloud render, renderAdvancedClouds is exiting
    // the per-cube loop before its glMatrixMode(GL_TEXTURE) call - usually
    // because Frustum::cubeInFrustum is rejecting everything.
    if (mode == kGL_TEXTURE) {
        static int s_texModeHits = 0;
        if (s_texModeHits < 20) {
            extern int mcle_log_msg(const char *);
            char buf[64];
            snprintf(buf, sizeof(buf), "MM_TEXTURE hit=%d", s_texModeHits);
            mcle_log_msg(buf);
            s_texModeHits++;
        }
    }
}
extern "C" void mcle_glbridge_load_identity(void) {
    current_stack().back() = mat_identity();
}
extern "C" void mcle_glbridge_load_matrix(const float* m16) {
    if (!m16) return;
    memcpy(current_matrix_data(), m16, 16 * sizeof(float));
}
extern "C" void mcle_glbridge_mult_matrix(const float* m16) {
    if (!m16) return;
    float r[16];
    mat_mul(r, current_matrix_data(), m16);
    memcpy(current_matrix_data(), r, sizeof(r));
}
extern "C" void mcle_glbridge_push_matrix(void) {
    current_stack().push_back(current_stack().back());
}
extern "C" void mcle_glbridge_pop_matrix(void) {
    if (current_stack().size() > 1) current_stack().pop_back();
}
// G5: remember last translate applied while a list is being recorded, so
// chunk display lists can carry their world position to replay. Per-
// thread because each worker compiles into a different chunk and needs
// its own remembered offset.
thread_local float g_recording_last_translate[3] = {0, 0, 0};
thread_local bool  g_recording_has_translate = false;

extern "C" void mcle_glbridge_translate(float x, float y, float z) {
    mat_translate(current_matrix_data(), x, y, z);
    if (g_recording_list != 0) {
        g_recording_last_translate[0] = x;
        g_recording_last_translate[1] = y;
        g_recording_last_translate[2] = z;
        g_recording_has_translate = true;
    }
}
extern "C" void mcle_glbridge_rotate(float angle, float x, float y, float z) {
    mat_rotate(current_matrix_data(), angle, x, y, z);
}
extern "C" void mcle_glbridge_scale(float x, float y, float z) {
    mat_scale(current_matrix_data(), x, y, z);
}

// G4-step3: PNG decoder bridge (png_decode.mm) and texture-from-path
// loader. Mirrors upstream's per-platform pattern - on Xbox/PS3 the
// platform's render API decodes the PNG; here we plug in CGImageSource.
extern "C" int  mcle_png_decode_rgba8(const void* data, unsigned long length,
                                       unsigned char** out_rgba,
                                       int* out_w, int* out_h);
extern "C" void mcle_png_decode_free(unsigned char* pixels);

// Forward decl - defined further down in this file.
extern "C" void mcle_glbridge_tex_image_2d_rgba(unsigned int tex_id, int width, int height,
                                                  const void* rgba_pixels);

namespace { std::unordered_map<std::string, unsigned int> g_path_to_tex; }

// Loads `<path>` as a PNG file, decodes it, uploads the RGBA bytes to a
// new MTLTexture, registers it in the GL texture map, and returns the
// allocated id. Cached by path - subsequent calls hit the cache. Returns
// 0 if the file is missing or decode fails (caller falls back to default).
extern "C" unsigned int mcle_glbridge_load_or_get_png_path(const char* path) {
    if (!path) return 0;
    auto it = g_path_to_tex.find(path);
    if (it != g_path_to_tex.end()) return it->second;

    FILE* f = fopen(path, "rb");
    if (!f) {
        NSLog(@"[mcle_glbridge G4] PNG missing: %s", path);
        // Also log to crash_log.txt so we can diagnose load-order issues
        // in shared logs. Otherwise these attempts only land in NSLog
        // (Apple system log) which the user can't easily capture.
        extern int mcle_log_msg(const char *);
        std::string m = std::string("PNG_MISSING ") + path;
        mcle_log_msg(m.c_str());
        g_path_to_tex[path] = 0;
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); g_path_to_tex[path] = 0; return 0; }
    std::vector<uint8_t> bytes((size_t)sz);
    size_t read = fread(bytes.data(), 1, (size_t)sz, f);
    fclose(f);
    if (read != (size_t)sz) {
        NSLog(@"[mcle_glbridge G4] short read for %s", path);
        g_path_to_tex[path] = 0;
        return 0;
    }

    unsigned char* rgba = nullptr;
    int            w = 0, h = 0;
    if (!mcle_png_decode_rgba8(bytes.data(), (unsigned long)bytes.size(),
                                 &rgba, &w, &h)) {
        NSLog(@"[mcle_glbridge G4] PNG decode failed: %s", path);
        g_path_to_tex[path] = 0;
        return 0;
    }

    const unsigned int id = g_next_tex_id.fetch_add(1, std::memory_order_relaxed);
    mcle_glbridge_tex_image_2d_rgba(id, w, h, rgba);
    mcle_png_decode_free(rgba);
    g_path_to_tex[path] = id;
    NSLog(@"[mcle_glbridge G4] loaded %s -> id=%u (%dx%d)", path, id, w, h);
    {
        // Also log to crash_log.txt - critical for diagnosing which file
        // path wins the bindTexture fallback chain (TitleUpdate/res vs
        // 1_2_2/ vs base) and what atlas dimensions the result has.
        extern int mcle_log_msg(const char *);
        char buf[512];
        snprintf(buf, sizeof(buf), "PNG_LOADED %s -> id=%u (%dx%d)",
                 path, id, w, h);
        mcle_log_msg(buf);
    }
    return id;
}

// G4-step2: GL texture object registry. All allocations / binds /
// uploads route through these so the fragment shader's bound texture
// matches whatever upstream glBindTexture last set.
extern "C" unsigned int mcle_glbridge_gen_texture(void) {
    return g_next_tex_id.fetch_add(1, std::memory_order_relaxed);
}
extern "C" void mcle_glbridge_gen_textures_n(int n, unsigned int* out) {
    if (!out || n <= 0) return;
    for (int i = 0; i < n; i++) {
        out[i] = g_next_tex_id.fetch_add(1, std::memory_order_relaxed);
    }
}
extern "C" void mcle_glbridge_delete_texture(unsigned int tex_id) {
    auto it = g_gl_textures.find(tex_id);
    if (it != g_gl_textures.end()) g_gl_textures.erase(it);
    if (g_bound_tex_id == tex_id) {
        g_bound_tex_id    = 0;
        g_current_texture = nil;
    }
}
extern "C" void mcle_glbridge_bind_texture(unsigned int tex_id) {
    g_bound_tex_id = tex_id;
    if (tex_id == 0) {
        g_current_texture = nil;
    } else {
        auto it = g_gl_textures.find(tex_id);
        g_current_texture = (it != g_gl_textures.end()) ? it->second : nil;
    }
    // Diagnostic: log each bind so we can correlate sun/moon binds with
    // their draw dispatches. Throttled to first 200 binds to avoid
    // flooding the log once steady-state is reached.
    {
        static int s_count = 0;
        if (s_count < 200) {
            extern int mcle_log_msg(const char *);
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "BIND_TEX id=%u registered=%d tex=%p",
                     tex_id,
                     (int)(g_gl_textures.find(tex_id) != g_gl_textures.end()),
                     (__bridge void*)g_current_texture);
            mcle_log_msg(buf);
            s_count++;
        }
    }
}
extern "C" unsigned int mcle_glbridge_get_bound_texture(void) {
    return g_bound_tex_id;
}
extern "C" void mcle_glbridge_tex_image_2d_rgba(unsigned int tex_id, int width, int height,
                                                  const void* rgba_pixels) {
    if (!g.device || width <= 0 || height <= 0) return;
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                            width:(NSUInteger)width
                                                           height:(NSUInteger)height
                                                        mipmapped:NO];
    td.usage       = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [g.device newTextureWithDescriptor:td];
    if (!tex) return;
    if (rgba_pixels) {
        [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)width, (NSUInteger)height)
                mipmapLevel:0
                  withBytes:rgba_pixels
                bytesPerRow:(NSUInteger)(width * 4)];
    }
    g_gl_textures[tex_id] = tex;
    if (tex_id == g_bound_tex_id) g_current_texture = tex;
    // Diagnostic: log each texture upload so we can match texture id
    // back to the upload source (which BIL_CKPT path produced it).
    // For moon_phases.png (128x64), additionally dump the center pixels
    // of the top-left cell so we can confirm channel order is correct -
    // expected RGB ~ (175, 184, 204) blue-white, NOT (204, 184, 175).
    {
        static int s_count = 0;
        if (s_count < 50) {
            extern int mcle_log_msg(const char *);
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "TEX_UPLOAD id=%u w=%d h=%d", tex_id, width, height);
            mcle_log_msg(buf);
            if (rgba_pixels && width == 128 && height == 64) {
                const unsigned char* px = (const unsigned char*)rgba_pixels;
                int idx = (16 * 128 + 16) * 4;  // pixel (16,16), cell-0 center
                snprintf(buf, sizeof(buf),
                         "TEX_PIXEL moon(16,16) R=%u G=%u B=%u A=%u",
                         px[idx+0], px[idx+1], px[idx+2], px[idx+3]);
                mcle_log_msg(buf);
            }
            if (rgba_pixels && width == 32 && height == 32) {
                const unsigned char* px = (const unsigned char*)rgba_pixels;
                int idx = (16 * 32 + 16) * 4;  // pixel (16,16), sun center
                snprintf(buf, sizeof(buf),
                         "TEX_PIXEL sun(16,16) R=%u G=%u B=%u A=%u",
                         px[idx+0], px[idx+1], px[idx+2], px[idx+3]);
                mcle_log_msg(buf);
            }
            s_count++;
        }
    }
}

// G3f: GL_CURRENT_COLOR setters. Each draw reads the live values into a
// vertex-shader uniform and modulates per-vertex color by it. Matches
// fixed-function D3D9 modulate.
extern "C" void mcle_glbridge_color4f(float r, float g, float b, float a) {
    g_current_color[0] = r;
    g_current_color[1] = g;
    g_current_color[2] = b;
    g_current_color[3] = a;
}
extern "C" void mcle_glbridge_color3f(float r, float g, float b) {
    g_current_color[0] = r;
    g_current_color[1] = g;
    g_current_color[2] = b;
    g_current_color[3] = 1.0f;
}
extern "C" void mcle_glbridge_color4ub(unsigned char r, unsigned char g,
                                        unsigned char b, unsigned char a) {
    g_current_color[0] = (float)r / 255.0f;
    g_current_color[1] = (float)g / 255.0f;
    g_current_color[2] = (float)b / 255.0f;
    g_current_color[3] = (float)a / 255.0f;
}

// Replaces the active matrix with a Metal-style perspective projection
// (depth maps to [0..1], not OpenGL's [-1..1]). Convenience wrapper so
// MCLEGameLoop doesn't have to build the matrix by hand.
extern "C" void mcle_glbridge_metal_perspective(float fov_y_deg,
                                                 float aspect,
                                                 float near_z,
                                                 float far_z) {
    const float fov_rad = fov_y_deg * (float)M_PI / 180.0f;
    const float t       = tanf(fov_rad / 2.0f) * near_z;
    const float r       = t * aspect;
    Mat4 P;
    P.m[0]  = near_z / r;  P.m[1]  = 0;            P.m[2]  = 0;                              P.m[3]  = 0;
    P.m[4]  = 0;           P.m[5]  = near_z / t;   P.m[6]  = 0;                              P.m[7]  = 0;
    P.m[8]  = 0;           P.m[9]  = 0;            P.m[10] = -far_z / (far_z - near_z);      P.m[11] = -1;
    P.m[12] = 0;           P.m[13] = 0;            P.m[14] = -(near_z * far_z) / (far_z - near_z); P.m[15] = 0;
    current_stack().back() = P;
}

// Replaces the active matrix with a Metal-style orthographic projection.
// Standard 2D HUD setup: glOrtho(left, right, bottom, top, near, far) with
// y-down screen coords if you set top<bottom. Depth maps to [0..1] (Metal).
extern "C" void mcle_glbridge_metal_ortho(float left, float right,
                                           float bottom, float top,
                                           float near_z, float far_z) {
    const float rl = right - left;
    const float tb = top   - bottom;
    const float fn = far_z - near_z;
    Mat4 P;
    P.m[0]  = 2.0f / rl;          P.m[1]  = 0;                  P.m[2]  = 0;                          P.m[3]  = 0;
    P.m[4]  = 0;                  P.m[5]  = 2.0f / tb;          P.m[6]  = 0;                          P.m[7]  = 0;
    P.m[8]  = 0;                  P.m[9]  = 0;                  P.m[10] = -1.0f / fn;                 P.m[11] = 0;
    P.m[12] = -(right + left)/rl; P.m[13] = -(top + bottom)/tb; P.m[14] = -near_z / fn;               P.m[15] = 1;
    current_stack().back() = P;
}

// HUD draw helper is defined further down (after the anonymous namespace
// where immediate_dispatch lives), since it dispatches via that function.


namespace {

// Compute MVP = projection * modelview using the current matrix stacks.
void compute_mvp(float* mvp16) {
    mat_mul(mvp16,
            g_projection_stack.back().m,
            g_modelview_stack.back().m);
}

inline void immediate_dispatch(int prim, int count, const void* data,
                                int fmt, int shader);

// Replay a recorded display list. Per-cmd modelview/texture get swapped in
// before each draw so chunk lists translate to their world position and
// sample their recorded texture (terrain.png) regardless of current state.
void call_list_replay(const DisplayList& dl) {
    for (const auto& cmd : dl.draws) {
        Mat4 savedMv = g_modelview_stack.back();
        unsigned int savedTexId = g_bound_tex_id;
        id<MTLTexture> savedTex = g_current_texture;
        if (cmd.hasTranslate) {
            // Apply chunk's world translation on top of current view:
            //   new_top = current_view * translate(chunk_xyz)
            mat_translate(g_modelview_stack.back().m,
                          cmd.translate[0], cmd.translate[1], cmd.translate[2]);
        }
        // Per-cmd texture override - skip for fmt=4 (chunks). Chunks all
        // use the terrain atlas bound externally before render() runs;
        // their captured texId at record time is whatever was bound during
        // boot stitch (often a misc texture) and overriding to it gives
        // wrong pixels.
        const bool overrideTex = (cmd.texId != 0 && cmd.fmt != 4);
        if (overrideTex) {
            auto tit = g_gl_textures.find(cmd.texId);
            g_current_texture = (tit != g_gl_textures.end()) ? tit->second : nil;
            g_bound_tex_id    = cmd.texId;
        }
        immediate_dispatch(cmd.prim, cmd.count, cmd.data.data(),
                           cmd.fmt, cmd.shader);
        if (cmd.hasTranslate) g_modelview_stack.back() = savedMv;
        if (overrideTex) {
            g_current_texture = savedTex;
            g_bound_tex_id    = savedTexId;
        }
    }
}

// Immediate dispatch path. G3c lands MTLBuffer upload + drawPrimitives.
// G3d builds an MVP from the player's live position so we look out into
// the world from where the simulation thinks the player stands.
inline void immediate_dispatch(int prim, int count, const void* data,
                                int fmt, int /*shader*/) {
    g_draw_count.fetch_add(1, std::memory_order_relaxed);

    if (!g.inFrame || !g.enc)        return;
    if (count <= 0 || !data)         return;
    if (fmt != 1 && fmt != 2 && fmt != 4) return;

    const bool isCompact = (fmt == 4);
    if (isCompact) {
        if (!ensure_world_pipeline_compact()) return;
    } else {
        if (!ensure_world_pipeline()) return;
    }

    const int stride = vertex_stride(fmt);
    const NSUInteger byteLen = (NSUInteger)count * (NSUInteger)stride;

    id<MTLBuffer> vbuf = [g.device newBufferWithBytes:data
                                                length:byteLen
                                               options:MTLResourceStorageModeShared];
    if (!vbuf) return;

    // G3d: real perspective MVP centered on the live player position.
    float mvp[16];
    compute_mvp(mvp);

    // Pick pipeline variant: no-blend / blend-over / blend-additive /
    // depth-only. Cache last-applied PSO so dispatches don't re-bind
    // unnecessarily.
    id<MTLRenderPipelineState> pso = nil;
    if (isCompact) {
        if (!g_blend_enabled)            pso = g_world_pso_compact;
        else if (g_blend_func_mode == 1) pso = g_world_pso_compact_additive;
        else if (g_blend_func_mode == 2) pso = g_world_pso_compact_depth_only;
        else                             pso = g_world_pso_compact_blend;
    } else {
        // fmt=1 path. When alpha test is disabled (sun/moon/sunrise pass)
        // use the no-cutout fragment shader so semi-transparent texture
        // pixels survive. Chunks/fmt=4 always keep the cutout (foliage).
        if (!g_blend_enabled) {
            pso = g_world_pso;  // no-blend has no nocut variant; not used by sky
        } else if (g_blend_func_mode == 1) {
            pso = g_alpha_test_enabled ? g_world_pso_additive : g_world_pso_additive_nocut;
        } else if (g_blend_func_mode == 2) {
            pso = g_world_pso_depth_only;
        } else {
            pso = g_alpha_test_enabled ? g_world_pso_blend : g_world_pso_blend_nocut;
        }
    }
    if (pso && pso != g_lastWorldPso) {
        [g.enc setRenderPipelineState:pso];
        g_lastWorldPso = pso;
        // Diagnostic: log which PSO variant is active when binding the
        // texture changes (sun -> moon -> back). Mostly useful right
        // after BIND_TEX so we can see if alpha-test=off actually picks
        // the nocut variant.
        static int s_count = 0;
        if (s_count < 60) {
            extern int mcle_log_msg(const char *);
            const char *name = "?";
            if      (pso == g_world_pso)                    name = "fmt1_noblend_cut";
            else if (pso == g_world_pso_blend)              name = "fmt1_blend_cut";
            else if (pso == g_world_pso_additive)           name = "fmt1_add_cut";
            else if (pso == g_world_pso_blend_nocut)        name = "fmt1_blend_NOCUT";
            else if (pso == g_world_pso_additive_nocut)     name = "fmt1_add_NOCUT";
            else if (pso == g_world_pso_depth_only)         name = "fmt1_depth_only";
            else if (pso == g_world_pso_compact)            name = "fmt4_noblend";
            else if (pso == g_world_pso_compact_blend)      name = "fmt4_blend";
            else if (pso == g_world_pso_compact_additive)   name = "fmt4_add";
            else if (pso == g_world_pso_compact_depth_only) name = "fmt4_depth_only";
            char buf[120];
            snprintf(buf, sizeof(buf),
                     "PSO_PICK %s atest=%d blend=%d func=%d tex=%u",
                     name, (int)g_alpha_test_enabled,
                     (int)g_blend_enabled, g_blend_func_mode,
                     g_bound_tex_id);
            mcle_log_msg(buf);
            s_count++;
        }
    }
    {
        // Three modes mirroring GL: (test=on,write=on) normal, (test=on,
        // write=off) sky dome - reads but doesn't occlude later passes,
        // (test=off,write=off) HUD/overlay. glDepthMask(false) before
        // skyList draw is what lets the sun/moon pass the depth test
        // against terrain instead of being clipped by the dome.
        id<MTLDepthStencilState> ds;
        if (!g_depth_test_enabled) {
            ds = g.depthStateNoTest;
        } else if (!g_depth_write_enabled) {
            ds = g.depthStateNoWrite;
        } else {
            ds = g.depthState;
        }
        if (ds && ds != g_lastDepthState) {
            [g.enc setDepthStencilState:ds];
            g_lastDepthState = ds;
        }
    }
    // Parity with upstream chunks: glEnable(GL_CULL_FACE) with default
    // GL CCW winding. Track last-applied state so we don't issue redundant
    // setCullMode / setFrontFacingWinding on every one of ~2300+ dispatches
    // per frame. g_lastCullMode / g_lastWinding reset at frame begin.
    if (isCompact) {
        if (g_lastWinding != MTLWindingCounterClockwise) {
            [g.enc setFrontFacingWinding:MTLWindingCounterClockwise];
            g_lastWinding = MTLWindingCounterClockwise;
        }
        if (g_lastCullMode != MTLCullModeBack) {
            [g.enc setCullMode:MTLCullModeBack];
            g_lastCullMode = MTLCullModeBack;
        }
    } else {
        if (g_lastCullMode != MTLCullModeNone) {
            [g.enc setCullMode:MTLCullModeNone];
            g_lastCullMode = MTLCullModeNone;
        }
    }
    [g.enc setVertexBuffer:vbuf offset:0 atIndex:0];
    // MVPUniforms = mvp + texMatrix (top of texture stack). Identity for
    // chunks; translates for cloud cube cells when renderAdvancedClouds
    // sets up per-cell UV offsets via glMatrixMode(GL_TEXTURE).
    {
        float uniforms[32];
        for (int i = 0; i < 16; i++) uniforms[i]    = mvp[i];
        const Mat4 &tm = g_texture_stack.back();
        for (int i = 0; i < 16; i++) uniforms[16+i] = tm.m[i];
        [g.enc setVertexBytes:uniforms length:sizeof(uniforms) atIndex:1];
        // Diagnostic: log fmt=1 dispatches where the texture matrix is
        // NOT identity. renderAdvancedClouds is the only path that sets
        // GL_TEXTURE translates - sun/moon/sky dome leave it at identity.
        // Filtering to non-identity skips the noise from earlier sky
        // dispatches and pins whether renderAdvancedClouds actually
        // routes per-cube UV offsets through our matrix stack.
        if (!isCompact) {
            const float tx = tm.m[12];
            const float ty = tm.m[13];
            if (tx != 0.0f || ty != 0.0f) {
                static int s_tmDumped = 0;
                if (s_tmDumped < 60) {
                    extern int mcle_log_msg(const char *);
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "TM_NONID idx=%d tx=%.4f ty=%.4f",
                             s_tmDumped, tx, ty);
                    mcle_log_msg(buf);
                    s_tmDumped++;
                }
            }
        }
    }
    [g.enc setVertexBytes:g_current_color length:sizeof(g_current_color) atIndex:2];

    // Fog uniforms at vertex/fragment buffer 3. Vertex shader reads
    // modelviewRow2 to compute eye-space depth; fragment reads fogColor
    // + fogRange to mix output toward fog color. fogColor.w gates the
    // effect off when GL_FOG is disabled - matches GL fixed-function.
    {
        float modelview[16];
        ::mcle_glbridge_get_modelview(modelview);
        // Row 2 of a column-major 4x4 = elements [2, 6, 10, 14].
        float fogBuf[12] = {
            g_fog_r, g_fog_g, g_fog_b, g_fog_enabled ? 1.0f : 0.0f,
            g_fog_start, g_fog_end, 0.0f, 0.0f,
            modelview[2], modelview[6], modelview[10], modelview[14]
        };
        [g.enc setVertexBytes:fogBuf   length:sizeof(fogBuf) atIndex:3];
        [g.enc setFragmentBytes:fogBuf length:sizeof(fogBuf) atIndex:3];
    }

    // G4: bind whatever's current (defaults to 1x1 white) + sampler.
    // When GL_TEXTURE_2D is disabled (skyList / starList / darkList /
    // sunrise gradient fan), force the 1x1 white texture so sample x
    // vertex_color collapses to vertex_color - parity with fixed-function
    // GL where disabled texturing means the fragment is just the primary
    // color. Without this, those vertex-color-only draws sample whatever
    // atlas happens to be bound at uninitialized UVs and produce a
    // chaotic textured slab stuck around the player.
    id<MTLTexture> tex = !g_texture_2d_enabled
                            ? g_default_texture
                            : (g_current_texture ? g_current_texture : g_default_texture);
    if (tex)               [g.enc setFragmentTexture:tex atIndex:0];
    if (g_default_sampler) [g.enc setFragmentSamplerState:g_default_sampler atIndex:0];

    // Lightmap at fragment unit 1. Only chunk vertices (fmt=4) carry a
    // real tex2 with sky+block light levels; fmt=1 sky/cloud passes get
    // the 1x1 white sibling so the shader's lightmap multiply is a no-op
    // for those.
    id<MTLTexture> lm = isCompact ? g_lightmap_texture : g_lightmap_white_texture;
    if (lm) [g.enc setFragmentTexture:lm atIndex:1];
    if (g_lightmap_sampler) [g.enc setFragmentSamplerState:g_lightmap_sampler atIndex:1];

    // Diagnostic on the first compact dispatch: dump tex2 bytes from
    // the first 4 vertices (one quad). If 4 corners of one face show
    // DIFFERENT tex2 values, smooth lighting is firing in tessellation.
    // If all 4 are identical, AO smoothing isn't reaching our buffer
    // and shadows will look blocky regardless of sampler.
    if (isCompact) {
        static int s_cvDumped = 0;
        if (s_cvDumped < 1 && data && count >= 4) {
            const uint8_t *p = (const uint8_t *)data;
            extern int mcle_log_msg(const char *);
            char buf[256];
            for (int v = 0; v < 4; v++) {
                const uint8_t *q = p + (v * 16);
                snprintf(buf, sizeof(buf),
                         "CV_QUAD v%d tex2=%02x %02x %02x %02x", v,
                         q[12], q[13], q[14], q[15]);
                mcle_log_msg(buf);
            }
            s_cvDumped++;
        }
    }


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

    // GL_TRIANGLE_FAN (6) has no direct Metal equivalent. Expand it
    // into an indexed triangle list (v0, v1, v2), (v0, v2, v3), ...
    // so the center vertex participates in every fan triangle. Without
    // this expansion the dispatch would fall through to plain Triangle
    // mode and treat the 18 vertices as 6 disconnected triangles - the
    // first one (center + 2 ring) renders as a gradient wedge, which
    // is the right-angle triangle visible during sunset.
    if (prim == 6 /*GL_TRIANGLE_FAN*/ && count >= 3) {
        const int triCount = count - 2;
        const int idxCount = triCount * 3;
        std::vector<uint16_t> idxs(idxCount);
        for (int i = 0; i < triCount; i++) {
            idxs[i * 3 + 0] = 0;
            idxs[i * 3 + 1] = (uint16_t)(i + 1);
            idxs[i * 3 + 2] = (uint16_t)(i + 2);
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

// HUD: draw a textured quad in screen-space pixel coords. Self-contained:
// saves + restores matrices, sets ortho, and dispatches a single quad
// through the existing 32-byte vertex pipeline (g_world_pso). Defined here
// so immediate_dispatch (anonymous namespace above) is in scope by name.
extern "C" void mcle_hud_draw_textured_quad(int x, int y, int w, int h,
                                             float u0, float v0,
                                             float u1, float v1,
                                             unsigned int tex_id) {
    if (!g.inFrame || !g.enc) return;
    int sw = 0, sh = 0;
    mcle_metal_current_size(&sw, &sh);
    if (sw <= 0 || sh <= 0) return;

    Mat4 savedProj = g_projection_stack.back();
    Mat4 savedMv   = g_modelview_stack.back();
    unsigned int savedTexId = g_bound_tex_id;
    id<MTLTexture> savedTex = g_current_texture;

    // Ortho with screen pixel coords, y-down (top=0, bottom=sh).
    mcle_glbridge_matrix_mode(0x1701 /* GL_PROJECTION */);
    mcle_glbridge_load_identity();
    mcle_glbridge_metal_ortho(0.0f, (float)sw, (float)sh, 0.0f, -1.0f, 1.0f);
    mcle_glbridge_matrix_mode(0x1700 /* GL_MODELVIEW */);
    mcle_glbridge_load_identity();

    if (tex_id != 0) mcle_glbridge_bind_texture(tex_id);

    // 32-byte vertex (fmt=1): pos float3 @0, uv float2 @12, color uchar4 @20,
    // normal uchar4 @24, extra @28. world_vert reads pos/uv/color/normal.
    struct V {
        float    pos[3];
        float    uv[2];
        uint8_t  color[4];
        uint8_t  normal[4];
        uint8_t  extra[4];
    };
    static_assert(sizeof(V) == 32, "HUD vertex must be 32 bytes");
    V v[4];
    const float fx0 = (float)x;
    const float fy0 = (float)y;
    const float fx1 = (float)(x + w);
    const float fy1 = (float)(y + h);
    const uint8_t white[4] = {255, 255, 255, 255};
    const uint8_t up[4]    = {0, 127, 0, 0};
    const uint8_t zero[4]  = {0, 0, 0, 0};
    auto fill = [&](V& vv, float px, float py, float pu, float pv) {
        vv.pos[0] = px; vv.pos[1] = py; vv.pos[2] = 0.0f;
        vv.uv[0]  = pu; vv.uv[1]  = pv;
        memcpy(vv.color,  white, 4);
        memcpy(vv.normal, up,    4);
        memcpy(vv.extra,  zero,  4);
    };
    // Order: tl, bl, br, tr - CCW when viewed from camera (-z toward viewer
    // with y-down ortho means tl->bl->br->tr is the CCW orientation).
    fill(v[0], fx0, fy0, u0, v0);
    fill(v[1], fx0, fy1, u0, v1);
    fill(v[2], fx1, fy1, u1, v1);
    fill(v[3], fx1, fy0, u1, v0);

    immediate_dispatch(7 /* GL_QUADS */, 4, v, 1 /* fmt */, 0);

    g_projection_stack.back() = savedProj;
    g_modelview_stack.back()  = savedMv;
    if (tex_id != 0) {
        g_current_texture = savedTex;
        g_bound_tex_id    = savedTexId;
    }
}

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

static uint64_t g_frame_start_ticks = 0;

extern "C" int mcle_metal_frame_begin(float r, float gn, float b, float a) {
    g_frame_start_ticks = mach_absolute_time();
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
        // LessEqual (not strict Less) for parity with upstream
        // Minecraft.cpp:394 glDepthFunc(GL_LEQUAL). Lets the grass-side
        // overlay's second pass (TileRenderer.cpp:5599+) render at the
        // exact same Z as the first pass instead of being depth-discarded;
        // also matches upstream's behavior for any other same-Z draws.
        MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
        dsd.depthCompareFunction = MTLCompareFunctionLessEqual;
        dsd.depthWriteEnabled    = YES;
        g.depthState = [g.device newDepthStencilStateWithDescriptor:dsd];
    }
    if (!g.depthStateNoTest) {
        MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
        dsd.depthCompareFunction = MTLCompareFunctionAlways;
        dsd.depthWriteEnabled    = NO;
        g.depthStateNoTest = [g.device newDepthStencilStateWithDescriptor:dsd];
    }
    if (!g.depthStateNoWrite) {
        MTLDepthStencilDescriptor* dsd = [[MTLDepthStencilDescriptor alloc] init];
        dsd.depthCompareFunction = MTLCompareFunctionLessEqual;
        dsd.depthWriteEnabled    = NO;
        g.depthStateNoWrite = [g.device newDepthStencilStateWithDescriptor:dsd];
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
    // Encoder is fresh - default state is no cull / clockwise winding.
    g_lastCullMode = MTLCullModeNone;
    g_lastWinding  = MTLWindingClockwise;
    g_lastDepthState = nil;
    g_lastWorldPso   = nil;
    // Each frame starts with depth test/write enabled, alpha test on
    // (chunks need cutout for foliage), blend disabled, fog disabled
    // (matches upstream default GL state). renderSky/renderClouds
    // glEnable(GL_BLEND), glEnable(GL_FOG), glDepthMask,
    // glDisable(GL_ALPHA_TEST) etc. flip these via the bridge.
    g_depth_test_enabled  = true;
    g_depth_write_enabled = true;
    g_alpha_test_enabled  = true;
    g_blend_enabled       = false;
    g_blend_func_mode     = 0;
    g_fog_enabled         = false;

    // Per-frame timing: log min/avg/max wall-clock between begin_frame calls
    // every 60 frames (~1/sec). Helps diagnose when walking vs rotating
    // produces different frame durations.
    {
        static uint64_t s_lastNs = 0;
        static uint64_t s_minNs = UINT64_MAX, s_maxNs = 0, s_sumNs = 0;
        static int      s_count = 0;
        const uint64_t now = mach_absolute_time();
        static mach_timebase_info_data_t s_tb = {0};
        if (s_tb.denom == 0) mach_timebase_info(&s_tb);
        if (s_lastNs != 0) {
            uint64_t dt = ((now - s_lastNs) * s_tb.numer) / s_tb.denom;
            if (dt < s_minNs) s_minNs = dt;
            if (dt > s_maxNs) s_maxNs = dt;
            s_sumNs += dt;
            s_count++;
            if (s_count >= 60) {
                char buf[160];
                snprintf(buf, sizeof(buf),
                         "frame_ms min=%.2f avg=%.2f max=%.2f n=%d",
                         s_minNs / 1.0e6, (s_sumNs / (double)s_count) / 1.0e6,
                         s_maxNs / 1.0e6, s_count);
                extern int mcle_log_msg(const char *msg);
                mcle_log_msg(buf);
                s_minNs = UINT64_MAX; s_maxNs = 0; s_sumNs = 0; s_count = 0;
            }
        }
        s_lastNs = now;
    }
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

    // Log when a single render frame takes >200ms. Pin whether the freeze
    // user reports lives in the render thread (i.e. the WaitForAll barrier
    // inside updateDirtyChunks blocks here) or somewhere else.
    if (g_frame_start_ticks != 0) {
        const uint64_t now = mach_absolute_time();
        static mach_timebase_info_data_t s_tb = {0};
        if (s_tb.denom == 0) mach_timebase_info(&s_tb);
        const uint64_t ns = (now - g_frame_start_ticks) * s_tb.numer / s_tb.denom;
        const uint64_t ms = ns / 1000000ull;
        if (ms > 200) {
            extern int mcle_log_msg(const char *);
            char buf[80];
            snprintf(buf, sizeof(buf), "SLOW_FRAME took=%llums", (unsigned long long)ms);
            mcle_log_msg(buf);
        }
    }
}

// G2a: Tesselator -> Metal hook. G3a: routes into the display-list
// recorder when glNewList is active; otherwise dispatches immediately.
// Counter bumps only on the immediate path so per-frame tick logs reflect
// actual draws, not the one-shot ctor recording.
// Track fmt distribution so we can see what formats chunks actually emit.
static unsigned long g_fmt_counts[16] = {0};

extern "C" void mcle_metal_draw_vertices(int prim, int count,
                                          const void* data,
                                          int fmt, int shader) {
    if (fmt >= 0 && fmt < 16) g_fmt_counts[fmt]++;
    if (g_recording_list != 0) {
        DrawCmd cmd;
        cmd.prim   = prim;
        cmd.count  = count;
        cmd.fmt    = fmt;
        cmd.shader = shader;
        const int sz = count * vertex_stride(fmt);
        const uint8_t* p = static_cast<const uint8_t*>(data);
        cmd.data.assign(p, p + sz);
        // G5: capture chunk world translation (set by translateToPos before
        // t->end()). Applied as translate on top of current modelview at
        // replay so geometry tracks the live camera.
        if (g_recording_has_translate) {
            cmd.translate[0] = g_recording_last_translate[0];
            cmd.translate[1] = g_recording_last_translate[1];
            cmd.translate[2] = g_recording_last_translate[2];
            cmd.hasTranslate = true;
        }
        // G5: capture currently bound texture id so chunks sample terrain.png
        // at replay even if other things have rebound the texture since.
        cmd.texId = g_bound_tex_id;
        // Push into the thread-local buffer - no lock. The whole buffer
        // is spliced into g_lists in one shot at glEndList.
        g_recording_buffer.draws.push_back(std::move(cmd));
        return;
    }
    immediate_dispatch(prim, count, data, fmt, shader);
}

extern "C" void mcle_glbridge_fmt_stats(unsigned long *out, int max_count) {
    int n = (max_count < 16) ? max_count : 16;
    for (int i = 0; i < n; i++) out[i] = g_fmt_counts[i];
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
    g_recording_buffer.draws.clear();
    g_recording_has_translate = false;
    g_recording_last_translate[0] = 0;
    g_recording_last_translate[1] = 0;
    g_recording_last_translate[2] = 0;
}

extern "C" void mcle_glbridge_end_list(void) {
    const int id = g_recording_list;
    const int draws = (int)g_recording_buffer.draws.size();
    if (id == 0) {
        // End-list with no matching begin-list. Drop the buffer so
        // it doesn't carry stale draws into the next recording.
        g_recording_buffer.draws.clear();
        return;
    }
    // Splice the per-thread recording buffer into the shared map under
    // a single exclusive lock. Hold time scales with one move-assign
    // not with the number of DrawCmds.
    {
        std::unique_lock<std::shared_mutex> _lk(g_lists_mu);
        g_lists[id] = std::move(g_recording_buffer);
    }
    g_recording_buffer.draws.clear();
    // First 50 lists logged so we can confirm the starList capture
    // inside LevelRenderer ctor (line 167-172 upstream) executes.
    {
        static int s_count = 0;
        if (s_count < 50) {
            extern int mcle_log_msg(const char *);
            char buf[80];
            snprintf(buf, sizeof(buf), "LIST_END id=%d draws=%d", id, draws);
            mcle_log_msg(buf);
            s_count++;
        }
    }
    g_recording_list = 0;
}

// Stats we update without I/O in the hot path.
static unsigned long g_call_list_hits = 0;
static unsigned long g_call_list_misses = 0;
static int g_call_list_first_miss_id = -1;
static int g_call_list_first_hit_id  = -1;
static int g_call_list_last_hit_id   = -1;
// Hits by ID range. Chunks get IDs in the lower (chunkLists, chunkLists+chunkCount*2)
// range. Sky/clouds/sun get higher IDs after that range.
static unsigned long g_call_list_hits_low  = 0;  // id < 4000000 (chunk range typically)
static unsigned long g_call_list_hits_high = 0;  // id >= 4000000

extern "C" void mcle_glbridge_call_list(int id) {
    // Replay in-place under a shared lock. Writers (workers calling
    // glNewList) wait briefly while we replay; readers (other render
    // call_list) run in parallel.
    std::shared_lock<std::shared_mutex> _lk(g_lists_mu);
    auto it = g_lists.find(id);
    if (it == g_lists.end()) {
        if (g_call_list_first_miss_id == -1) g_call_list_first_miss_id = id;
        g_call_list_misses++;
        return;
    }
    if (g_call_list_first_hit_id == -1) g_call_list_first_hit_id = id;
    g_call_list_last_hit_id = id;
    g_call_list_hits++;
    if (id < 4000000) g_call_list_hits_low++; else g_call_list_hits_high++;
    call_list_replay(it->second);
}

// Sampled getter so the consumer logs once per second instead of per call.
extern "C" void mcle_glbridge_call_list_stats(unsigned long *hits, unsigned long *misses,
                                                int *first_miss, int *first_hit, unsigned long *list_count) {
    if (hits)        *hits        = g_call_list_hits;
    if (misses)      *misses      = g_call_list_misses;
    if (first_miss)  *first_miss  = g_call_list_first_miss_id;
    if (first_hit)   *first_hit   = g_call_list_first_hit_id;
    if (list_count)  *list_count  = (unsigned long)g_lists.size();
}

extern "C" void mcle_glbridge_call_list_stats_ext(unsigned long *hits_low,
                                                    unsigned long *hits_high,
                                                    int *last_hit) {
    if (hits_low)  *hits_low  = g_call_list_hits_low;
    if (hits_high) *hits_high = g_call_list_hits_high;
    if (last_hit)  *last_hit  = g_call_list_last_hit_id;
}

extern "C" void mcle_glbridge_release_lists(int id, int range) {
    std::unique_lock<std::shared_mutex> _lk(g_lists_mu);
    for (int i = 0; i < range; i++) g_lists.erase(id + i);
}

// Diagnostics: number of recorded display lists (so we can correlate with
// the 191 ctor draws we saw before recording was wired).
extern "C" unsigned long long mcle_glbridge_list_count(void) {
    return static_cast<unsigned long long>(g_lists.size());
}

// Memory accounting for the display-list cache. Each chunk mesh sits
// here as a vector of DrawCmds; each DrawCmd carries a byte vector
// holding the per-vertex data. If chunks get re-meshed but old lists
// aren't released, this grows unbounded - that's a leak suspect.
extern "C" unsigned long long mcle_glbridge_list_bytes(void) {
    std::shared_lock<std::shared_mutex> _lk(g_lists_mu);
    unsigned long long total = 0;
    for (const auto &kv : g_lists) {
        const DisplayList &dl = kv.second;
        total += sizeof(DisplayList);
        total += dl.draws.capacity() * sizeof(DrawCmd);
        for (const auto &cmd : dl.draws) {
            total += cmd.data.capacity();
        }
    }
    return total;
}

// Total DrawCmd count across all display lists. Coarse measure of
// how much geometry the renderer has compiled.
extern "C" unsigned long long mcle_glbridge_total_draws(void) {
    std::shared_lock<std::shared_mutex> _lk(g_lists_mu);
    unsigned long long total = 0;
    for (const auto &kv : g_lists) {
        total += kv.second.draws.size();
    }
    return total;
}

// Set of list IDs that the auto-replay must skip. Populated from
// MCLEGameLoop after LevelRenderer construction with the world-
// decoration list IDs (starList, skyList, darkList, haloRingList,
// cloudList[0..6]). Those are already drawn by upstream's renderSky /
// renderAdvancedClouds via glCallList, so auto-replaying them again
// draws a second copy stuck around the player - haloRingList in
// particular is never called by upstream (the call site checks for a
// specific player skin) so without skipping it we render a 100-radius
// textured ring on every frame that becomes the "wedge" visible
// across the upper sky.
static std::unordered_set<int> g_skip_autoreplay_lists;

extern "C" void mcle_glbridge_skip_autoreplay(int id) {
    g_skip_autoreplay_lists.insert(id);
}

// G3b: chunks still rely on this auto-replay because our
// glCallLists(IntBuffer*) is a no-op stub, so OffsettedRenderList's
// upstream render path doesn't actually call our display lists. World
// decoration lists registered via mcle_glbridge_skip_autoreplay get
// filtered out so they're only drawn once - from upstream's explicit
// glCallList sites.
extern "C" void mcle_glbridge_replay_all_lists(void) {
    for (const auto& kv : g_lists) {
        if (g_skip_autoreplay_lists.count(kv.first)) continue;
        call_list_replay(kv.second);
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
