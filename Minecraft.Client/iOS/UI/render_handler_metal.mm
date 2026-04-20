// Metal-backed render_handler for GameSWF.
//
// Third pass: actual drawing. Lazy-compiles a solid-color pipeline on first
// use, transforms sint16 SWF input coords to clip space via the current
// matrix + stage bounds, and dispatches triangle-strip draws against the
// open MetalContext encoder. Bitmap fills, cxform color tint, anti-aliased
// line strips, masks, and line-width outlines are still stubbed.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "render_handler_metal.h"
#include "MetalContext.h"

#include "gameswf/gameswf.h"
#include "gameswf/gameswf_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <atomic>

extern "C" id<MTLDevice> mcle_metal_shared_device_objc(void);

namespace {

std::atomic<unsigned long long> g_total_strips{0};
std::atomic<unsigned long long> g_total_tris{0};
std::atomic<unsigned long long> g_total_frames{0};
std::atomic<unsigned long long> g_total_bitmap_draws{0};
std::atomic<unsigned long long> g_total_line_strips{0};
std::atomic<unsigned long long> g_total_masks{0};
std::atomic<unsigned long long> g_total_fill_bitmaps{0};

NSString* const kSolidColorShader = @R"(
    #include <metal_stdlib>
    using namespace metal;

    struct Uniforms {
        // Column-major 3x3 affine encoded as 4x4 clip-space matrix.
        float4x4 transform;
        float4   color;
    };

    struct VertexIn {
        // GameSWF passes Sint16 pairs. We read them as int16_t via the
        // [[attribute]] binding path and convert to float in the shader.
        short2 pos [[attribute(0)]];
    };

    struct v2f {
        float4 position [[position]];
        float4 color;
    };

    vertex v2f swf_solid_vert(VertexIn in [[stage_in]],
                              constant Uniforms& u [[buffer(1)]])
    {
        v2f o;
        float2 p = float2(in.pos);
        o.position = u.transform * float4(p, 0.0, 1.0);
        o.color = u.color;
        return o;
    }

    fragment float4 swf_solid_frag(v2f in [[stage_in]]) {
        return in.color;
    }
)";

struct Uniforms {
    float transform[16];
    float color[4];
};

struct metal_bitmap_info : public gameswf::bitmap_info {
    int w = 0, h = 0;
    int bpp = 4;
    unsigned char* pixels = nullptr;

    metal_bitmap_info() = default;
    metal_bitmap_info(int width, int height, int bytes_per_pixel, const unsigned char* src)
        : w(width), h(height), bpp(bytes_per_pixel)
    {
        const size_t n = (size_t)w * (size_t)h * (size_t)bpp;
        if (n) {
            pixels = (unsigned char*)std::malloc(n);
            if (src && pixels) std::memcpy(pixels, src, n);
        }
    }

    ~metal_bitmap_info() override {
        if (pixels) std::free(pixels);
    }

    int get_width()  const override { return w; }
    int get_height() const override { return h; }
    unsigned char* get_data() const override { return pixels; }
    int get_bpp() const override { return bpp; }
};

struct metal_render_handler : public gameswf::render_handler {
    // --- Pipeline, lazily built on first frame ---
    id<MTLRenderPipelineState> pso = nil;
    id<MTLDepthStencilState>   dss = nil;
    bool pso_built = false;
    bool pso_failed = false;

    // --- Per-frame state ---
    int vp_x0 = 0, vp_y0 = 0, vp_w = 0, vp_h = 0;
    float stage_x0 = 0, stage_x1 = 0, stage_y0 = 0, stage_y1 = 0;

    // Current SWF-space transform captured by set_matrix.
    float m00 = 1, m01 = 0, m02 = 0;
    float m10 = 0, m11 = 1, m12 = 0;

    // Current fill color in linear 0..1.
    float fill_r = 1, fill_g = 1, fill_b = 1, fill_a = 1;
    bool  fill_enabled = false;

    int   mesh_strips_this_frame = 0;
    int   triangles_this_frame = 0;

    bool ensure_pipeline() {
        if (pso_built || pso_failed) return pso != nil;
        pso_built = true;

        id<MTLDevice> device = mcle_metal_shared_device_objc();
        if (!device) { pso_failed = true; return false; }

        NSError* err = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:kSolidColorShader
                                                  options:nil
                                                    error:&err];
        if (!lib) {
            NSLog(@"[swf-handler] shader compile failed: %@", err);
            pso_failed = true;
            return false;
        }

        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        vd.attributes[0].format = MTLVertexFormatShort2;
        vd.attributes[0].offset = 0;
        vd.attributes[0].bufferIndex = 0;
        vd.layouts[0].stride = 4;  // 2 * sint16
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = [lib newFunctionWithName:@"swf_solid_vert"];
        desc.fragmentFunction = [lib newFunctionWithName:@"swf_solid_frag"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.vertexDescriptor = vd;

        pso = [device newRenderPipelineStateWithDescriptor:desc error:&err];
        if (!pso) {
            NSLog(@"[swf-handler] pipeline build failed: %@", err);
            pso_failed = true;
            return false;
        }
        return true;
    }

    // Compose: clip = stage_to_clip * swf_matrix * input
    // stage_to_clip maps stage coords [x0..x1, y0..y1] onto [-1..1, 1..-1].
    void compute_transform(float out16[16]) const {
        const float sx = (stage_x1 - stage_x0);
        const float sy = (stage_y1 - stage_y0);
        const float inv_sx = sx != 0 ? (2.0f / sx) : 0;
        const float inv_sy = sy != 0 ? (2.0f / sy) : 0;

        // stage_to_clip affine:
        //   cx = inv_sx * (sx_coord - stage_x0) - 1
        //   cy = 1 - inv_sy * (sy_coord - stage_y0)
        // swf_matrix affine:
        //   sx_coord = m00*x + m01*y + m02
        //   sy_coord = m10*x + m11*y + m12
        // Compose:
        const float a00 = inv_sx * m00;
        const float a01 = inv_sx * m01;
        const float a02 = inv_sx * (m02 - stage_x0) - 1.0f;
        const float a10 = -inv_sy * m10;
        const float a11 = -inv_sy * m11;
        const float a12 = 1.0f - inv_sy * (m12 - stage_y0);

        // Column-major 4x4 for Metal (float4x4).
        out16[0]  = a00; out16[1]  = a10; out16[2]  = 0; out16[3]  = 0;  // col 0
        out16[4]  = a01; out16[5]  = a11; out16[6]  = 0; out16[7]  = 0;  // col 1
        out16[8]  = 0;   out16[9]  = 0;   out16[10] = 1; out16[11] = 0;  // col 2
        out16[12] = a02; out16[13] = a12; out16[14] = 0; out16[15] = 1;  // col 3
    }

    void submit_triangles(const void* coords, int vertex_count,
                          MTLPrimitiveType prim, int triangles_added)
    {
        if (!coords || vertex_count <= 0) return;
        if (!fill_enabled) return;
        if (!ensure_pipeline() || !pso) return;

        id<MTLRenderCommandEncoder> enc =
            (__bridge id<MTLRenderCommandEncoder>)mcle_metal_current_encoder();
        if (!enc) return;

        id<MTLDevice> device = mcle_metal_shared_device_objc();
        if (!device) return;

        const NSUInteger byte_count = (NSUInteger)(vertex_count * 2 * sizeof(int16_t));

        Uniforms u{};
        compute_transform(u.transform);
        u.color[0] = fill_r; u.color[1] = fill_g;
        u.color[2] = fill_b; u.color[3] = fill_a;

        [enc setRenderPipelineState:pso];
        [enc setVertexBytes:coords length:byte_count atIndex:0];
        [enc setVertexBytes:&u length:sizeof(Uniforms) atIndex:1];
        [enc drawPrimitives:prim vertexStart:0 vertexCount:vertex_count];

        triangles_this_frame += triangles_added;
    }

    // ---- Bitmap creation --------------------------------------------------
    gameswf::bitmap_info* create_bitmap_info_empty() override {
        return new metal_bitmap_info();
    }
    gameswf::bitmap_info* create_bitmap_info_alpha(int w, int h, unsigned char* data) override {
        return new metal_bitmap_info(w, h, 1, data);
    }
    gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb* im) override {
        (void)im;
        return new metal_bitmap_info();
    }
    gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba* im) override {
        (void)im;
        return new metal_bitmap_info();
    }
    gameswf::video_handler* create_video_handler() override {
        return nullptr;
    }

    // ---- Frame lifecycle --------------------------------------------------
    void begin_display(
        gameswf::rgba background_color,
        int viewport_x0, int viewport_y0,
        int viewport_width, int viewport_height,
        float x0, float x1, float y0, float y1) override
    {
        (void)background_color;
        vp_x0 = viewport_x0; vp_y0 = viewport_y0;
        vp_w = viewport_width; vp_h = viewport_height;
        stage_x0 = x0; stage_x1 = x1;
        stage_y0 = y0; stage_y1 = y1;

        // Reset matrix / fill to sensible defaults in case the SWF does not
        // emit set_matrix / fill_style_color before its first draw.
        m00 = 1; m01 = 0; m02 = 0;
        m10 = 0; m11 = 1; m12 = 0;
        fill_enabled = false;

        mesh_strips_this_frame = 0;
        triangles_this_frame = 0;
    }
    void end_display() override {
        g_total_frames.fetch_add(1, std::memory_order_relaxed);
        g_total_strips.fetch_add((unsigned long long)mesh_strips_this_frame,
                                 std::memory_order_relaxed);
        g_total_tris.fetch_add((unsigned long long)triangles_this_frame,
                               std::memory_order_relaxed);
        using namespace std::chrono;
        static steady_clock::time_point last;
        auto now = steady_clock::now();
        if (now - last > seconds(1)) {
            last = now;
            std::printf("[swf-handler] frame strips=%d tris=%d viewport=%dx%d\n",
                mesh_strips_this_frame, triangles_this_frame, vp_w, vp_h);
        }
    }

    // ---- Transforms -------------------------------------------------------
    void set_matrix(const gameswf::matrix& m) override {
        m00 = m.m_[0][0]; m01 = m.m_[0][1]; m02 = m.m_[0][2];
        m10 = m.m_[1][0]; m11 = m.m_[1][1]; m12 = m.m_[1][2];
    }
    void set_cxform(const gameswf::cxform& cx) override { (void)cx; }

    // ---- Draw -------------------------------------------------------------
    void draw_mesh_strip(const void* coords, int vertex_count) override {
        mesh_strips_this_frame++;
        int tris = (vertex_count > 2) ? (vertex_count - 2) : 0;
        submit_triangles(coords, vertex_count, MTLPrimitiveTypeTriangleStrip, tris);
    }
    void draw_triangle_list(const void* coords, int vertex_count) override {
        submit_triangles(coords, vertex_count, MTLPrimitiveTypeTriangle, vertex_count / 3);
    }
    void draw_line_strip(const void* coords, int vertex_count) override {
        (void)coords; (void)vertex_count;
        g_total_line_strips.fetch_add(1, std::memory_order_relaxed);
    }

    // ---- Styles -----------------------------------------------------------
    void fill_style_disable(int fill_side) override {
        (void)fill_side;
        fill_enabled = false;
    }
    void fill_style_color(int fill_side, const gameswf::rgba& color) override {
        (void)fill_side;
        fill_r = color.m_r / 255.0f;
        fill_g = color.m_g / 255.0f;
        fill_b = color.m_b / 255.0f;
        fill_a = color.m_a / 255.0f;
        fill_enabled = true;
    }
    void fill_style_bitmap(int fill_side, gameswf::bitmap_info* bi, const gameswf::matrix& m,
                           bitmap_wrap_mode wm, bitmap_blend_mode bm) override
    {
        (void)fill_side; (void)bi; (void)m; (void)wm; (void)bm;
        g_total_fill_bitmaps.fetch_add(1, std::memory_order_relaxed);
        // Fall back to white until we wire a textured pipeline.
        fill_r = fill_g = fill_b = 1.0f;
        fill_a = 1.0f;
        fill_enabled = true;
    }
    void line_style_disable() override {}
    void line_style_color(gameswf::rgba color) override { (void)color; }
    void line_style_width(float width) override { (void)width; }

    // ---- Bitmap draw (glyphs) --------------------------------------------
    void draw_bitmap(
        const gameswf::matrix& m,
        gameswf::bitmap_info* bi,
        const gameswf::rect& coords,
        const gameswf::rect& uv_coords,
        gameswf::rgba color) override
    {
        (void)m; (void)bi; (void)coords; (void)uv_coords; (void)color;
        g_total_bitmap_draws.fetch_add(1, std::memory_order_relaxed);
    }

    void set_antialiased(bool enable) override { (void)enable; }

    // ---- Masks / stencil --------------------------------------------------
    bool test_stencil_buffer(const gameswf::rect& bound, Uint8 pattern) override {
        (void)bound; (void)pattern;
        return false;
    }
    void begin_submit_mask() override {
        g_total_masks.fetch_add(1, std::memory_order_relaxed);
    }
    void end_submit_mask() override {}
    void disable_mask() override {}

    // ---- Misc -------------------------------------------------------------
    bool is_visible(const gameswf::rect& bound) override { (void)bound; return true; }
    void open() override {}
};

} // namespace

gameswf::render_handler* create_render_handler_metal() {
    return new metal_render_handler();
}

extern "C" unsigned long long mcle_swf_total_mesh_strips(void) {
    return g_total_strips.load(std::memory_order_relaxed);
}
extern "C" unsigned long long mcle_swf_total_triangles(void) {
    return g_total_tris.load(std::memory_order_relaxed);
}
extern "C" unsigned long long mcle_swf_total_frames(void) {
    return g_total_frames.load(std::memory_order_relaxed);
}
extern "C" unsigned long long mcle_swf_total_bitmap_draws(void) {
    return g_total_bitmap_draws.load(std::memory_order_relaxed);
}
extern "C" unsigned long long mcle_swf_total_line_strips(void) {
    return g_total_line_strips.load(std::memory_order_relaxed);
}
extern "C" unsigned long long mcle_swf_total_masks(void) {
    return g_total_masks.load(std::memory_order_relaxed);
}
extern "C" unsigned long long mcle_swf_total_fill_bitmaps(void) {
    return g_total_fill_bitmaps.load(std::memory_order_relaxed);
}
