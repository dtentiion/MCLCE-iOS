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

// Minecraft - the platform client app singleton accessor.
Minecraft *Minecraft::GetInstance()                         { return nullptr; }

// PlayerConnection
INetworkPlayer *PlayerConnection::getNetworkPlayer()        { return nullptr; }

// ServerPlayer destructor - emits the vtable + typeinfo so any TU
// dynamic_cast'ing or RTTI-lookup'ing ServerPlayer can link.
ServerPlayer::~ServerPlayer() {}

// LevelRenderer.cpp now compiles and is in the lib, so its real
// `DestroyedTileManager` method bodies are emitted there. Nothing
// to stub here for LevelRenderer anymore.

// Legacy OpenGL function bodies. iOS doesn't have legacy GL; calls
// route through the Metal-backed C4JRender_iOS in Phase D2. For now
// these are no-ops so any TU that calls them links cleanly without
// pulling actual GL into the .ipa.
extern "C" {
void glEnable(unsigned int)                                {}
void glDisable(unsigned int)                               {}
void glClear(unsigned int)                                 {}
void glClearColor(float, float, float, float)              {}
void glViewport(int, int, int, int)                        {}
void glPushMatrix(void)                                    {}
void glPopMatrix(void)                                     {}
void glLoadIdentity(void)                                  {}
void glMatrixMode(unsigned int)                            {}
void glTranslatef(float, float, float)                     {}
void glRotatef(float, float, float, float)                 {}
void glScalef(float, float, float)                         {}
void glColor4f(float, float, float, float)                 {}
void glBegin(unsigned int)                                 {}
void glEnd(void)                                           {}
void glVertex3f(float, float, float)                       {}
void glTexCoord2f(float, float)                            {}
void glNewList(int, int)                                   {}
void glEndList(void)                                       {}
void glCallList(int)                                       {}
void glDeleteLists(int, int)                               {}
int  glGenLists(int)                                       { return 0; }
void glBindTexture(unsigned int, unsigned int)             {}
void glTexParameteri(unsigned int, unsigned int, int)      {}
void glDepthFunc(unsigned int)                             {}
void glAlphaFunc(unsigned int, float)                      {}
void glBlendFunc(unsigned int, unsigned int)               {}
void glShadeModel(unsigned int)                            {}
void glDepthMask(unsigned char)                            {}
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
void glGenTextures(int, unsigned int*)                     {}
void glDeleteTextures(int, const unsigned int*)            {}
void glTexImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*) {}
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
void glColor3f(float, float, float)                        {}
void glColor3ub(unsigned char, unsigned char, unsigned char) {}
void glColor4ub(unsigned char, unsigned char, unsigned char, unsigned char) {}
void glColor4ubv(const unsigned char*)                     {}
void glVertex2f(float, float)                              {}
void glVertex2i(int, int)                                  {}
void glNormal3f(float, float, float)                       {}
void glLightfv(unsigned int, unsigned int, const float*)   {}
void glLight(unsigned int, unsigned int, float)            {} // upstream Lighting.cpp
void glTexGeni(unsigned int, unsigned int, int)            {} // TheEndPortalRenderer.cpp
void glTexGenfv(unsigned int, unsigned int, const float*)  {} // TheEndPortalRenderer.cpp
void glMaterialfv(unsigned int, unsigned int, const float*) {}
void glFogf(unsigned int, float)                           {}
void glFogi(unsigned int, int)                             {}
void glFogfv(unsigned int, const float*)                   {}
void glMultMatrixf(const float*)                           {}
void glLoadMatrixf(const float*)                           {}
void glGetFloatv(unsigned int, float*)                     {}
void glGetIntegerv(unsigned int, int*)                     {}
void glMultiTexCoord2f(unsigned int, float, float)         {}
void glMultiTexCoord2fv(unsigned int, const float*)        {}
void glActiveTexture(unsigned int)                         {}
void glClientActiveTexture(unsigned int)                   {}
} // extern "C"
