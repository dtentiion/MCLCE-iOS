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
#endif

#endif // !_WIN32 && !_WIN64
