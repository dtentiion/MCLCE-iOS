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
#include "iOS_app_stub.h"
// 4J platform storage layer - exposes C4JStorage class with the
// EMessageResult / ESaveGameState enums upstream code references.
#include "4JLibs/inc/4J_Storage.h"
// 4J profile layer - PlayerUID, C_4JProfile, ProfileManager (extern).
#include "4JLibs/inc/4J_Profile.h"
// Sentient telemetry enum tags (mirrored from upstream's Orbis/PS3/
// PSVita/Durango copies which are byte-identical). TelemetryManager.h
// references ESen_CompeteOrCoop / ESen_FriendOrMatch by type.
#include "Sentient/SentientTelemetryCommon.h"

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
    struct Texture {};
    struct VertexBuffer {};
    struct IndexBuffer {};
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
};

// DLCSkinFile is the real upstream class (Minecraft.Client/Common/DLC/
// DLCSkinFile.h). Header is pre-included further down in this file so
// every TU sees the real definition.

// C4JRender is a heavyweight platform render class - the real one
// lives in platform-specific 4JLibs/. Files like Textures.h
// reference nested types via `C4JRender::Texture *` which need the
// class declared. Map the name to our stub via typedef.
typedef C4JRenderStub C4JRender;
class IntArrayTag;
class CompoundTag;
class Player;
class Mob;
class DamageSource;
class MobEffect;
class ItemEntity;
class StructureFeatureSavedData;
class Connection;
// Real `class Minecraft` is the platform client app class (lives in
// Minecraft.Client/Minecraft.h). MultiPlayerLevel etc reference it
// via pointer, so a forward decl is enough for the probe parses.
class Minecraft;
// DLCPack lives in Minecraft.Client/DLCManager territory. TexturePack.h
// references it via `DLCPack* getDLCPack()` pointer. Forward decl is
// all we need for the probe parses.
class DLCPack;
// FriendSessionInfo lives in Common/Network/SessionInfo.h which has a
// platform-conditional chain we do not cover. UIStructs.h references
// it as a pointer field; forward-decl is enough for parses.
class FriendSessionInfo;
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
// App_enums.h lives in Minecraft.Client/Common, escape upstream/
// Minecraft.World via .. then descend into the Common/ tree.
#include "../Minecraft.Client/Common/App_enums.h"
#include "../Minecraft.Client/Common/App_Defines.h"
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
// DLCFile + DLCSkinFile real headers. DLCPack.h's inline getSkinFile()
// does static_cast<DLCSkinFile*>(DLCFile*), needs both as complete
// types at every TU. DLCSkinFile.h pulls HumanoidModel -> Model -> a
// chunk of the renderer chain; we accept the compile cost for parity.
#include "../Minecraft.Client/Common/DLC/DLCFile.h"
#include "../Minecraft.Client/Common/DLC/DLCSkinFile.h"
#include "FileHeader.h"
#include "SharedConstants.h"
#include "C4JThread.h"
#endif

#endif // !_WIN32 && !_WIN64
