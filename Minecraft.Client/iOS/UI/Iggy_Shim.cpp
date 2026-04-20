// Stub implementations for the Iggy API surface.
//
// Background: the upstream game drives its UI through Iggy, a closed-source
// SWF runtime by RAD Game Tools. On Windows the build links against
// iggy_w64.lib. Those libs do not exist for iOS. This file satisfies the
// linker so the game compiles, at the cost of showing no UI at runtime.
//
// The plan is to replace these stubs with calls into GameSWF, an open-source
// C++ SWF player. The .iggy files the game ships with are standard SWFs
// under a renamed extension, which GameSWF can consume. See ROADMAP.md.
//
// For now only a handful of lifecycle calls are stubbed. When we start
// linking upstream UI sources into the iOS build, the linker will complain
// about the rest and we add them incrementally.

#include "iggy.h"
#include "iggyperfmon.h"
#include "iggyexpruntime.h"

#include <cstdio>
#include <cstdlib>

namespace {

void iggy_warn_once(const char* fn) {
    static bool warned = false;
    if (!warned) {
        warned = true;
        std::fprintf(stderr,
            "[iggy-shim] Iggy calls are stubbed on iOS. "
            "First stubbed call: %s. UI will not render until GameSWF lands.\n",
            fn);
    }
}

} // namespace

// Lifecycle (called from UIController::UIController).
extern "C" void RADEXPLINK IggyInit(IggyAllocator* /*allocator*/) {
    iggy_warn_once("IggyInit");
}

extern "C" void RADEXPLINK IggyShutdown(void) {
    // no-op
}

// Every other Iggy entry point the upstream code references will be added
// here as compile/link failures reveal them. Keeping the surface minimal on
// purpose; a generator script under scripts/ will help with the bulk add.
