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

#include "../../../upstream/Minecraft.World/ConsoleSaveFileOriginal.h"
#include "../../../upstream/Minecraft.World/File.h"
#include "../../../upstream/Minecraft.World/FileInputStream.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/LevelData.h"
#include "../../../upstream/Minecraft.World/LevelSettings.h"
#include "../../../upstream/Minecraft.World/LevelStorage.h"
#include "../../../upstream/Minecraft.World/LevelSummary.h"
#include "../../../upstream/Minecraft.World/McRegionLevelStorage.h"
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
ConsoleSaveFileOriginal     *g_saveFile      = nullptr;
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

    // Step 2.5: locate the .ms bundle inside the save folder. Win64 LCE
    // names it after the world ("New World.ms" by default), not a fixed
    // filename - so list the dir and pick the first .ms entry.
    std::wstring saveFolder = savesDir + L"/" + levelId;
    std::wstring saveDataPath;
    {
        File saveDir(saveFolder);
        std::vector<File *> *contents = saveDir.listFiles();
        if (contents) {
            for (File *entry : *contents) {
                if (!entry) continue;
                std::wstring name = entry->getName();
                if (name.size() >= 3 &&
                    name.compare(name.size() - 3, 3, L".ms") == 0) {
                    saveDataPath = saveFolder + L"/" + name;
                    break;
                }
            }
            for (File *entry : *contents) delete entry;
            delete contents;
        }
    }
    if (saveDataPath.empty()) {
        MCLE_LOG("mcle_game_init: no .ms bundle in save folder, idling");
        g_initState = kStateReadyIdle;
        return;
    }
    File saveDataFile(saveDataPath);
    int64_t saveSize = saveDataFile.length();
    if (saveSize <= 0) {
        MCLE_LOG("mcle_game_init: .ms missing or empty (%lld bytes)",
                 (long long)saveSize);
        g_initState = kStateFailed;
        return;
    }
    MCLE_LOG("mcle_game_init: .ms bundle = %{public}s, size = %lld bytes",
             narrow(saveDataPath).c_str(), (long long)saveSize);

    byteArray saveBytes((unsigned int)saveSize);
    try {
        FileInputStream fis(saveDataFile);
        fis.read(saveBytes);
        fis.close();
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: saveData.ms read threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }

    // Construct the upstream save-file wrapper around the in-memory bytes.
    // Matches MinecraftServer.cpp:913 exactly.
    try {
        g_saveFile = new ConsoleSaveFileOriginal(
            /*fileName*/      saveDataPath,
            /*pvSaveData*/    saveBytes.data,
            /*fileSize*/      saveBytes.length,
            /*forceCleanSave*/false,
            /*plat*/          SAVE_FILE_PLATFORM_LOCAL);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: ConsoleSaveFileOriginal ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!g_saveFile) {
        MCLE_LOG("mcle_game_init: ConsoleSaveFileOriginal ctor returned null");
        g_initState = kStateFailed;
        return;
    }
    g_saveFile->ConvertToLocalPlatform();
    MCLE_LOG("mcle_game_init: saveData.ms parsed at %p", (void*)g_saveFile);

    // Step 3: build the storage directly. Parity with MinecraftServer.cpp:919
    // (`storage = make_shared<McRegionLevelStorage>(pSave, File(L"."), name, true)`).
    // The McRegionLevelStorageSource->selectLevel path is for Xbox-style save
    // folders; for in-memory bundle storage we wrap pSave straight away.
    try {
        g_levelStorage = std::make_shared<McRegionLevelStorage>(
            g_saveFile, File(L"."), levelId, /*createPlayerDir*/ true);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: McRegionLevelStorage ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!g_levelStorage) {
        MCLE_LOG("mcle_game_init: McRegionLevelStorage ctor returned null");
        g_initState = kStateFailed;
        return;
    }
    MCLE_LOG("mcle_game_init: McRegionLevelStorage at %p", (void*)g_levelStorage.get());

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
    if (g_saveFile) {
        try { delete g_saveFile; } catch (...) {}
        g_saveFile = nullptr;
    }
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
