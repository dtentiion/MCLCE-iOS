// Metal-backed render_handler for GameSWF.
//
// First pass: every method is stubbed with enough real logic for the probe
// to link, and for `begin_display` / `end_display` / `draw_mesh_strip` to
// be wired into our Metal renderer. The rest (textured fills, line
// strips, bitmap uploads, masks) comes in follow-up passes.

#include "render_handler_metal.h"

#include "gameswf/gameswf.h"
#include "gameswf/gameswf_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
        (void)viewport_x0; (void)viewport_y0;
        (void)viewport_width; (void)viewport_height;
        (void)x0; (void)x1; (void)y0; (void)y1;
        // TODO: save the viewport + background color, begin a Metal render pass.
    }
    void end_display() override {
        // TODO: present the pass to the drawable.
    }

    // ---- Transforms -------------------------------------------------------
    void set_matrix(const gameswf::matrix& m) override { (void)m; }
    void set_cxform(const gameswf::cxform& cx) override { (void)cx; }

    // ---- Draw -------------------------------------------------------------
    void draw_mesh_strip(const void* coords, int vertex_count) override {
        (void)coords; (void)vertex_count;
        // TODO: upload to a dynamic vertex buffer, dispatch a solid-color
        // triangle-strip draw under the current fill style.
    }
    void draw_triangle_list(const void* coords, int vertex_count) override {
        (void)coords; (void)vertex_count;
    }
    void draw_line_strip(const void* coords, int vertex_count) override {
        (void)coords; (void)vertex_count;
    }

    // ---- Styles -----------------------------------------------------------
    void fill_style_disable(int fill_side) override { (void)fill_side; }
    void fill_style_color(int fill_side, const gameswf::rgba& color) override {
        (void)fill_side; (void)color;
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
