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
class ItemInstance;
class HtmlString;
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

// PlayerUID stub. Other platforms define this in their 4J_Profile.h;
// our iOS 4J_Profile.h has the same definition but it does not get
// included on iOS (upstream stdafx.h is a no-op for us, so its
// platform-4JLibs include chain never fires). Inline the definition
// here so the probe sees it at file scope.
#ifdef __cplusplus
struct PlayerUID {
    uint8_t bytes[16];
};
typedef PlayerUID* PPlayerUID;
#endif

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
#include "ArrayWithLength.h"
#include "System.h"
#include "Mth.h"
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
#include "Attribute.h"
#include "AttributeModifier.h"
// Auto-generated localization string ID #defines (IDS_OK,
// IDS_ATTRIBUTE_NAME_GENERIC_MAXHEALTH, IDS_HOW_TO_PLAY_MENU_*, etc).
// 2287 entries, zero includes, pure #define so it is safe to pre-
// include for the probe. Use the Windows64 string table because
// the iOS port already targets the Windows64 string set on the
// menu side.
#include "../Minecraft.Client/Windows64Media/strings.h"
#endif

#endif // !_WIN32 && !_WIN64
