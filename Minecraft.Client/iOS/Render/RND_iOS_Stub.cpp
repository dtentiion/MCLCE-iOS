#include "RND_iOS_Stub.h"

#include <cstdio>

// Placeholder renderer. Does nothing visible yet. Keeps the build graph happy
// until we drop in a GL ES backend (and MetalANGLE) or a native Metal backend.

namespace {
int g_width = 0;
int g_height = 0;
bool g_init = false;
} // namespace

extern "C" int mcle_render_init(void* /*context*/, int width, int height) {
    g_width = width;
    g_height = height;
    g_init = true;
    std::printf("[mcle_render] init %dx%d\n", width, height);
    return 0;
}

extern "C" void mcle_render_resize(int width, int height) {
    g_width = width;
    g_height = height;
}

extern "C" void mcle_render_frame(void) {
    // Real drawing will live here once the renderer port begins.
}

extern "C" void mcle_render_shutdown(void) {
    g_init = false;
}
