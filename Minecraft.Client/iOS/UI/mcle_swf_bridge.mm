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
