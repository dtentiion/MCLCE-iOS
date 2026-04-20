// Metal-backed render_handler for GameSWF.
//
// Second pass: plugs into MetalContext so draws issue commands on whatever
// render pass the outer frame loop has opened. begin_display / end_display
// bracket a SWF within an existing frame; they do not present.
//
// Draw calls are tracked via a per-frame counter and logged at most once a
// second so we can observe activity without spamming the log. Actual
// vertex submission will land in the next pass once we wire a solid-color
// pipeline state + vertex buffer pool.

#include "render_handler_metal.h"
#include "MetalContext.h"

#include "gameswf/gameswf.h"
#include "gameswf/gameswf_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

namespace {

struct metal_bitmap_info : public gameswf::bitmap_info {
    int w = 0, h = 0;
    int bpp = 4;
    // Raw pixel data, owned. Uploaded to a Metal texture lazily on first
    // draw once we wire the upload path.
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
    // --- Frame state captured between begin_display and end_display ---
    int vp_x0 = 0, vp_y0 = 0, vp_w = 0, vp_h = 0;
    float stage_x0 = 0, stage_x1 = 0, stage_y0 = 0, stage_y1 = 0;
    unsigned char fill_r = 255, fill_g = 255, fill_b = 255, fill_a = 255;
    bool  fill_enabled = false;
    int   mesh_strips_this_frame = 0;
    int   triangles_this_frame = 0;

    // ---- Bitmap creation --------------------------------------------------
    gameswf::bitmap_info* create_bitmap_info_empty() override {
        return new metal_bitmap_info();
    }
    gameswf::bitmap_info* create_bitmap_info_alpha(int w, int h, unsigned char* data) override {
        return new metal_bitmap_info(w, h, 1, data);
    }
    gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb* im) override {
        // image::rgb has m_data / m_width / m_height / m_pitch; for the stub
        // we allocate an empty-ish info and fill later when we wire image.h
        // properly. Leaving unused-parameter on purpose.
        (void)im;
        return new metal_bitmap_info();
    }
    gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba* im) override {
        (void)im;
        return new metal_bitmap_info();
    }
    gameswf::video_handler* create_video_handler() override {
        return nullptr;  // no video playback yet
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
        mesh_strips_this_frame = 0;
        triangles_this_frame = 0;
    }
    void end_display() override {
        // Emit a heartbeat at most once per second so we can confirm the
        // SWF player is actually calling into us once a movie loads.
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
    void set_matrix(const gameswf::matrix& m) override { (void)m; }
    void set_cxform(const gameswf::cxform& cx) override { (void)cx; }

    // ---- Draw -------------------------------------------------------------
    // TODO next pass: upload coords to a ring-buffered MTLBuffer and dispatch
    // a triangle-strip draw using a solid-color pipeline state. Today we
    // just count calls so we can validate wiring with logs.
    void draw_mesh_strip(const void* coords, int vertex_count) override {
        (void)coords;
        mesh_strips_this_frame++;
        triangles_this_frame += (vertex_count > 2) ? (vertex_count - 2) : 0;
    }
    void draw_triangle_list(const void* coords, int vertex_count) override {
        (void)coords;
        triangles_this_frame += vertex_count / 3;
    }
    void draw_line_strip(const void* coords, int vertex_count) override {
        (void)coords; (void)vertex_count;
    }

    // ---- Styles -----------------------------------------------------------
    void fill_style_disable(int fill_side) override {
        (void)fill_side;
        fill_enabled = false;
    }
    void fill_style_color(int fill_side, const gameswf::rgba& color) override {
        (void)fill_side;
        fill_r = color.m_r; fill_g = color.m_g;
        fill_b = color.m_b; fill_a = color.m_a;
        fill_enabled = true;
    }
    void fill_style_bitmap(int fill_side, gameswf::bitmap_info* bi, const gameswf::matrix& m,
                           bitmap_wrap_mode wm, bitmap_blend_mode bm) override
    {
        (void)fill_side; (void)bi; (void)m; (void)wm; (void)bm;
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
    }

    void set_antialiased(bool enable) override { (void)enable; }

    // ---- Masks / stencil --------------------------------------------------
    bool test_stencil_buffer(const gameswf::rect& bound, Uint8 pattern) override {
        (void)bound; (void)pattern;
        return false;
    }
    void begin_submit_mask() override {}
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
