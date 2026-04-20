#import <Foundation/Foundation.h>

#include "mcle_swf_bridge.h"
#include "render_handler_metal.h"

#include "gameswf/gameswf.h"
#include "gameswf/gameswf_player.h"
#include "gameswf/gameswf_root.h"
#include "gameswf/gameswf_movie_def.h"
#include "base/tu_file.h"

#include <atomic>

namespace {
std::atomic<bool> g_ready{false};
gameswf::render_handler* g_rh = nullptr;
gameswf::player* g_player = nullptr;

// Strong hold on the loaded root. player::m_current_root is a weak_ptr,
// so without this the root gets garbage-collected the moment our local
// gc_ptr in mcle_swf_load goes out of scope.
gameswf::gc_ptr<gameswf::root> g_root;

// Tiny last-status buffer. Updated by init/load/tick so the UI layer can
// display it on-device.
char g_status[256] = "uninit";

// Last N log lines from GameSWF, joined. Size tuned to fit the overlay.
char g_gameswf_log[512] = "";

// GameSWF calls this to open a SWF by path. Returns a tu_file which
// GameSWF owns (it deletes when done). Mirrors the reference
// implementation shipped in gameswf's own processor/test_ogl.
tu_file* mcle_swf_file_opener(const char* url) {
    return new tu_file(url, "rb");
}

// GameSWF log callback: mirrors to NSLog and keeps the last few lines in
// a wraparound buffer so we can surface them on the on-device overlay.
void mcle_swf_log_callback(bool is_error, const char* msg) {
    if (!msg) msg = "";
    NSLog(@"[gameswf%s] %s", is_error ? " ERR" : "", msg);

    size_t cur = strlen(g_gameswf_log);
    size_t room = sizeof(g_gameswf_log) - 1 - cur;
    size_t in = strlen(msg);
    while (in > 0 && (msg[in - 1] == '\n' || msg[in - 1] == '\r')) in--;
    if (in + 1 > sizeof(g_gameswf_log) - 1) in = sizeof(g_gameswf_log) - 2;
    if (in + 1 > room) {
        size_t need = (in + 1) - room;
        size_t cut = need;
        while (cut < cur && g_gameswf_log[cut] != '\n') cut++;
        if (cut < cur) cut++;
        memmove(g_gameswf_log, g_gameswf_log + cut, cur - cut + 1);
        cur -= cut;
    }
    if (cur > 0 && cur < sizeof(g_gameswf_log) - 1) {
        g_gameswf_log[cur++] = '\n';
        g_gameswf_log[cur] = '\0';
    }
    size_t copy = in;
    if (cur + copy >= sizeof(g_gameswf_log)) copy = sizeof(g_gameswf_log) - 1 - cur;
    memcpy(g_gameswf_log + cur, msg, copy);
    g_gameswf_log[cur + copy] = '\0';
}

void set_status(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
}
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

        // Route GameSWF's log messages through NSLog.
        gameswf::register_log_callback(mcle_swf_log_callback);
        gameswf::set_verbose_parse(true);

        // File opener: gameswf will not open any SWF without one.
        gameswf::register_file_opener_callback(mcle_swf_file_opener);

        g_player = new gameswf::player();
        if (!g_player) {
            NSLog(@"[mcle_swf] failed to create gameswf::player");
            return 2;
        }

        g_ready.store(true, std::memory_order_release);
        NSLog(@"[mcle_swf] init ok, player=%p", g_player);
        set_status("runtime ready, no movie");
    }
    return 0;
}

extern "C" const char* mcle_swf_last_status(void) {
    return g_status;
}

extern "C" const char* mcle_swf_gameswf_log(void) {
    return g_gameswf_log;
}

extern "C" void mcle_swf_shutdown(void) {
    g_ready.store(false, std::memory_order_release);
    g_root = nullptr;  // drop strong reference to the loaded root
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

extern "C" int mcle_swf_has_movie(void) {
    if (!g_player) return 0;
    return g_player->get_root() ? 1 : 0;
}

extern "C" void mcle_swf_tick_with_viewport(float dt, int vp_w, int vp_h) {
    if (!g_ready.load(std::memory_order_acquire) || !g_player) return;
    gameswf::root* r = g_player->get_root();
    if (!r) return;  // no movie loaded yet; nothing to do
    if (vp_w > 0 && vp_h > 0) {
        r->set_display_viewport(0, 0, vp_w, vp_h);
    }
    r->advance(dt);
    r->display();
}

extern "C" void mcle_swf_tick(float dt) {
    mcle_swf_tick_with_viewport(dt, 0, 0);
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
    if (!g_player || !path) {
        set_status("load: bad state (player=%p path=%s)",
                   (void*)g_player, path ?: "<null>");
        NSLog(@"[mcle_swf] %s", g_status);
        return 1;
    }
    // Check that the file exists and is readable at this path.
    FILE* f = fopen(path, "rb");
    if (!f) {
        set_status("load: fopen failed errno=%d path=%s", errno, path);
        NSLog(@"[mcle_swf] %s", g_status);
        return 3;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);

    NSLog(@"[mcle_swf] load: attempting %s (%ld bytes)", path, sz);
    set_status("load: file %ld bytes, calling load_file", sz);

    @autoreleasepool {
        g_root = g_player->load_file(path);  // strong ref held here
        if (!g_root.get_ptr()) {
            set_status("load_file returned null");
            NSLog(@"[mcle_swf] %s (%s)", g_status, path);
            return 2;
        }
        g_player->set_root(g_root.get_ptr());
        gameswf::movie_definition* def = g_root->get_movie_definition();
        if (def) {
            set_status("loaded v%d %.0fx%.0f frames=%d fps=%.1f",
                def->get_version(),
                def->get_width_pixels(), def->get_height_pixels(),
                def->get_frame_count(), def->get_frame_rate());
        } else {
            set_status("loaded but movie_def is null");
        }
        NSLog(@"[mcle_swf] %s", g_status);
    }
    return 0;
}
