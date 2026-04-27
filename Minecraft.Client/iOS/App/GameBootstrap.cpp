// Bootstrap entry points the iOS app calls into upstream gameplay code
// through. MinecraftViewController.mm's render loop calls
// mcle_game_tick() every frame. Until the Minecraft instance is fully
// constructable the tick body is a guarded no-op that only logs
// presence, so we can confirm the wire-up reaches the linked probe lib
// without crashing on a partially-initialised world.
//
// Flip MCLE_AUTOSTART_GAMEPLAY on in the build to turn the tick on by
// default. The user-trigger path (a debug controller chord routed
// through the SWF main menu) calls mcle_game_init() directly when the
// flag is off.

#include <os/log.h>
#include <stdint.h>

// On by default for the first .ipa boot test: the tick body is a
// safe no-op that only logs once a second, so even autostart can't
// crash. Turn off later with -DMCLE_AUTOSTART_GAMEPLAY=0 once a real
// Minecraft instance is constructed and the tick can do real work.
#ifndef MCLE_AUTOSTART_GAMEPLAY
#  define MCLE_AUTOSTART_GAMEPLAY 1
#endif

static int      g_initialized    = 0;
static uint64_t g_tick_count     = 0;
static const uint64_t kLogEveryN = 60; // ~1 log/sec at 60 fps

extern "C" void mcle_game_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
                     "[MCLE] mcle_game_init: bootstrap reached, probe lib linked.");
    // Future: instantiate the global `app` (auto-initialised at TU
    // load), construct a C4JRender, and allocate a Minecraft via new.
    // The Minecraft constructor pulls a wide init chain (level storage,
    // renderer, options) we have not stubbed yet, so we keep this
    // conservative for the first .ipa boot test.
}

extern "C" void mcle_game_tick(void) {
#if MCLE_AUTOSTART_GAMEPLAY
    if (!g_initialized) mcle_game_init();
    g_tick_count++;
    if ((g_tick_count % kLogEveryN) == 0) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
                         "[MCLE] tick %llu (probe lib running)",
                         (unsigned long long)g_tick_count);
    }
#else
    // Off by default: the user has to fire mcle_game_debug_start()
    // through a debug controller chord before any tick output appears.
    // Keeps first-launch crash logs clean.
    (void)g_tick_count;
#endif
}

// Called from MinecraftViewController.mm when the user fires the
// debug controller chord. Triggers init lazily.
extern "C" void mcle_game_debug_start(void) {
    mcle_game_init();
    os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
                     "[MCLE] debug_start: bootstrap initialised on user gesture.");
}
