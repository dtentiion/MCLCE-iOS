#import <Foundation/Foundation.h>

#include "mcle_swf_bridge.h"
#include "render_handler_metal.h"

#include "gameswf/gameswf.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"

#include <atomic>

namespace {
std::atomic<bool> g_ready{false};
gameswf::render_handler* g_rh = nullptr;
gameswf::player* g_player = nullptr;
}

extern "C" int mcle_swf_init(void) {
    if (g_ready.load(std::memory_order_acquire)) return 0;

    @autoreleasepool {
        g_rh = create_render_handler_metal();
        if (!g_rh) {
            NSLog(@"[mcle_swf] failed to create Metal render_handler");
            return 1;
        }
        gameswf::set_render_handler(g_rh);

        g_player = new gameswf::player();
        if (!g_player) {
            NSLog(@"[mcle_swf] failed to create gameswf::player");
            return 2;
        }

        g_ready.store(true, std::memory_order_release);
        NSLog(@"[mcle_swf] init ok, player=%p", g_player);
    }
    return 0;
}

extern "C" void mcle_swf_shutdown(void) {
    g_ready.store(false, std::memory_order_release);
    if (g_player) {
        delete g_player;
        g_player = nullptr;
    }
    if (g_rh) {
        gameswf::set_render_handler(nullptr);
        delete g_rh;
        g_rh = nullptr;
    }
}

extern "C" int mcle_swf_is_ready(void) {
    return g_ready.load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" void mcle_swf_tick(float dt) {
    if (!g_ready.load(std::memory_order_acquire) || !g_player) return;
    gameswf::root* r = g_player->get_root();
    if (!r) return;  // no movie loaded yet; nothing to do
    r->advance(dt);
    r->display();
}

extern "C" void mcle_swf_draw_test_rect(int vp_w, int vp_h) {
    if (!g_rh) return;
    if (vp_w <= 0 || vp_h <= 0) return;

    gameswf::rgba bg;   bg.m_r = 0;   bg.m_g = 0;   bg.m_b = 0;   bg.m_a = 0;
    gameswf::rgba fill; fill.m_r = 240; fill.m_g = 100; fill.m_b = 200; fill.m_a = 255;

    // Stage units = pixels for the test. Matrix is identity.
    g_rh->begin_display(bg, 0, 0, vp_w, vp_h,
                        0.0f, (float)vp_w, 0.0f, (float)vp_h);

    gameswf::matrix m;
    m.m_[0][0] = 1; m.m_[0][1] = 0; m.m_[0][2] = 0;
    m.m_[1][0] = 0; m.m_[1][1] = 1; m.m_[1][2] = 0;
    g_rh->set_matrix(m);
    g_rh->fill_style_color(0, fill);

    // Centered rectangle, ~40% of the screen. Triangle strip order:
    //   0----1
    //   |  / |
    //   | /  |
    //   2----3
    const int16_t cx = (int16_t)(vp_w / 2);
    const int16_t cy = (int16_t)(vp_h / 2);
    const int16_t hw = (int16_t)(vp_w / 5);
    const int16_t hh = (int16_t)(vp_h / 5);
    const int16_t verts[] = {
        (int16_t)(cx - hw), (int16_t)(cy - hh),
        (int16_t)(cx + hw), (int16_t)(cy - hh),
        (int16_t)(cx - hw), (int16_t)(cy + hh),
        (int16_t)(cx + hw), (int16_t)(cy + hh),
    };
    g_rh->draw_mesh_strip(verts, 4);

    g_rh->end_display();
}

extern "C" int mcle_swf_load(const char* path) {
    if (!g_player || !path) return 1;
    @autoreleasepool {
        auto root = g_player->load_file(path);
        if (!root.get_ptr()) {
            NSLog(@"[mcle_swf] load_file failed: %s", path);
            return 2;
        }
        g_player->set_root(root.get_ptr());
        NSLog(@"[mcle_swf] loaded movie: %s", path);
    }
    return 0;
}
