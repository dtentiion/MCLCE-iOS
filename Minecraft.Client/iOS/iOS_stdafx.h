// Precompiled-header analog for upstream Minecraft.World / Minecraft.Client
// translation units when compiled against the iOS toolchain.
//
// Upstream code starts every .cpp with `#include "stdafx.h"` and assumes it
// has dragged in the C++ standard library, the Win32 type aliases, and a
// handful of platform helpers. The Windows build provides that via per-
// project precompiled headers; we do not have those on iOS so we force-
// include this file at the top of every TU instead (see CMake target_compile_
// options -include flags).
//
// Keep this minimal. Anything added here is paid for by every TU that gets
// the force-include. If a single file needs an exotic header, include it
// directly in that file rather than expanding this list.

#pragma once

#if !defined(_WIN32) && !defined(_WIN64)

// Win32 type aliases (DWORD, BOOL, HANDLE, ...) that upstream public headers
// reference without guarding.
#include "iOS_WinCompat.h"
// 4J platform storage layer - exposes C4JStorage class with the
// EMessageResult / ESaveGameState enums upstream code references.
#include "4JLibs/inc/4J_Storage.h"
// 4J profile layer - PlayerUID, C_4JProfile, ProfileManager (extern).
#include "4JLibs/inc/4J_Profile.h"
// 4J render layer - extern declaration for the global RenderManager.
// Note: 4J_Render.h is included AFTER iOS_stdafx.h's `typedef
// C4JRenderStub C4JRender;` (which lives further down in this file)
// so the typedef is in scope by the time 4J_Render.h declares the
// global. We pull it in here near the top via a forward-declared
// pointer wrapper, with the actual definition in WorldProbe/probe_stub.cpp.
// See the include further down for the typedef of C4JRender.
// Sentient telemetry enum tags (mirrored from upstream's Orbis/PS3/
// PSVita/Durango copies which are byte-identical). TelemetryManager.h
// references ESen_CompeteOrCoop / ESen_FriendOrMatch by type.
#include "Sentient/SentientTelemetryCommon.h"
// iOS_app_stub.h goes later in this file (after the DLC headers are
// pre-included) so McleAppStub's m_dlcManager can hold a real
// DLCManager value rather than void*.

// C++ standard library bits upstream uses unguarded.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <cwchar>
#include <ctime>
#include <cassert>
#include <cfloat>

#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <queue>
#include <stack>
#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <iterator>
#include <limits>
#include <numeric>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>

// Upstream files write `using namespace std;` at file scope in their .h files
// (AABB.h, Vec3.h, Definitions.h all do it explicitly). Some upstream .cpp
// files however assume the `using` is already in scope by the time their
// translation unit starts (e.g. StringHelpers.cpp at file scope: `wstring
// toLower(const wstring& a)`). On other platforms upstream's stdafx.h chain
// includes a header that brings std into scope before these .cpps parse.
// Our stdafx.h is a no-op on iOS, so we make the same `using` happen here.
// Scoped to translation units that get this header force-included, which is
// only the world-probe target (not the App or Ruffle code paths).
#ifdef __cplusplus
using namespace std;
#endif

// Upstream files write `using namespace std;` at file scope and then refer to
// names like `vector` / `shared_ptr` / `string` without the std:: prefix. The
// `using` above brings them into scope; we still need the headers themselves
// included so the symbols actually exist.

// Forward declarations for upstream gameplay types referenced through pointers
// or shared_ptr by headers in the probe set. On other platforms these declar-
// ations come transitively through stdafx.h's chain of includes, which we
// shut off on iOS to avoid the avalanche of unrelated dependencies. Add to
// this list as the probe set grows and surfaces a new forward-declared name.
#ifdef __cplusplus
class Entity;
class Node;
class Biome;
class LivingEntity;
class BaseAttributeMap;
class AttributeModifier;
// ItemInstance is referenced as a non-pointer field in
// LivingEntity / Mob / Player (`ItemInstanceArray equipment;`).
// Use the real ItemInstance.h - included a bit later in this file
// after System.h is fully parsed (so ByteArrayTag inline body's
// System::arraycopy reference resolves).
class ItemInstance;
class HtmlString;
class Explosion;
class Container;
class LocalPlayer;
class ResourceLocation;
class SharedConstants;
// C4JRender / C4JThread / LevelGenerationOptions defined as stubs below.
class TileEntity;
class ServerPlayer;
class CombatTracker;
class GAME_SETTINGS;
class C_4JProfile;
class CDLCManager;
class FileNameMap;
class ConsoleSaveFile;

// Telemetry enum stub for ServerPlayer.h family. Real enum lives in
// platform-specific Sentient/TelemetryEnum.h. Few enumerators give
// upstream code something to compile against.
enum ETelemetryChallenges {
    eTelemetryChallenges_None = 0,
    eTelemetryChallenges_Unknown,
};

// Real eDebugSetting enum lives in Common/Console_Debug_enum.h; pre-
// included below so all enumerators (FreezeTime, CraftAnything,
// MobsDontAttack, etc) are visible.

// eTYPE_BOSS_MOB_PART is a console-branch RTTI slot upstream's Class.h
// does not declare. BossMobPart.h GetType() returns this; using the
// enum identifier eTYPE_NOTSET (0) means the macro substitutes a real
// eINSTANCEOF value rather than an int literal that would not implicitly
// convert. Resolved at the use site, after Class.h is parsed.
#ifndef eTYPE_BOSS_MOB_PART
#  define eTYPE_BOSS_MOB_PART eTYPE_NOTSET
#endif

// MOJANG_DATA mirrors App_structs.h:155 - eXUID + cape/skin filenames.
// Named struct (not typedef of unnamed) so it composes with elaborated
// `struct MOJANG_DATA*` references upstream code uses without any
// pre-declaration.
#ifndef MOJANG_DATA_DEFINED
#define MOJANG_DATA_DEFINED
struct MOJANG_DATA {
    int eXuid;
    wchar_t wchCape[32];
    wchar_t wchSkin[32];
};
#endif

// Stubs for upstream classes we cannot easily include without
// pulling huge subtrees. Each is defined as an empty struct so
// any pointer / pointer-to-incomplete reference compiles, and
// member-access calls fall through templated catch-alls (none
// of these methods are ever called by the probe).
// ConsoleGameRules: real upstream class. Lightweight Constants header
// gives us the class shell + EGameRuleType / EGameRuleAttr enums with
// zero heavy includes. The full ConsoleGameRules.h pulls 24 GameRules/*
// headers which we do not want force-included; files that need full
// behavior include ConsoleGameRules.h directly.
// Stub for upstream's heavyweight C4JRender. Textures.h asks for the
// nested texture-format enum value to declare a static field and
// uses TEXTURE_FORMAT_RxGyBzAw as a default arg. Real platforms
// expose a fuller enum; the probe just needs the names.
struct C4JRenderStub {
    enum eTextureFormat {
        eTextureFormat_None = 0,
        eTextureFormat_RGBA = 1,
        TEXTURE_FORMAT_RxGyBzAw = 2,
        TEXTURE_FORMAT_DXT1 = 3,
        TEXTURE_FORMAT_DXT3 = 4,
        TEXTURE_FORMAT_DXT5 = 5,
    };
    enum eViewportType {
        VIEWPORT_TYPE_FULLSCREEN = 0,
        VIEWPORT_TYPE_TOP        = 1,
        VIEWPORT_TYPE_BOTTOM     = 2,
        VIEWPORT_TYPE_LEFT       = 3,
        VIEWPORT_TYPE_RIGHT      = 4,
        VIEWPORT_TYPE_TOPLEFT    = 5,
        VIEWPORT_TYPE_TOPRIGHT   = 6,
        VIEWPORT_TYPE_BOTLEFT    = 7,
        VIEWPORT_TYPE_BOTRIGHT   = 8,
        VIEWPORT_TYPE_NONE       = 9,
    };
    enum ePixelShader {
        ePixelShader_Default = 0,
    };
    enum eVertexFormat {
        eVertexFormat_Default = 0,
    };
    struct Texture {};
    struct VertexBuffer {};
    struct IndexBuffer {};
    // Variadic catch-alls for upstream RenderManager.X() call sites.
    // The probe never executes rendering; real Metal-backed C4JRender
    // for the app-linked variant lives in Render/C4JRender_iOS.{h,mm}.
    template<class... A> bool   IsWidescreen(A...)   { return false; }
    template<class... A> int    GetWidth(A...)       { return 0; }
    template<class... A> int    GetHeight(A...)      { return 0; }
    template<class... A> void   StartFrame(A...)     {}
    template<class... A> void   Present(A...)        {}
    template<class... A> void   Clear(A...)          {}
    template<class... A> void   Tick(A...)           {}
    template<class... A> void   MatrixSetIdentity(A...) {}
    template<class... A> void   MatrixPush(A...)     {}
    template<class... A> void   MatrixPop(A...)      {}
    template<class... A> void   MatrixMult(A...)     {}
    template<class... A> void   DrawVertices(A...)   {}
    template<class... A> void   DrawVertexBuffer(A...) {}
    template<class... A> void   SetViewport(A...)    {}
    template<class... A> void   SetBlendMode(A...)   {}
    template<class... A> void   SetDepthTest(A...)   {}
    template<class... A> void   BindTexture(A...)    {}
    template<class... A> int    CreateTexture(A...)  { return 0; }
    template<class... A> void   FreeTexture(A...)    {}
    // Command buffer interface upstream LevelRenderer.cpp uses to
    // record draws into a reusable buffer.
    template<class... A> bool   CBuffCall(A...)               { return false; }
    template<class... A> bool   CBuffCallCutOut(A...)         { return false; }
    template<class... A> bool   CBuffCallMultiple(A...)       { return false; }
    template<class... A> void   CBuffDeferredModeStart(A...)  {}
    template<class... A> void   CBuffLockStaticCreations(A...) {}
    template<class... A> int    CBuffSize(A...)               { return 0; }
    template<class... A> int    CBuffCreate(A...)             { return 0; }
    template<class... A> void   CBuffDelete(A...)             {}
    template<class... A> bool   CBuffLock(A...)               { return false; }
    template<class... A> void   CBuffUnlock(A...)             {}
    // Render-state setters Minecraft.cpp / LevelRenderer.cpp call.
    template<class... A> void   StateSetEnableViewportClipPlanes(A...) {}
    template<class... A> void   StateSetForceLOD(A...)        {}
    template<class... A> void   StateSetViewport(A...)        {}
    template<class... A> void   SetCameraPosition(A...)       {}
    template<class... A> void   InitialiseContext(A...)       {}
    template<class... A> void   DoScreenGrabOnNextPresent(A...) {}
    template<class... A> void   InternalScreenCapture(A...)   {}
    template<class... A> bool   IsHiDef(A...)                 { return true; }
};
// SharedConstants / C4JThread are real classes in Minecraft.World/
// and get pre-included below. Do not define stubs here.

// LevelGenerationOptions: full stub. Upstream uses it as a pointer
// returned from app.getLevelGenerationOptions() and called for
// methods like checkIntersects / requiresBaseSave / etc. Variadic
// template methods absorb whatever signature.
struct LevelGenerationOptions {
    template<class... A> bool checkIntersects(A...)    { return false; }
    template<class... A> bool requiresBaseSave(A...)   { return false; }
    template<class... A> void* getBaseSaveData(A...)   { return nullptr; }
    template<class... A> void deleteBaseSaveData(A...) {}
    template<class... A> bool isFromDLC(A...)          { return false; }
    template<class... A> bool isFromMashup(A...)       { return false; }
    template<class... A> bool isFeatureChunk(A...)     { return false; }
    template<class... A> int  getFeatureSeed(A...)     { return 0; }
    template<class... A> bool hasLoadedData(A...)      { return false; }
    template<class... A> bool isReady(A...)            { return true; }
    template<class... A> bool ready(A...)              { return true; }
    template<class... A> bool getuseFlatWorld(A...)    { return false; }
    template<class... A> bool useFlatWorld(A...)       { return false; }
    template<class... A> int  getWorldSeed(A...)       { return 0; }
    // Pos* return so MinecraftServer's `Pos *spawnPos = ...` typechecks.
    // Pos.h is pre-included further down so the type is visible by the
    // time this method body is instantiated; forward-decl keeps the
    // declaration parseable here too.
    class Pos* getSpawnPos() { return nullptr; }
    template<class... A> int  getDifficulty(A...)      { return 0; }
    template<class... A> int  getGameMode(A...)        { return 0; }
    template<class... A> bool getLevelHasBeenInCreative(A...) { return false; }
    template<class... A> bool getMapInteractionEnabled(A...)  { return false; }
    template<class... A> bool getCommandBlocksEnabled(A...)   { return false; }
    template<class... A> int  getInitialSpawnRadius(A...)     { return 0; }
};

// DLCSkinFile is the real upstream class (Minecraft.Client/Common/DLC/
// DLCSkinFile.h). Header is pre-included further down in this file so
// every TU sees the real definition.

// C4JRender is a heavyweight platform render class - the real one
// lives in platform-specific 4JLibs/. Files like Textures.h
// reference nested types via `C4JRender::Texture *` which need the
// class declared. Map the name to our stub via typedef.
typedef C4JRenderStub C4JRender;
// Now that C4JRender is a known type (alias to C4JRenderStub), pull
// in the iOS 4J_Render.h header which declares
// `extern C4JRender RenderManager;` so callers see the global.
// The actual instance is defined in WorldProbe/probe_stub.cpp.
#include "4JLibs/inc/4J_Render.h"
class IntArrayTag;
class CompoundTag;
class Player;
class Mob;
class DamageSource;
class MobEffect;
class ItemEntity;
class StructureFeatureSavedData;
class Connection;
// Real `class Minecraft` (Minecraft.Client/Minecraft.h) is pre-
// included further down, so member access on Minecraft instances
// (->soundEngine, ->skins, ...) compiles in callers.
// DLCPack: real header pre-included further down.
// FriendSessionInfo lives in Common/Network/SessionInfo.h which has a
// platform-conditional chain we do not cover. UIStructs.h references
// it as a pointer field; forward-decl is enough for parses.
class FriendSessionInfo;
// UIScene is the SWF-backed scene class; UIControl.h:47 references it
// as a pointer return type without including UIScene.h (circular -
// UIScene.h itself includes UIControl_Base.h). Forward-decl here so
// UIControl.h parses; actual class definition comes from UIScene.h
// when the cascade does include it.
class UIScene;
class UILayer;
class UIControl;
class UIControl_Base;
class UIControl_TextInput;
class ItemRenderer;
// CustomDrawData is an Iggy-adjacent opaque type used as
// `CustomDrawData*` in UIScene.h:231 and UIController.h. No definition
// exists anywhere in the upstream tree (it's a forward-decl shape).
// Forward-decl here so UIScene.h parses.
class CustomDrawData;
// UIVec2D is a 2D float vector used as a value-type member in
// IUIScene_AbstractContainerMenu.h. Upstream has no definition (the
// type is provided by the platform UI layer); a thin float pair
// matches the implied shape so member layout works.
struct UIVec2D { float x; float y; };
// Textures lives in Minecraft.Client/Textures.h. TexturePack.h
// references it as a pointer field.
class Textures;
// BufferedImage lives in Minecraft.Client/BufferedImage.h. Used as
// pointer in TexturePack.h's getDescTexture() return type.
class BufferedImage;
// Note: INetworkPlayer is defined by the real header pre-included
// further down. No forward-decl here to avoid duplicate-declaration.
// ListTag is a template `template<class T> class ListTag`, do not
// forward-declare as plain class.
#endif

// Math + type macros that upstream Definitions.h provides at file scope.
// Random.h refers to `byte`, Mth.cpp refers to `PI` and `HALF_PI` without
// pulling Definitions.h directly, expecting upstream's stdafx.h chain to
// have brought them in. Mirror the same names here so the probe sees them.
#ifndef PI
#  define PI       (3.141592654f)
#endif
#ifndef HALF_PI
#  define HALF_PI  (1.570796327f)
#endif

// extraX64.h does `typedef unsigned char byte;` at file scope. Several
// upstream headers (Random.h, DataInput.h, FileHeader.h) reference `byte`
// unqualified expecting that typedef to be in scope.
#ifdef __cplusplus
typedef unsigned char byte;
#endif

// PlayerUID lives in iOS/4JLibs/inc/4J_Profile.h (already pre-included
// via the iOS_stdafx.h `#include "4JLibs/inc/4J_Storage.h"` chain at the
// top of this file via iOS_app_stub.h forward refs). Definition kept
// in 4J_Profile.h so it can stay in sync with the C_4JProfile class
// declaration without duplication.

// Memory-section profiler hook declared in upstream stdafx.h:
//     void MemSect(int sect);
// Used by NbtIo.cpp etc to mark allocation regions for the platform's
// memory tracker. We do not have a tracker on iOS, no-op it out.
#ifdef __cplusplus
inline void MemSect(int) {}
#endif

// Pre-include ArrayWithLength.h + System.h so the byteArray / charArray
// typedefs and the System class body are visible before any other
// upstream header references them. Without this, upstream files like
// Tag.h / InputOutputStream.h / DataInput.h hit `byteArray` and
// `System::arraycopy` before ArrayWithLength.h has been parsed (the
// upstream include graph is order-sensitive and only works on other
// platforms because their stdafx.h chain parses these in the right
// sequence; ours is a no-op).
//
// ArrayWithLength.h is patched at build time to skip ItemInstance.h
// on iOS so this pre-include does not pull in the NBT cascade.
#ifdef __cplusplus
#include "Definitions.h"
#include "ArrayWithLength.h"
// ItemInstanceArray typedef from upstream's ArrayWithLength.h
// (the line we patch out for iOS to avoid the NBT cascade).
typedef arrayWithLength<std::shared_ptr<ItemInstance> > ItemInstanceArray;
#include "System.h"
#include "Mth.h"
#include "Random.h"
#include "DataInputStream.h"
#include "DataOutputStream.h"
#include "ByteArrayInputStream.h"
#include "Icon.h"
#include "TilePos.h"
#include "ChunkPos.h"
#include "Pos.h"
// Bring in real ItemInstance now that System / ArrayWithLength / NBT
// chain headers are all parsed. ItemInstance.h pulls com.mojang.nbt
// -> NbtIo -> CompoundTag -> ByteArrayTag where ByteArrayTag's
// inline copy() body calls System::arraycopy. System is defined by
// this point so the call resolves.
#include "ItemInstance.h"
#include "AttributeInstance.h"
#endif

// Upstream enum definitions. App_enums.h has eMinecraftColour /
// eGameSetting / eGameMode / eXuiAction / EControllerActions /
// EHTMLFontSize / eMCLang and many more. Attribute.h has
// eATTRIBUTE_ID. Both files are header-include-free pure-enum
// declarations - safe to pre-include for the probe.
#ifdef __cplusplus
// Iggy SWF runtime API surface. iOS replaces Iggy with the Ruffle-
// based SWF runtime in third_party/ruffle_ios/, but upstream UI
// headers (UIScene.h, UIControl_Base.h, UIBitmapFont.h) reference
// Iggy types as members and through pointer signatures. The shim
// declares enough of the Iggy surface for those headers to parse.
#include "Iggy/iggy.h"
// App_enums.h lives in Minecraft.Client/Common, escape upstream/
// Minecraft.World via .. then descend into the Common/ tree.
#include "../Minecraft.Client/Common/App_enums.h"
#include "../Minecraft.Client/Common/App_Defines.h"
// App_structs.h has UI / C4JStorage / GAME_SETTINGS deps that conflict
// with our shims. We forward-declare MOJANG_DATA in Consoles_App stub
// and patch upstream call sites instead.
#include "Class.h"
#include "Attribute.h"
#include "AttributeModifier.h"
// Auto-generated localization string ID #defines (IDS_OK,
// IDS_ATTRIBUTE_NAME_GENERIC_MAXHEALTH, IDS_HOW_TO_PLAY_MENU_*, etc).
// 2287 entries, zero includes, pure #define so it is safe to pre-
// include for the probe. Use the Windows64 string table because
// the iOS port already targets the Windows64 string set on the
// menu side.
#include "../Minecraft.Client/Windows64Media/strings.h"
#include "../Minecraft.Client/Common/Console_Awards_enum.h"
#include "../Minecraft.Client/Common/Console_Debug_enum.h"
#include "../Minecraft.Client/Common/Minecraft_Macros.h"
#include "../Minecraft.Client/Common/Potion_Macros.h"
#include "../Minecraft.Client/Common/Colours/ColourTable.h"
#include "Exceptions.h"
#include "StringHelpers.h"
#include "../Minecraft.Client/Common/GameRules/ConsoleGameRulesConstants.h"
// Pure virtual interface - lightweight, no chained includes besides
// `class Socket` forward decl. Brings IsHost / GetSmallId / SendData /
// etc into scope for files that reach into INetworkPlayer.
#include "../Minecraft.Client/Common/Network/NetworkPlayerInterface.h"
// StringTable: real upstream class with map<wstring, wstring> backing
// store. DLCTexturePack reaches in for getString(); pre-include so
// member access compiles without extra include directives in callers.
#include "../Minecraft.Client/StringTable.h"
// DLCFile + DLCSkinFile + DLCPack real headers. DLCPack.h's inline
// getSkinFile() does static_cast<DLCSkinFile*>(DLCFile*), needs both
// as complete types. ServerLevel uses pDLCPack->hasPurchasedFile().
#include "../Minecraft.Client/Common/DLC/DLCFile.h"
#include "../Minecraft.Client/Common/DLC/DLCSkinFile.h"
#include "../Minecraft.Client/Common/DLC/DLCPack.h"
// Real LevelRuleset (game-rule root). PlayerList calls
// app.getGameRuleDefinitions()->postProcessPlayer(player). The McleAppStub
// returns LevelRuleset* and the body of postProcessPlayer comes from
// the real GameRuleDefinition.h chain.
#include "../Minecraft.Client/Common/GameRules/LevelRuleset.h"
// CTelemetryManager - PlayerList / MinecraftServer use the global
// TelemetryManager pointer for telemetry events. Real header is light
// (only pulls UIEnums.h which has no further deps).
#include "../Minecraft.Client/Common/Telemetry/TelemetryManager.h"
// Real Minecraft (the platform client app class). MultiPlayerLevel
// reaches into Minecraft::soundEngine, ServerLevel reaches into
// Minecraft::skins, etc. Forward-decl is not enough; we need the full
// class.
#include "../Minecraft.Client/Minecraft.h"

// Minimal SoundEngine for the probe. Real upstream SoundEngine.h pulls
// miniaudio.h (~95k lines) which would balloon every-TU compile time.
// MultiPlayerLevel.cpp invokes only play() and schedule() on
// minecraft->soundEngine; the variadic stubs absorb the call shape.
// Real audio runs through the iOS Audio/SoundEngine.cpp at runtime,
// not through this stub.
//
// Defined AFTER Minecraft.h's forward decl so the names match. TUs
// that include real SoundEngine.h directly (only ServerPlayer.cpp at
// the moment) don't get added to the probe lib for now.
class SoundEngine {
public:
    SoundEngine() {}
    template<class... A> void init(A...)     {}
    template<class... A> void destroy(A...)  {}
    template<class... A> void shutdown(A...) {}
    template<class... A> void play(A...)     {}
    template<class... A> void schedule(A...) {}
    template<class... A> void stop(A...)     {}
    template<class... A> void stopAll(A...)  {}
    template<class... A> void pauseAll(A...) {}
    template<class... A> void resumeAll(A...) {}
    template<class... A> void setVolume(A...) {}
    template<class... A> void update(A...)   {}
    template<class... A> void tick(A...)     {}
    template<class... A> bool isPlaying(A...) { return false; }
    template<class... A> int  getMusicTrackCount(A...) { return 0; }
    template<class... A> void playMusic(A...) {}
    template<class... A> void stopMusic(A...) {}
    template<class... A> int  playStreaming(A...) { return 0; }
    template<class... A> int  playStreamed(A...)  { return 0; }
    template<class... A> int  playUI(A...)        { return 0; }
    template<class... A> bool GetIsPlayingStreamingGameMusic(A...) { return false; }
    template<class... A> bool IsPlayingMusic(A...) { return false; }
    template<class... A> bool IsPlayingStreaming(A...) { return false; }
    template<class... A> void StopAllStreaming(A...) {}
    template<class... A> void StopAllSounds(A...)    {}
    template<class... A> void SetMusicVolume(A...)   {}
    template<class... A> void SetSFXVolume(A...)     {}
};
// Real MultiPlayerGameMode - PlayerList chains through
// Minecraft::gameMode->getTutorial(). Header is light (only pulls
// GameMode.h which has zero deps).
#include "../Minecraft.Client/MultiPlayerGameMode.h"
// MemoryTracker - LevelRenderer.cpp uses MemoryTracker::genLists()
// and friends. Header is 28 lines, no heavy deps.
#include "../Minecraft.Client/MemoryTracker.h"
// Font - Minecraft.cpp does `new Font(...)` so the type must be
// complete. Header forward-decls IntBuffer/Options/Textures/
// ResourceLocation but only via pointer; the class body is light.
#include "../Minecraft.Client/Font.h"
// Gui - Minecraft.cpp does `new Gui(this)`. Pre-include for the
// constructor signature.
#include "../Minecraft.Client/Gui.h"
// ScreenSizeCalculator - Minecraft.cpp constructs one on the stack.
#include "../Minecraft.Client/ScreenSizeCalculator.h"
// Mouse class stub. Real upstream version lives in
// Minecraft.Client/stubs.h alongside a `class Color` that conflicts
// with `Minecraft.World/Color.h`. We can't pre-include stubs.h
// without breaking Color, so we ship just the Mouse + Keyboard
// shapes the gameplay-host classes need.
class Mouse {
public:
    static void create() {}
    static void destroy() {}
    static int  getX()                   { return 0; }
    static int  getY()                   { return 0; }
    static bool isButtonDown(int)        { return false; }
};
class Keyboard {
public:
    static void create() {}
    static void destroy() {}
    static bool isKeyDown(int)           { return false; }
};
class Display {
public:
    static void create() {}
    static void destroy() {}
    static int  getWidth()               { return 1280; }
    static int  getHeight()              { return 720; }
    static void update()                 {}
    static void swapBuffers()            {}
    static bool isCloseRequested()       { return false; }
    static void setTitle(const char*)    {}
};
// Tutorial.h transitively pulls UIScene (UI subsystem replaced by
// SWF on iOS) so we cannot pre-include the full header. The light
// TutorialEnum.h has no deps and brings the eTutorial_State enum
// into scope so PlayerList::placeNewPlayer's
// `e_Tutorial_State_Food_Bar` reference resolves.
#include "../Minecraft.Client/Common/Tutorial/TutorialEnum.h"
// Minimal Tutorial class stub so PlayerList.cpp's
// gameMode->getTutorial()->isStateCompleted(state) call resolves
// without pulling Tutorial.h (which has UIScene deps). Real Tutorial
// state lives in Tutorial.cpp which is in the lib already
// (Common/Tutorial/AreaConstraint.cpp etc are green per the auto-probe).
class Tutorial {
public:
    template<class... A> bool isStateCompleted(A...) { return false; }
    template<class... A> void setStateCompleted(A...) {}
    template<class... A> bool isFullTutorial(A...)   { return false; }
};
#include "FileHeader.h"
#include "SharedConstants.h"
#include "C4JThread.h"
// iOS_app_stub.h is pre-included LAST so the McleAppStub has full
// access to upstream types like DLCManager, ModelPart, MOJANG_DATA
// without forward-decl gymnastics.
#include "iOS_app_stub.h"
#endif

#endif // !_WIN32 && !_WIN64
