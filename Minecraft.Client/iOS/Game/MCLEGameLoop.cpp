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
#include <pthread.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <exception>

#include "../../../upstream/Minecraft.World/Minecraft.World.h"
#include "../../../upstream/Minecraft.World/Compression.h"
#include "../../../upstream/Minecraft.World/ConsoleSaveFileOriginal.h"
#include "../../../upstream/Minecraft.World/File.h"
#include "../../../upstream/Minecraft.World/FileInputStream.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/LevelData.h"
#include "../../../upstream/Minecraft.World/LevelSettings.h"
#include "../../../upstream/Minecraft.World/LevelType.h"
#include "../../../upstream/Minecraft.World/MaterialColor.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/Item.h"
#include "../../../upstream/Minecraft.World/LevelStorage.h"
#include "../../../upstream/Minecraft.World/LevelSummary.h"
#include "../../../upstream/Minecraft.World/McRegionLevelStorage.h"
#include "../../../upstream/Minecraft.World/McRegionLevelStorageSource.h"
#include "../../../upstream/Minecraft.World/Pos.h"
#include "../../../upstream/Minecraft.Client/ServerChunkCache.h"
#include "../../../upstream/Minecraft.Client/DerivedServerLevel.h"
#include "../../../upstream/Minecraft.Client/MinecraftServer.h"
#include "../../../upstream/Minecraft.Client/PlayerList.h"
#include "../../../upstream/Minecraft.Client/Settings.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
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
MinecraftServer             *g_server        = nullptr;
std::shared_ptr<LevelStorage> g_levelStorage;
// Three dimensions per upstream: 0=overworld, 1=nether, 2=end. levels[0]
// is a ServerLevel; levels[1] and levels[2] are DerivedServerLevel that
// share state with levels[0]. Mirrors MinecraftServer.cpp:956-1007.
ServerLevel                  *g_levels[3]    = { nullptr, nullptr, nullptr };
std::shared_ptr<ServerPlayer> g_player;
ServerPlayerGameMode         *g_playerGameMode = nullptr;
std::wstring                  g_levelName;
// std::atomic so the main-thread tick reliably reads progress from the
// background init thread. On arm64 plain int loads/stores are atomic
// but the compiler can still reorder them with surrounding writes
// (g_levels[0] = ...) and starve the tick of a happens-before edge.
std::atomic<int>              g_initState{ kStateUnstarted };
uint64_t                      g_tickCount    = 0;
// Init runs on a background pthread so the main-thread tick stays free
// to render the placeholder SWF + handle input while save loading is
// in progress. Set once on first tick, never reset.
std::atomic<bool>             g_initStarted{ false };
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

    // Per-platform bootstrap (Windows64_Minecraft.cpp, Xbox_Minecraft.cpp,
    // ServerMain.cpp, etc.) all call this on first thread entry. Without
    // it, Compression::getCompression() returns garbage from TLS slot 0
    // and Decompress is a silent no-op - which leaves saveData.ms's
    // decompressed body empty and prepareLevel falls into the no-level.dat
    // branch.
    Compression::CreateNewThreadStorage();

    // MinecraftWorld_RunStaticCtors() is the parity-correct upstream init
    // pass but Tile::staticCtor SIGSEGVs on iOS (separate investigation).
    // For now, call the lightweight static ctors that downstream code
    // actually needs: GameType (LevelData ctor calls byId), LevelType
    // (LevelData reads lvl_normal). These have no Tile/Material deps.
    try {
        GameType::staticCtor();
        MCLE_LOG("mcle_game_init: GameType::staticCtor done");
    } catch (...) {
        MCLE_LOG("mcle_game_init: GameType::staticCtor threw");
    }
    try {
        LevelType::staticCtor();
        MCLE_LOG("mcle_game_init: LevelType::staticCtor done");
    } catch (...) {
        MCLE_LOG("mcle_game_init: LevelType::staticCtor threw");
    }
    // MinecraftServer ctor calls DispenserBootstrap::bootStrap() which
    // registers behaviors keyed off Item::* statics; null without these.
    try { MaterialColor::staticCtor(); MCLE_LOG("mcle_game_init: MaterialColor::staticCtor done"); }
    catch (...) { MCLE_LOG("mcle_game_init: MaterialColor::staticCtor threw"); }
    try { Material::staticCtor();      MCLE_LOG("mcle_game_init: Material::staticCtor done"); }
    catch (...) { MCLE_LOG("mcle_game_init: Material::staticCtor threw"); }
    // Item::staticCtor crashes on iOS (separate investigation, same family
    // as the Tile::staticCtor crash). DispenserBootstrap registers
    // behaviors keyed off Item::* statics, but registering a nullptr key
    // is just a map insert with no deref - should be safe.
    // try { Item::staticCtor();       MCLE_LOG("mcle_game_init: Item::staticCtor done"); }
    // catch (...) { MCLE_LOG("mcle_game_init: Item::staticCtor threw"); }
    // try { Item::staticInit();       MCLE_LOG("mcle_game_init: Item::staticInit done"); }
    // catch (...) { MCLE_LOG("mcle_game_init: Item::staticInit threw"); }

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
            int idx = 0;
            for (File *child : *children) {
                if (!child) { ++idx; continue; }
                std::wstring nm = child->getName();
                MCLE_LOG("mcle_game_init: saves[%d] = %{public}s (%zu wchars, isDir=%d)",
                         idx++, narrow(nm).c_str(), nm.size(), child->isDirectory() ? 1 : 0);
                if (levelId.empty() && child->isDirectory()) {
                    levelId = nm;
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
    if (saveBytes.length >= 16 && saveBytes.data) {
        unsigned char *b = (unsigned char *)saveBytes.data;
        unsigned int u0 = (unsigned int)b[0] | ((unsigned int)b[1]<<8) | ((unsigned int)b[2]<<16) | ((unsigned int)b[3]<<24);
        unsigned int u1 = (unsigned int)b[4] | ((unsigned int)b[5]<<8) | ((unsigned int)b[6]<<16) | ((unsigned int)b[7]<<24);
        MCLE_LOG("mcle_game_init: .ms head: u0(LE)=%u u1(LE)=%u "
                 "raw=%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
                 u0, u1,
                 b[0],b[1],b[2],b[3], b[4],b[5],b[6],b[7],
                 b[8],b[9],b[10],b[11], b[12],b[13],b[14],b[15]);
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

    // Diagnostic: enumerate the parsed bundle's file table. If level.dat
    // isn't here, prepareLevel() will return null and the bootstrap idles
    // with "simulation idle, state=-1".
    try {
        std::vector<FileEntry *> *files = g_saveFile->getFilesWithPrefix(std::wstring(L""));
        if (!files) {
            MCLE_LOG("mcle_game_init: bundle file table is null");
        } else {
            MCLE_LOG("mcle_game_init: bundle has %zu file entries", files->size());
            int n = 0;
            for (FileEntry *fe : *files) {
                if (!fe) continue;
                std::wstring fn(fe->data.filename);
                MCLE_LOG("mcle_game_init: file[%d] = %{public}s (%u bytes)",
                         n++, narrow(fn).c_str(), (unsigned)fe->getFileSize());
                if (n >= 64) { MCLE_LOG("mcle_game_init: ...truncated"); break; }
            }
        }
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: file-table dump threw: %{public}s", e.what());
    } catch (...) {
        MCLE_LOG("mcle_game_init: file-table dump threw non-std exception");
    }

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
    MCLE_LOG("mcle_game_init: calling prepareLevel...");
    LevelData *levelData = nullptr;
    try {
        levelData = g_levelStorage->prepareLevel();
        MCLE_LOG("mcle_game_init: prepareLevel returned %p", (void*)levelData);
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
    MCLE_LOG("mcle_game_init: building LevelSettings...");
    LevelSettings *settings = nullptr;
    try {
        settings = new LevelSettings(levelData);
        MCLE_LOG("mcle_game_init: LevelSettings at %p", (void*)settings);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: LevelSettings ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }

    // Step 5.5: construct a real MinecraftServer instance. Parity with
    // MinecraftServer::main:2251 (`server = new MinecraftServer()`).
    // The ctor sets up command dispatcher + dispenser bootstrap +
    // pause-event - all things ServerLevel ctor expects to call back
    // into.
    MCLE_LOG("mcle_game_init: constructing MinecraftServer...");
    try {
        g_server = new MinecraftServer();
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: MinecraftServer ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!g_server) {
        MCLE_LOG("mcle_game_init: MinecraftServer ctor returned null");
        g_initState = kStateFailed;
        return;
    }
    MCLE_LOG("mcle_game_init: MinecraftServer at %p", (void*)g_server);

    // Step 5.55: server-side Settings. Upstream initServer() does
    // `settings = new Settings(new File(L"server.properties"))` before
    // PlayerList is built. PlayerList ctor calls
    // server->settings->getInt(L"max-players", 8) - null defaults aren't
    // safe. Settings ctor reads server.properties (file may be missing
    // - that's fine; getInt then returns the supplied default).
    try {
        g_server->settings = new Settings(new File(std::wstring(L"server.properties")));
        MCLE_LOG("mcle_game_init: server->settings at %p", (void*)g_server->settings);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: Settings ctor threw: %{public}s", e.what());
    }

    // Step 5.6: attach a PlayerList. Parity with MinecraftServer.cpp:690
    // (`setPlayers(new PlayerList(this))`). Without this, ServerLevel ctor
    // crashes inside `server->getPlayers()->getViewDistance()`.
    MCLE_LOG("mcle_game_init: constructing PlayerList...");
    try {
        PlayerList *pl = new PlayerList(g_server);
        MCLE_LOG("mcle_game_init: PlayerList allocated at %p, attaching...", (void*)pl);
        g_server->setPlayers(pl);
        MCLE_LOG("mcle_game_init: PlayerList attached");
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: PlayerList ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    if (!g_server->getPlayers()) {
        MCLE_LOG("mcle_game_init: PlayerList not attached");
        g_initState = kStateFailed;
        return;
    }
    MCLE_LOG("mcle_game_init: PlayerList at %p", (void*)g_server->getPlayers());

    // Step 6: construct three ServerLevels (overworld + nether + end).
    // Parity with MinecraftServer.cpp:956-1007 - levels[0] is a real
    // ServerLevel, levels[1] (nether, dim=-1) and levels[2] (end, dim=1)
    // are DerivedServerLevels that share state with levels[0].
    static const int kDimForIndex[3] = { 0, -1, 1 };
    bool levelOk = true;
    for (int i = 0; i < 3 && levelOk; ++i) {
        try {
            if (i == 0) {
                g_levels[i] = new ServerLevel(
                    /*server*/        g_server,
                    /*levelStorage*/  g_levelStorage,
                    /*levelName*/     g_levelName,
                    /*dimension*/     kDimForIndex[i],
                    /*levelSettings*/ settings);
            } else {
                g_levels[i] = new DerivedServerLevel(
                    /*server*/        g_server,
                    /*levelStorage*/  g_levelStorage,
                    /*levelName*/     g_levelName,
                    /*dimension*/     kDimForIndex[i],
                    /*levelSettings*/ settings,
                    /*wrapped*/       g_levels[0]);
            }
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: ServerLevel[%d] ctor threw: %{public}s", i, e.what());
            levelOk = false;
            break;
        }
        if (!g_levels[i]) {
            MCLE_LOG("mcle_game_init: ServerLevel[%d] ctor returned null", i);
            levelOk = false;
            break;
        }
        MCLE_LOG("mcle_game_init: levels[%d] (dim=%d) at %p",
                 i, kDimForIndex[i], (void*)g_levels[i]);
    }

    if (!levelOk) {
        for (int i = 0; i < 3; ++i) {
            if (g_levels[i]) { try { delete g_levels[i]; } catch (...) {} }
            g_levels[i] = nullptr;
        }
        g_initState = kStateFailed;
        return;
    }

    // Step 7: preload chunks around spawn for the overworld. Parity with
    // MinecraftServer::run lines 1115-1146. Iterates a 13x13 chunk grid
    // (208-block radius) calling ServerChunkCache::create which loads
    // existing chunks from saveData.ms's region data or generates new
    // ones for missing tiles. Wrapped per-chunk so a single bad chunk
    // doesn't kill the whole bootstrap.
    {
        static constexpr int kPreloadRadiusChunks = 6;  // ~96 blocks; r=196 in upstream is too aggressive for first boot
        Pos *spawnPos = nullptr;
        try { spawnPos = g_levels[0]->getSharedSpawnPos(); } catch (...) {}
        int spawnCx = spawnPos ? (spawnPos->x >> 4) : 0;
        int spawnCz = spawnPos ? (spawnPos->z >> 4) : 0;
        delete spawnPos;
        MCLE_LOG("mcle_game_init: preloading chunks around (cx=%d, cz=%d), r=%d",
                 spawnCx, spawnCz, kPreloadRadiusChunks);

        int loaded = 0;
        int failed = 0;
        for (int dx = -kPreloadRadiusChunks; dx <= kPreloadRadiusChunks; ++dx) {
            for (int dz = -kPreloadRadiusChunks; dz <= kPreloadRadiusChunks; ++dz) {
                try {
                    if (g_levels[0]->cache && g_levels[0]->cache->create(spawnCx + dx, spawnCz + dz, true)) {
                        loaded++;
                    }
                } catch (const std::exception &e) {
                    failed++;
                    if (failed <= 4) {
                        MCLE_LOG("mcle_game_init: chunk (%d,%d) ctor threw: %{public}s",
                                 spawnCx + dx, spawnCz + dz, e.what());
                    }
                } catch (...) {
                    failed++;
                }
            }
        }
        MCLE_LOG("mcle_game_init: chunk preload done, %d loaded, %d failed", loaded, failed);
    }

    // Step 8: spawn a ServerPlayer at the overworld's shared spawn pos
    // and register it. Parity-light: upstream's full network flow runs
    // through PendingConnection -> AddPlayerPacket -> PlayerList::add;
    // for a single-player iOS shell we shortcut to the same end state
    // that PlayerList::add would produce (player constructed against
    // levels[0], registered with the server, added to the level).
    try {
        g_playerGameMode = new ServerPlayerGameMode(g_levels[0]);
        g_player = std::make_shared<ServerPlayer>(
            /*server*/    g_server,
            /*level*/     static_cast<Level *>(g_levels[0]),
            /*name*/      std::wstring(L"iOSPlayer"),
            /*gameMode*/  g_playerGameMode);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: ServerPlayer ctor threw: %{public}s", e.what());
        // Don't fail the whole bootstrap - levels are still ticking.
    }
    if (g_player) {
        MCLE_LOG("mcle_game_init: ServerPlayer at %p", (void*)g_player.get());
        try {
            g_levels[0]->addEntity(g_player);
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: addEntity threw: %{public}s", e.what());
        }
    }

    g_initState = kStateTicking;
    MCLE_LOG("mcle_game_init: 3 levels constructed, ticking enabled");
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
    // menu has had a chance to draw at least one frame. Init runs on a
    // pthread so a long save-load doesn't block the main-thread render
    // loop and freeze the placeholder.
    if (!g_initStarted && g_initState == kStateUnstarted) {
        g_initStarted = true;
        pthread_t t;
        pthread_create(&t, nullptr, [](void *) -> void * {
            mcle_game_init();
            return nullptr;
        }, nullptr);
        pthread_detach(t);
    }

    g_tickCount++;

    if (g_initState != kStateTicking || !g_levels[0]) {
        if ((g_tickCount % kLogEveryN) == 0) {
            MCLE_LOG("tick %llu (probe lib running, simulation idle, state=%d)",
                     static_cast<unsigned long long>(g_tickCount), g_initState.load());
        }
        return;
    }

    // Tick all three dimensions in order. Parity with how the upstream
    // server's runUpdate iterates levels[] each frame.
    try {
        for (int i = 0; i < 3; ++i) {
            if (!g_levels[i]) continue;
            g_levels[i]->tick();
            g_levels[i]->tickEntities();
        }
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_tick: tick threw: %{public}s; pausing simulation", e.what());
        g_initState = kStateFailed;
        return;
    } catch (...) {
        MCLE_LOG("mcle_game_tick: tick non-std exception; pausing simulation");
        g_initState = kStateFailed;
        return;
    }

    if ((g_tickCount % kLogEveryN) == 0) {
        size_t entityCount = 0;
        try { if (g_levels[0]) entityCount = g_levels[0]->entities.size(); } catch (...) {}
        MCLE_LOG("tick %llu - overworld=%p entities=%zu",
                 static_cast<unsigned long long>(g_tickCount),
                 (void*)g_levels[0],
                 entityCount);
    }
}

extern "C" void mcle_game_shutdown(void) {
    MCLE_LOG("mcle_game_shutdown: tearing down");
    // Drop the player first so its dtor runs before the level it lives in.
    g_player.reset();
    if (g_playerGameMode) {
        try { delete g_playerGameMode; } catch (...) {}
        g_playerGameMode = nullptr;
    }
    // Tear levels down in reverse order: derived levels reference the
    // overworld, so they must go before levels[0]. Server is destroyed
    // after the levels because they hold raw pointers to it.
    for (int i = 2; i >= 0; --i) {
        if (g_levels[i]) {
            try { delete g_levels[i]; } catch (...) {}
            g_levels[i] = nullptr;
        }
    }
    if (g_server) {
        try { delete g_server; } catch (...) {}
        g_server = nullptr;
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
