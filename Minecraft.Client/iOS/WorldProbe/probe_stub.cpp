// Anchor TU for the probe static archive. Also home for single-TU
// definitions of platform-global instances upstream code references
// via `extern`. Real iOS implementations live under platform/ subdirs;
// this file just gives the linker a definition so the archive resolves.

#include "iOS_stdafx.h"
#include "4JLibs/inc/4J_Profile.h"
#include "4JLibs/inc/4J_Storage.h"
#include "4JLibs/inc/4J_Render.h"
#include "ChestTile.h"
#include "ZonedChunkStorage.h"
#include "../Minecraft.Client/MinecraftServer.h"
#include "../Minecraft.Client/PlayerConnection.h"
#include "../Minecraft.Client/ServerPlayer.h"
#include "../Minecraft.Client/LevelRenderer.h"

namespace mcle_world_probe { inline void _anchor() {} }

// Definitions of platform-globals upstream gameplay code references via
// `extern`. Real iOS implementations land under Minecraft.Client/iOS/
// (Profile/, Storage/, etc) but the probe lib needs a single TU that
// owns the symbol so the link resolves.
C_4JProfile ProfileManager;
C4JStorage  StorageManager;
C4JRender   RenderManager;
C_4JInput   InputManager;

// G2a/G3a: probe-lib weak fallbacks. The real Metal-backed impls live
// in Render/MetalContext.mm and override these in the iOS app link.
// Marked weak so the probe link-test (which doesn't link Render) still
// resolves while the iOS-app build picks up the strong definitions.
__attribute__((weak)) extern "C" void mcle_metal_draw_vertices(int /*prim*/, int /*count*/,
                                                                const void* /*data*/,
                                                                int /*fmt*/, int /*shader*/) {}
__attribute__((weak)) extern "C" unsigned long long mcle_metal_draw_count(void) { return 0; }

// G3a: weak fallbacks for the display-list bridge so the probe link
// resolves without depending on mcle_ios_render. Real impls in
// Render/MetalContext.mm record + replay DrawVertices payloads.
__attribute__((weak)) extern "C" int  mcle_glbridge_gen_lists(int /*range*/)            { return 1; }
__attribute__((weak)) extern "C" void mcle_glbridge_begin_list(int /*id*/, int /*mode*/) {}
__attribute__((weak)) extern "C" void mcle_glbridge_end_list(void)                       {}
__attribute__((weak)) extern "C" void mcle_glbridge_call_list(int /*id*/)                {}
__attribute__((weak)) extern "C" void mcle_glbridge_release_lists(int /*id*/, int /*range*/) {}

// G3d-step3: weak fallbacks for the matrix bridge.
__attribute__((weak)) extern "C" void mcle_glbridge_matrix_mode(int /*mode*/)            {}
__attribute__((weak)) extern "C" void mcle_glbridge_load_identity(void)                  {}
__attribute__((weak)) extern "C" void mcle_glbridge_load_matrix(const float* /*m*/)      {}
__attribute__((weak)) extern "C" void mcle_glbridge_mult_matrix(const float* /*m*/)      {}
__attribute__((weak)) extern "C" void mcle_glbridge_push_matrix(void)                    {}
__attribute__((weak)) extern "C" void mcle_glbridge_pop_matrix(void)                     {}
__attribute__((weak)) extern "C" void mcle_glbridge_translate(float, float, float)       {}
__attribute__((weak)) extern "C" void mcle_glbridge_rotate(float, float, float, float)   {}
__attribute__((weak)) extern "C" void mcle_glbridge_scale(float, float, float)           {}
__attribute__((weak)) extern "C" void mcle_glbridge_metal_perspective(float, float, float, float) {}

// G3f: weak fallbacks for the GL_CURRENT_COLOR bridge.
__attribute__((weak)) extern "C" void mcle_glbridge_color4f(float, float, float, float)  {}
__attribute__((weak)) extern "C" void mcle_glbridge_color3f(float, float, float)         {}
__attribute__((weak)) extern "C" void mcle_glbridge_color4ub(unsigned char, unsigned char,
                                                              unsigned char, unsigned char) {}

// G4-step2: weak fallbacks for the texture bridge.
__attribute__((weak)) extern "C" unsigned int mcle_glbridge_gen_texture(void) { return 1; }
__attribute__((weak)) extern "C" void mcle_glbridge_gen_textures_n(int, unsigned int*)   {}
__attribute__((weak)) extern "C" void mcle_glbridge_delete_texture(unsigned int)         {}
__attribute__((weak)) extern "C" void mcle_glbridge_bind_texture(unsigned int)           {}
__attribute__((weak)) extern "C" void mcle_glbridge_tex_image_2d_rgba(unsigned int, int, int,
                                                                        const void*)    {}
__attribute__((weak)) extern "C" unsigned int mcle_glbridge_get_bound_texture(void)      { return 0; }
__attribute__((weak)) extern "C" unsigned int mcle_glbridge_load_or_get_png_path(const char*) { return 0; }
class CTelemetryManager;
CTelemetryManager *TelemetryManager = nullptr;

// Out-of-line definitions for upstream class statics that were declared
// with in-class initializers but never defined out-of-line in the .cpp
// chain we have. MSVC tolerates this; macOS/iOS clang+ld require the
// definition for any odr-use (e.g. taking the address, passing by
// reference, or initialiser-list evaluation).
//
// Values match the in-class declarations in the corresponding headers.
const int ChestTile::TYPE_TRAP;
const int ZonedChunkStorage::CHUNKS_PER_ZONE_BITS = 5;
const int ZonedChunkStorage::CHUNKS_PER_ZONE = 1 << ZonedChunkStorage::CHUNKS_PER_ZONE_BITS;

// DLCManager constructor + destructor stub bodies. Real upstream
// implementations live in DLCManager.cpp which is blocked on the UI/
// Iggy chain (Phase D) and cannot compile until the GL ES 3.0 renderer
// bringup. Provide trivial bodies here so anything that holds a
// DLCManager value (CMinecraftApp, ConsoleSaveFileConverter, ...) can
// link. Real iOS DLC implementation lands under Minecraft.Client/iOS/
// DLC/ in a later phase.
DLCManager::DLCManager() {}
DLCManager::~DLCManager() {}

// Stubs for upstream gameplay-host class methods whose .cpp files are
// blocked on the UI / render chain (Minecraft.cpp, MinecraftServer.cpp,
// LevelRenderer.cpp). The real bodies come back when Phase D2's GL ES
// renderer lands and unblocks those TUs. Until then, every stub is a
// safe no-op returning nullptr / false / 0; gameplay code that calls
// these in dev-build init paths will see "no server", "no players",
// etc and route around them.

// MinecraftServer.cpp now compiles and is linked into the lib, so its
// real method bodies are emitted there. The static `server` pointer
// is also defined in MinecraftServer.cpp's TU. Nothing to stub here
// for MinecraftServer anymore.

// Minecraft - the platform client app singleton accessor. Returns the
// static m_instance pointer that MCLEGameLoop sets to its shim. Real
// upstream Minecraft.cpp does the same: `return m_instance;`.
Minecraft *Minecraft::GetInstance()                         { return Minecraft::m_instance; }

// PlayerConnection
INetworkPlayer *PlayerConnection::getNetworkPlayer()        { return nullptr; }

// ServerPlayer.cpp now compiles and is in the lib, so its real method
// bodies (incl ~ServerPlayer + vtable) are emitted there.

// LevelRenderer.cpp now compiles and is in the lib, so its real
// `DestroyedTileManager` method bodies are emitted there. Nothing
// to stub here for LevelRenderer anymore.

// Legacy OpenGL function bodies. iOS doesn't have legacy GL; calls
// route through the Metal-backed C4JRender_iOS in Phase D2. For now
// these are no-ops so any TU that calls them links cleanly without
// pulling actual GL into the .ipa.
//
// G2b: defined with C++ linkage (no extern "C") so the header decls
// in iOS_WinCompat.h can declare upstream-wrapper overloads.
// Track GL_DEPTH_TEST and GL_BLEND. Other caps are no-op.
//   GL_DEPTH_TEST 0x0B71: HUD path needs it off so HUD fragments at
//     z=0.5 don't lose to closer world depth.
//   GL_BLEND 0x0BE2: sky/sun/cloud/HUD passes need srcAlpha/oneMinusSrcAlpha
//     blending so transparent edges of sprites blend with the framebuffer
//     instead of writing rgba=0 directly (visible black box around sun etc).
extern "C" void mcle_glbridge_set_depth_test(int enabled);
extern "C" void mcle_glbridge_set_blend_enabled(int enabled);
extern "C" void mcle_glbridge_set_alpha_test(int enabled);
extern "C" void mcle_glbridge_set_fog_enabled(int enabled);
void glEnable(unsigned int cap) {
    if (cap == 0x0B71 /*GL_DEPTH_TEST*/) mcle_glbridge_set_depth_test(1);
    else if (cap == 0x0BE2 /*GL_BLEND*/)      mcle_glbridge_set_blend_enabled(1);
    else if (cap == 0x0BC0 /*GL_ALPHA_TEST*/) mcle_glbridge_set_alpha_test(1);
    else if (cap == 0x0B60 /*GL_FOG*/)        mcle_glbridge_set_fog_enabled(1);
}
void glDisable(unsigned int cap) {
    if (cap == 0x0B71 /*GL_DEPTH_TEST*/) mcle_glbridge_set_depth_test(0);
    else if (cap == 0x0BE2 /*GL_BLEND*/)      mcle_glbridge_set_blend_enabled(0);
    else if (cap == 0x0BC0 /*GL_ALPHA_TEST*/) mcle_glbridge_set_alpha_test(0);
    else if (cap == 0x0B60 /*GL_FOG*/)        mcle_glbridge_set_fog_enabled(0);
}
void glClear(unsigned int)                                 {}
void glClearColor(float, float, float, float)              {}
void glViewport(int, int, int, int)                        {}
void glPushMatrix(void)                                    { mcle_glbridge_push_matrix(); }
void glPopMatrix(void)                                     { mcle_glbridge_pop_matrix(); }
void glLoadIdentity(void)                                  { mcle_glbridge_load_identity(); }
void glMatrixMode(unsigned int mode)                       { mcle_glbridge_matrix_mode((int)mode); }
void glTranslatef(float x, float y, float z)               { mcle_glbridge_translate(x, y, z); }
void glRotatef(float angle, float x, float y, float z)     { mcle_glbridge_rotate(angle, x, y, z); }
void glScalef(float x, float y, float z)                   { mcle_glbridge_scale(x, y, z); }
// glColor4f also syncs Tesselator's internal _col so subsequent
// t->vertexUV() emits pick up the new color. Without this, vertex colors
// stay at whatever the last t->color() call set (e.g. alpha=0 leftover
// from sunrise glow), and the sun renders as a black box because every
// vertex multiplies the texture by alpha=0.
extern "C" void Tesselator_setColorBridge(float r, float g, float b, float a);
void glColor4f(float r, float g, float b, float a) {
    mcle_glbridge_color4f(r, g, b, a);
    Tesselator_setColorBridge(r, g, b, a);
}
void glBegin(unsigned int)                                 {}
void glEnd(void)                                           {}
void glVertex3f(float, float, float)                       {}
void glTexCoord2f(float, float)                            {}
void glNewList(int id, int mode)                           { mcle_glbridge_begin_list(id, mode); }
void glEndList(void)                                       { mcle_glbridge_end_list(); }
void glCallList(int id)                                    { mcle_glbridge_call_list(id); }
void glDeleteLists(int id, int range)                      { mcle_glbridge_release_lists(id, range); }
int  glGenLists(int range)                                 { return mcle_glbridge_gen_lists(range); }
void glBindTexture(unsigned int /*target*/, unsigned int id)  { mcle_glbridge_bind_texture(id); }
void glTexParameteri(unsigned int, unsigned int, int)      {}
void glDepthFunc(unsigned int)                             {}
void glAlphaFunc(unsigned int, float)                      {}
// glBlendFunc: route src/dst factors to the bridge. Current Metal
// pipeline bakes srcAlpha/oneMinusSrcAlpha as the static blend func,
// so the bridge ignores them for now - sun's additive (srcAlpha,ONE)
// renders as srcAlpha-over instead, which still removes the black box
// without breaking other passes.
extern "C" void mcle_glbridge_set_blend_func(int src, int dst);
void glBlendFunc(unsigned int src, unsigned int dst) {
    mcle_glbridge_set_blend_func((int)src, (int)dst);
}
void glShadeModel(unsigned int)                            {}
// glDepthMask: route to depth-write flag. renderSky calls glDepthMask(false)
// before drawing skyList so the dome reads but doesn't write depth - that's
// what lets sun/moon (drawn after, against the same depth buffer) pass the
// less-than test against terrain instead of being clipped by the dome.
extern "C" void mcle_glbridge_set_depth_write(int enabled);
void glDepthMask(unsigned char flag) {
    mcle_glbridge_set_depth_write(flag ? 1 : 0);
}
void glColorMask(unsigned char, unsigned char, unsigned char, unsigned char) {}
void glFrontFace(unsigned int)                             {}
void glCullFace(unsigned int)                              {}
void glPointSize(float)                                    {}
void glLineWidth(float)                                    {}
void glHint(unsigned int, unsigned int)                    {}
void glPolygonOffset(float, float)                         {}
void glScissor(int, int, int, int)                         {}
void glOrtho(double, double, double, double, double, double) {}
void glFrustum(double, double, double, double, double, double) {}
void glStencilFunc(unsigned int, int, unsigned int)        {}
void glStencilOp(unsigned int, unsigned int, unsigned int) {}
void glStencilMask(unsigned int)                           {}
void glClearStencil(int)                                   {}
void glClearDepth(double)                                  {}
unsigned int glGetError(void)                              { return 0; }
void glGenTextures(int n, unsigned int* out)               { mcle_glbridge_gen_textures_n(n, out); }
void glDeleteTextures(int n, const unsigned int* ids) {
    if (!ids) return;
    for (int i = 0; i < n; i++) mcle_glbridge_delete_texture(ids[i]);
}
// Upstream-wrapper variants used by MemoryTracker.cpp.
unsigned int glGenTextures(void)                           { return mcle_glbridge_gen_texture(); }
void         glDeleteTextures(unsigned int id)             { mcle_glbridge_delete_texture(id); }
// glTexImage2D(target, level, internalFormat, width, height, border, format, type, pixels)
// Uploads to the currently bound texture. Only level 0 + RGBA 8-bit is
// supported for now; mipmap levels and other formats are no-op.
void glTexImage2D(unsigned int /*target*/, int level, int /*internalFormat*/,
                  int width, int height, int /*border*/,
                  unsigned int /*format*/, unsigned int /*type*/,
                  const void* pixels) {
    if (level != 0) return;
    unsigned int id = mcle_glbridge_get_bound_texture();
    if (id == 0) return;
    mcle_glbridge_tex_image_2d_rgba(id, width, height, pixels);
}
void glTexSubImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*) {}
void glPixelStorei(unsigned int, int)                      {}
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*) {}
void glEnableClientState(unsigned int)                     {}
void glDisableClientState(unsigned int)                    {}
void glVertexPointer(int, unsigned int, int, const void*)  {}
void glColorPointer(int, unsigned int, int, const void*)   {}
void glTexCoordPointer(int, unsigned int, int, const void*) {}
void glNormalPointer(unsigned int, int, const void*)       {}
void glDrawArrays(unsigned int, int, int)                  {}
void glDrawElements(unsigned int, int, unsigned int, const void*) {}
void glColor3f(float r, float g, float b)                  { mcle_glbridge_color3f(r, g, b); }
void glColor3ub(unsigned char r, unsigned char g, unsigned char b) {
    mcle_glbridge_color4ub(r, g, b, 255);
}
void glColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    mcle_glbridge_color4ub(r, g, b, a);
}
void glColor4ubv(const unsigned char* v) {
    if (v) mcle_glbridge_color4ub(v[0], v[1], v[2], v[3]);
}
void glVertex2f(float, float)                              {}
void glVertex2i(int, int)                                  {}
void glNormal3f(float, float, float)                       {}
void glLightfv(unsigned int, unsigned int, const float*)   {}
void glLight(unsigned int, unsigned int, float)            {} // upstream Lighting.cpp
// FloatBuffer overload Lighting::turnOn passes vector data to.
class FloatBuffer;
void glLight(unsigned int, unsigned int, FloatBuffer *)    {}
// IntBuffer overload OffsettedRenderList::render uses.
class IntBuffer;
void glCallLists(IntBuffer *)                              {}
// Lighting::turnOn calls this with the ambient color buffer.
void glLightModel(unsigned int, FloatBuffer *)             {}
void glColorMaterial(unsigned int, unsigned int)           {}
void glLightModelfv(unsigned int, const float*)            {}
// glGetFloat(matrix, FloatBuffer*) - Frustum::calculateFrustum (Frustum.cpp:56)
// calls this with GL_PROJECTION_MATRIX (0x0BA7) or GL_MODELVIEW_MATRIX
// (0x0BA6) to read the live matrices, then computes the 6 clip planes for
// cubeInFrustum. With a no-op shim the planes stay zero and every cube
// (including each renderAdvancedClouds cube cell) gets rejected. Wire it
// to our real matrix stacks so frustum culling works.
#include "../../../upstream/Minecraft.World/FloatBuffer.h"
extern "C" void mcle_glbridge_get_modelview(float *out16);
extern "C" void mcle_glbridge_get_projection(float *out16);
void glGetFloat(int param, FloatBuffer *buf) {
    if (!buf) return;
    float m[16];
    if (param == 0x0BA7 /*GL_PROJECTION_MATRIX*/) {
        mcle_glbridge_get_projection(m);
    } else if (param == 0x0BA6 /*GL_MODELVIEW_MATRIX*/) {
        mcle_glbridge_get_modelview(m);
    } else {
        return;
    }
    for (int i = 0; i < 16; i++) buf->put(m[i]);
}
void glTexGeni(unsigned int, unsigned int, int)            {} // TheEndPortalRenderer.cpp
void glTexGenfv(unsigned int, unsigned int, const float*)  {} // TheEndPortalRenderer.cpp
void glMaterialfv(unsigned int, unsigned int, const float*) {}
// Fog parameter shim. Upstream GameRenderer::setupFog issues:
//   glFog(GL_FOG_COLOR, vec4)        - via glFogfv
//   glFogi(GL_FOG_MODE, GL_LINEAR)   - we always linear; no-op
//   glFogf(GL_FOG_START, near)
//   glFogf(GL_FOG_END, far)
// GL constants: GL_FOG_COLOR=0x0B66, GL_FOG_MODE=0x0B65,
// GL_FOG_START=0x0B63, GL_FOG_END=0x0B64.
extern "C" void mcle_glbridge_set_fog_color(float r, float g, float b, float a);
extern "C" void mcle_glbridge_set_fog_start(float v);
extern "C" void mcle_glbridge_set_fog_end(float v);
void glFogf(unsigned int pname, float param) {
    if (pname == 0x0B63 /*GL_FOG_START*/) mcle_glbridge_set_fog_start(param);
    else if (pname == 0x0B64 /*GL_FOG_END*/) mcle_glbridge_set_fog_end(param);
}
void glFogi(unsigned int, int) {}  // mode/distance hint - we always do linear
void glFogfv(unsigned int pname, const float *params) {
    if (pname == 0x0B66 /*GL_FOG_COLOR*/ && params) {
        mcle_glbridge_set_fog_color(params[0], params[1], params[2], params[3]);
    }
}
void glMultMatrixf(const float*)                           {}
void glLoadMatrixf(const float*)                           {}
void glGetFloatv(unsigned int, float*)                     {}
void glGetIntegerv(unsigned int, int*)                     {}
void glMultiTexCoord2f(unsigned int, float, float)         {}
void glMultiTexCoord2fv(unsigned int, const float*)        {}
void glActiveTexture(unsigned int)                         {}
void glClientActiveTexture(unsigned int)                   {}
