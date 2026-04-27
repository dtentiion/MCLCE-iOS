// iOS top-level game shell.
//
// Bypasses the upstream Minecraft.cpp construction path (blocked on
// the tutorial / trial / UI cascade we replaced with SWF) and instead
// constructs the minimum subset of upstream gameplay classes that the
// simulation tick needs:
//
//   1. McRegionLevelStorageSource against Documents/saves/
//   2. selectLevel() to load a save -> shared_ptr<LevelStorage>
//   3. prepareLevel() -> LevelData metadata
//   4. LevelSettings(LevelData*) -> world settings
//   5. new Level(...) -> the world simulation
//
// On each tick from the iOS render loop we call Level::tick() +
// tickEntities() so the simulation actually runs.
//
// This file is parity-preserving for everything below the top level
// (Entity, Level, Player, Mob, NBT, packets, simulation logic). The
// only thing it replaces is Minecraft::init() / Minecraft::tick().
//
// All progress + crash points log through os_log so 3uTools / iMazing
// / Console.app can show them on a Windows or Mac dev machine.

#include "iOS_stdafx.h"

#include <os/log.h>
#include <memory>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <exception>

#include "../../../upstream/Minecraft.World/File.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/LevelData.h"
#include "../../../upstream/Minecraft.World/LevelSettings.h"
#include "../../../upstream/Minecraft.World/LevelStorage.h"
#include "../../../upstream/Minecraft.World/LevelSummary.h"
#include "../../../upstream/Minecraft.World/McRegionLevelStorageSource.h"
#include "../../../upstream/Minecraft.Client/ServerLevel.h"

#include "4JLibs/inc/4J_Storage.h"

#define MCLE_LOG(fmt, ...) \
    os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT, "[MCLE] " fmt, ##__VA_ARGS__)

namespace {

enum InitState {
    kStateUnstarted = 0,
    kStateReadyIdle = 1,   // bootstrap finished, no save to drive simulation
    kStateTicking   = 2,   // Level constructed, ticking each frame
    kStateFailed    = -1,  // an init step crashed/returned null; tick stays idle
};

// File-scope state. Lives for the lifetime of the app process.
McRegionLevelStorageSource  *g_levelSource   = nullptr;
std::shared_ptr<LevelStorage> g_levelStorage;
Level                        *g_level        = nullptr;
std::wstring                  g_levelName;
int                           g_initState    = kStateUnstarted;
uint64_t                      g_tickCount    = 0;
constexpr uint64_t            kLogEveryN     = 60; // ~1 log/sec at 60 fps

// UTF-8 -> wide string conversion. Documents path comes back as char*.
std::wstring widen(const char *utf8) {
    if (!utf8) return std::wstring();
    std::wstring out;
    out.reserve(64);
    for (const char *p = utf8; *p; ++p) {
        // ASCII-only fast path; iOS sandboxed paths never have multi-byte
        // sequences, so we treat each byte as a wchar_t.
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    }
    return out;
}

// utf16-to-utf8 truncated for log output. The wide identifiers from
// LevelData / LevelSummary go through here for human-readable logs.
std::string narrow(const std::wstring &w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t ch : w) {
        if (ch < 128) out.push_back(static_cast<char>(ch));
        else          out.push_back('?');
    }
    return out;
}

void initImpl() {
    MCLE_LOG("mcle_game_init: starting");

    const char *saveRootC = StorageManager.GetSaveRootPath();
    if (!saveRootC || !*saveRootC) {
        MCLE_LOG("mcle_game_init: GetSaveRootPath returned empty; aborting");
        g_initState = kStateFailed;
        return;
    }
    MCLE_LOG("mcle_game_init: save root = %{public}s", saveRootC);

    std::wstring savesDir = widen(saveRootC) + L"/saves";

    // Step 1: construct the level source. Catches a likely first-crash
    // point (NSFileManager perms, missing directory).
    try {
        g_levelSource = new McRegionLevelStorageSource(File(savesDir));
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: McRegionLevelStorageSource ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!g_levelSource) {
        MCLE_LOG("mcle_game_init: McRegionLevelStorageSource ctor returned null");
        g_initState = kStateFailed;
        return;
    }
    MCLE_LOG("mcle_game_init: level source constructed at %p", (void*)g_levelSource);

    // Step 2: enumerate saves. Upstream's DirectoryLevelStorageSource::
    // getLevelList() body is `#if 0`'d (Xbox saves enumerated differently),
    // so we list Documents/saves/ ourselves via File::listFiles. Each
    // subdirectory there is a candidate levelId.
    std::wstring levelId;
    {
        File savesRoot(savesDir);
        std::vector<File *> *children = savesRoot.listFiles();
        int candidateCount = children ? static_cast<int>(children->size()) : 0;
        MCLE_LOG("mcle_game_init: Documents/saves listed %d entries", candidateCount);
        if (children) {
            for (File *child : *children) {
                if (child && child->isDirectory()) {
                    levelId = child->getName();
                    break;
                }
            }
            for (File *child : *children) delete child;
            delete children;
        }
    }

    if (levelId.empty()) {
        MCLE_LOG("mcle_game_init: no saves available, simulation idle");
        g_initState = kStateReadyIdle;
        return;
    }
    MCLE_LOG("mcle_game_init: selected level id = %{public}s", narrow(levelId).c_str());

    // Step 3: open the storage handle for that save.
    try {
        g_levelStorage = g_levelSource->selectLevel(
            /*saveFile*/   nullptr,
            /*levelId*/    levelId,
            /*createPlayerDir*/ false);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: selectLevel threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!g_levelStorage) {
        MCLE_LOG("mcle_game_init: selectLevel returned null");
        g_initState = kStateFailed;
        return;
    }
    MCLE_LOG("mcle_game_init: selectLevel returned storage at %p", (void*)g_levelStorage.get());

    // Step 4: read level metadata.
    LevelData *levelData = nullptr;
    try {
        levelData = g_levelStorage->prepareLevel();
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: prepareLevel threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!levelData) {
        MCLE_LOG("mcle_game_init: prepareLevel returned null");
        g_initState = kStateFailed;
        return;
    }
    g_levelName = levelData->getLevelName();
    MCLE_LOG("mcle_game_init: prepareLevel succeeded, name = %{public}s",
             narrow(g_levelName).c_str());

    // Step 5: build LevelSettings from the metadata.
    LevelSettings *settings = nullptr;
    try {
        settings = new LevelSettings(levelData);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: LevelSettings ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }

    // Step 6: construct the Level. `Level` itself is abstract
    // (createChunkSource, getEntity are pure virtual), so we use the
    // concrete `ServerLevel` subclass which handles single-player
    // chunk source + entity registry. Pass nullptr for the
    // MinecraftServer* arg - we don't construct a MinecraftServer (the
    // upstream class is in the lib but its init chain pulls UI), and
    // the iOS shell drives the simulation directly. Likely first real
    // crash point since ServerLevel pulls Dimension / ChunkSource.
    try {
        g_level = new ServerLevel(
            /*server*/        nullptr,
            /*levelStorage*/  g_levelStorage,
            /*levelName*/     g_levelName,
            /*dimension*/     0,        // overworld
            /*levelSettings*/ settings);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: ServerLevel ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!g_level) {
        MCLE_LOG("mcle_game_init: Level ctor returned null");
        g_initState = kStateFailed;
        return;
    }

    g_initState = kStateTicking;
    MCLE_LOG("mcle_game_init: Level constructed at %p, ticking enabled", (void*)g_level);
}

} // anonymous namespace

extern "C" void mcle_game_init(void) {
    if (g_initState != kStateUnstarted) return;
    try {
        initImpl();
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: top-level exception: %{public}s", e.what());
        g_initState = kStateFailed;
    } catch (...) {
        MCLE_LOG("mcle_game_init: top-level non-std exception");
        g_initState = kStateFailed;
    }
}

extern "C" void mcle_game_tick(void) {
    // Lazy-init on the first tick so the bootstrap runs after the SWF
    // menu has had a chance to draw at least one frame.
    if (g_initState == kStateUnstarted) mcle_game_init();

    g_tickCount++;

    if (g_initState != kStateTicking || !g_level) {
        if ((g_tickCount % kLogEveryN) == 0) {
            MCLE_LOG("tick %llu (probe lib running, simulation idle, state=%d)",
                     static_cast<unsigned long long>(g_tickCount), g_initState);
        }
        return;
    }

    try {
        g_level->tick();
        g_level->tickEntities();
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_tick: Level::tick threw: %{public}s; pausing simulation", e.what());
        g_initState = kStateFailed;
        return;
    } catch (...) {
        MCLE_LOG("mcle_game_tick: Level::tick non-std exception; pausing simulation");
        g_initState = kStateFailed;
        return;
    }

    if ((g_tickCount % kLogEveryN) == 0) {
        size_t entityCount = 0;
        try { entityCount = g_level->entities.size(); } catch (...) {}
        MCLE_LOG("tick %llu - level=%p entities=%zu",
                 static_cast<unsigned long long>(g_tickCount),
                 (void*)g_level,
                 entityCount);
    }
}

extern "C" void mcle_game_shutdown(void) {
    MCLE_LOG("mcle_game_shutdown: tearing down");
    if (g_level) {
        try { delete g_level; } catch (...) {}
        g_level = nullptr;
    }
    g_levelStorage.reset();
    if (g_levelSource) {
        try { delete g_levelSource; } catch (...) {}
        g_levelSource = nullptr;
    }
    g_initState = kStateUnstarted;
    MCLE_LOG("mcle_game_shutdown: done");
}

// Optional debug helper invoked from a controller chord in
// MinecraftViewController.mm. Forces a re-init even if the previous
// attempt failed (useful while iterating on save formats).
extern "C" void mcle_game_debug_start(void) {
    MCLE_LOG("mcle_game_debug_start: forcing re-init");
    mcle_game_shutdown();
    mcle_game_init();
}
