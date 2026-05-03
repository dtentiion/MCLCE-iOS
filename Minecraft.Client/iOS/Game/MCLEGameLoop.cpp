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
#include <signal.h>
#include <unistd.h>
#include <cmath>
#include <new>

// G2a: declared in Render lib (MetalContext.mm). Forward decl here so
// the tick log can read the cumulative DrawVertices count.
extern "C" unsigned long long mcle_metal_draw_count(void);

// G3a: number of recorded display lists. Bumps once per glNewList during
// LevelRenderer ctor (skyList, starList, darkList, cloudList, chunkLists)
// and stays flat after init - so this is a static signal that recording
// is wired correctly. Counter version of draws climbs per frame once
// glCallList replays start firing.
extern "C" unsigned long long mcle_glbridge_list_count(void);

// G1B-probe: defined later in this same TU; forward-declared here so
// the tick path can call it before the definition appears.
extern "C" void mcle_world_g1b_probe_tick(void);

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
#include "../../../upstream/Minecraft.World/Recipes.h"
#include "../../../upstream/Minecraft.World/Tile.h"
#include "../../../upstream/Minecraft.World/Item.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"
#include "../../../upstream/Minecraft.World/Entity.h"
#include "../../../upstream/Minecraft.World/Biome.h"
#include "../../../upstream/Minecraft.World/GenericStats.h"
#include "../../../upstream/Minecraft.World/CommonStats.h"
#include "../../../upstream/Minecraft.World/Stats.h"
#include "../../../upstream/Minecraft.World/Packet.h"
#include "../../../upstream/Minecraft.World/HatchetItem.h"
#include "../../../upstream/Minecraft.World/PickaxeItem.h"
#include "../../../upstream/Minecraft.World/ShovelItem.h"
#include "../../../upstream/Minecraft.World/BlockReplacements.h"
#include "../../../upstream/Minecraft.World/FurnaceRecipes.h"
#include "../../../upstream/Minecraft.World/TileEntity.h"
#include "../../../upstream/Minecraft.World/EntityIO.h"
#include "../../../upstream/Minecraft.World/MobCategory.h"
#include "../../../upstream/Minecraft.World/LevelChunk.h"
#include "../../../upstream/Minecraft.World/StructureFeatureIO.h"
#include "../../../upstream/Minecraft.World/MineShaftPieces.h"
#include "../../../upstream/Minecraft.World/StrongholdFeature.h"
#include "../../../upstream/Minecraft.World/VillagePieces.h"
#include "../../../upstream/Minecraft.World/VillageFeature.h"
#include "../../../upstream/Minecraft.World/RandomScatteredLargeFeature.h"
#include "../../../upstream/Minecraft.World/EnderMan.h"
#include "../../../upstream/Minecraft.World/PotionBrewing.h"
#include "../../../upstream/Minecraft.World/Enchantment.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/SparseLightStorage.h"
#include "../../../upstream/Minecraft.World/LevelStorage.h"
#include "../../../upstream/Minecraft.World/LevelSummary.h"
#include "../../../upstream/Minecraft.World/McRegionLevelStorage.h"
#include "../../../upstream/Minecraft.World/McRegionLevelStorageSource.h"
#include "../../../upstream/Minecraft.World/Pos.h"
#include "../../../upstream/Minecraft.World/Vec3.h"
#include "../../../upstream/Minecraft.Client/ServerChunkCache.h"
#include "../../../upstream/Minecraft.Client/DerivedServerLevel.h"
#include "../../../upstream/Minecraft.Client/MinecraftServer.h"
#include "../../../upstream/Minecraft.Client/PlayerList.h"
#include "../../../upstream/Minecraft.Client/Settings.h"
#include "../../../upstream/Minecraft.Client/ServerPlayer.h"
#include "../../../upstream/Minecraft.Client/ServerPlayerGameMode.h"
#include "../../../upstream/Minecraft.Client/ServerLevel.h"
#include "../../../upstream/Minecraft.Client/LevelRenderer.h"
#include "../../../upstream/Minecraft.Client/Tesselator.h"
#include "../../../upstream/Minecraft.Client/Minecraft.h"
#include "../../../upstream/Minecraft.Client/Options.h"
#include "../../../upstream/Minecraft.Client/GameRenderer.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLevel.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLocalPlayer.h"
#include "../../../upstream/Minecraft.Client/Textures.h"

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
LevelRenderer                *g_levelRenderer  = nullptr;
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

    // Mark this thread as the server thread so Entity ctor allocates
    // small (network-trackable, < 16384) IDs for entities. Without this
    // every Entity gets an id >= 16384 and EntityTracker::addEntity
    // hits __debugbreak().
    Entity::useSmallIds();

    // Tile::setShape needs TLS-owned ThreadStorage on every thread that
    // touches the world. Bootstrap thread first, render thread on its
    // first tick (see mcle_game_tick).
    Tile::CreateNewThreadStorage();
    MCLE_LOG("mcle_game_init: Tile::CreateNewThreadStorage done");

    // Tesselator is also TLS-singleton (see Tesselator.cpp:27, :34).
    // LevelRenderer ctor + every renderer path uses Tesselator::getInstance()
    // which derefs the TLS slot. Without this it's null and all upstream
    // renderer code silently null-derefs (or no-ops if behind null guards).
    // 16 KB matches the size upstream platform mains pass.
    Tesselator::CreateNewThreadStorage(16 * 1024);
    MCLE_LOG("mcle_game_init: Tesselator::CreateNewThreadStorage done");

    // Vec3 is also TLS-singleton (see Vec3.cpp:19). Vec3::newTemp() is
    // used liberally throughout the renderer (Level::getSkyColor,
    // getCloudColor, getSunriseColor, ...) and reads tls->pool[idx]. If
    // unset on this thread, deref produces a corrupt-pointer-shaped
    // SIGSEGV (which was the G3e crash inside Level::getSkyColor).
    Vec3::CreateNewThreadStorage();
    MCLE_LOG("mcle_game_init: Vec3::CreateNewThreadStorage done");

    // Parity-correct upstream static init order, mirroring
    // Minecraft.World.cpp's MinecraftWorld_RunStaticCtors at lines 30-79.
    // Each call wrapped in try/catch so a single C++ throw doesn't kill
    // the whole sequence (SIGSEGV will still terminate; that's by design
    // - we want to know which static init has a bad dep on iOS).
    #define MCLE_STATIC(name, body) \
        try { body; MCLE_LOG("mcle_game_init: " name " done"); } \
        catch (...) { MCLE_LOG("mcle_game_init: " name " threw"); }

    MCLE_STATIC("Packet::staticCtor",          Packet::staticCtor());
    MCLE_STATIC("GameType::staticCtor",        GameType::staticCtor());
    MCLE_STATIC("MaterialColor::staticCtor",   MaterialColor::staticCtor());
    MCLE_STATIC("Material::staticCtor",        Material::staticCtor());
    MCLE_STATIC("Tile::staticCtor",            Tile::staticCtor());
    MCLE_STATIC("HatchetItem::staticCtor",     HatchetItem::staticCtor());
    MCLE_STATIC("PickaxeItem::staticCtor",     PickaxeItem::staticCtor());
    MCLE_STATIC("ShovelItem::staticCtor",      ShovelItem::staticCtor());
    MCLE_STATIC("BlockReplacements::staticCtor", BlockReplacements::staticCtor());
    MCLE_STATIC("Biome::staticCtor",           Biome::staticCtor());
    MCLE_STATIC("MobEffect::staticCtor",       MobEffect::staticCtor());
    MCLE_STATIC("Item::staticCtor",            Item::staticCtor());
    MCLE_STATIC("FurnaceRecipes::staticCtor",  FurnaceRecipes::staticCtor());
    MCLE_STATIC("Recipes::staticCtor",         Recipes::staticCtor());
    MCLE_STATIC("GenericStats::setInstance",   GenericStats::setInstance(new CommonStats()));
    MCLE_STATIC("Stats::staticCtor",           Stats::staticCtor());
    MCLE_STATIC("TileEntity::staticCtor",      TileEntity::staticCtor());
    MCLE_STATIC("EntityIO::staticCtor",        EntityIO::staticCtor());
    MCLE_STATIC("MobCategory::staticCtor",     MobCategory::staticCtor());
    MCLE_STATIC("Item::staticInit",            Item::staticInit());
    MCLE_STATIC("LevelChunk::staticCtor",      LevelChunk::staticCtor());
    MCLE_STATIC("LevelType::staticCtor",       LevelType::staticCtor());
    MCLE_STATIC("StructureFeatureIO::staticCtor",       StructureFeatureIO::staticCtor());
    MCLE_STATIC("MineShaftPieces::staticCtor",          MineShaftPieces::staticCtor());
    MCLE_STATIC("StrongholdFeature::staticCtor",        StrongholdFeature::staticCtor());
    MCLE_STATIC("VillagePieces::Smithy::staticCtor",    VillagePieces::Smithy::staticCtor());
    MCLE_STATIC("VillageFeature::staticCtor",           VillageFeature::staticCtor());
    MCLE_STATIC("RandomScatteredLargeFeature::staticCtor", RandomScatteredLargeFeature::staticCtor());
    MCLE_STATIC("EnderMan::staticCtor",        EnderMan::staticCtor());
    MCLE_STATIC("PotionBrewing::staticCtor",   PotionBrewing::staticCtor());
    MCLE_STATIC("Enchantment::staticCtor",     Enchantment::staticCtor());
    MCLE_STATIC("SharedConstants::staticCtor", SharedConstants::staticCtor());
    MCLE_STATIC("ServerLevel::staticCtor",     ServerLevel::staticCtor());
    MCLE_STATIC("SparseLightStorage::staticCtor", SparseLightStorage::staticCtor());

    #undef MCLE_STATIC

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

    // Parity with MinecraftServer.cpp:891 - levels array must be sized to 3
    // before any ServerLevel ctor runs setLevel(dim, this) on the server.
    // Default-constructed ServerLevelArray has data=nullptr/length=0 so
    // levels[0] = level would null-deref.
    g_server->levels = ServerLevelArray(3);
    MCLE_LOG("mcle_game_init: server->levels sized to 3");

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
        // G5: this save's chunk pattern is irregular - even r=3 hit a
        // null deref at (16,13). Bulk preloading any unvisited chunk
        // crashes inside the level source's procedural generator. Drop
        // to r=0 (just the spawn chunk) and rely on updateDirtyChunks's
        // per-chunk lazy load for the render area. Per-chunk loads go
        // through level->getChunkAt which has its own null guard.
        static constexpr int kPreloadRadiusChunks = 0;
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
                int cx = spawnCx + dx;
                int cz = spawnCz + dz;
                MCLE_LOG("mcle_game_init: chunk preload (%d,%d) start", cx, cz);
                try {
                    if (g_levels[0]->cache && g_levels[0]->cache->create(cx, cz, true)) {
                        loaded++;
                        MCLE_LOG("mcle_game_init: chunk preload (%d,%d) ok", cx, cz);
                    } else {
                        MCLE_LOG("mcle_game_init: chunk preload (%d,%d) returned null", cx, cz);
                    }
                } catch (const std::exception &e) {
                    failed++;
                    MCLE_LOG("mcle_game_init: chunk (%d,%d) ctor threw: %{public}s",
                             cx, cz, e.what());
                } catch (...) {
                    failed++;
                    MCLE_LOG("mcle_game_init: chunk (%d,%d) ctor threw unknown", cx, cz);
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
    MCLE_LOG("mcle_game_init: building ServerPlayerGameMode...");
    try {
        g_playerGameMode = new ServerPlayerGameMode(g_levels[0]);
        MCLE_LOG("mcle_game_init: ServerPlayerGameMode at %p", (void*)g_playerGameMode);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: ServerPlayerGameMode ctor threw: %{public}s", e.what());
    } catch (...) {
        MCLE_LOG("mcle_game_init: ServerPlayerGameMode ctor threw unknown");
    }

    if (g_playerGameMode) {
        MCLE_LOG("mcle_game_init: building ServerPlayer...");
        try {
            g_player = std::make_shared<ServerPlayer>(
                g_server,
                static_cast<Level *>(g_levels[0]),
                std::wstring(L"iOSPlayer"),
                g_playerGameMode);
            MCLE_LOG("mcle_game_init: ServerPlayer at %p", (void*)g_player.get());
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: ServerPlayer ctor threw: %{public}s", e.what());
        } catch (...) {
            MCLE_LOG("mcle_game_init: ServerPlayer ctor threw unknown");
        }
    }

    if (g_player) {
        MCLE_LOG("mcle_game_init: addEntity to overworld");
        try {
            g_levels[0]->addEntity(g_player);
            MCLE_LOG("mcle_game_init: addEntity returned");
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: addEntity threw: %{public}s", e.what());
        } catch (...) {
            MCLE_LOG("mcle_game_init: addEntity threw unknown");
        }
    }

    // Flip to ticking state ONLY after the player is fully registered.
    // Otherwise the render thread tick races against the bootstrap and
    // first tick runs entities=0, second tick runs entities=1 mid-init.
    g_initState = kStateTicking;
    MCLE_LOG("mcle_game_init: 3 levels constructed, ticking enabled");

    // G2c-shim: allocate a zero-filled Minecraft-sized buffer and cast to
    // Minecraft*. Class has no virtual methods (verified) so no vtable
    // pointer needed. Fields left zero; LevelRenderer::render derefs them
    // one by one - the auto-probe pins each so we can fill them in
    // incrementally. Eventual goal: full upstream Minecraft instance.
    static char s_minecraftShimBuf[sizeof(Minecraft)] = {0};
    Minecraft *g_minecraftShim = reinterpret_cast<Minecraft *>(s_minecraftShimBuf);
    MCLE_LOG("mcle_game_init: Minecraft shim at %p (sizeof=%zu)",
             (void*)g_minecraftShim, sizeof(Minecraft));

    // Wire the player into the shim so renderChunks's cameraTargetPlayer
    // null check doesn't bail. ServerPlayer extends LivingEntity so the
    // shared_ptr conversion is implicit. Done with placement-new on the
    // already-zeroed buffer so the shared_ptr's default ctor runs.
    new (&g_minecraftShim->cameraTargetPlayer) std::shared_ptr<LivingEntity>(g_player);

    // G5: localplayers[0] is what LevelRenderer::updateDirtyChunks reads
    // to find the nearest dirty chunk for rebuild (uses player x/y/z).
    // ServerPlayer != MultiplayerLocalPlayer but they share Entity base
    // and only the position fields are touched in this loop, so a
    // reinterpret-cast placement-new is safe for the read-only path.
    {
        std::shared_ptr<MultiplayerLocalPlayer> *slot =
            reinterpret_cast<std::shared_ptr<MultiplayerLocalPlayer> *>(
                &g_minecraftShim->localplayers[0]);
        new (slot) std::shared_ptr<MultiplayerLocalPlayer>(
            std::reinterpret_pointer_cast<MultiplayerLocalPlayer>(g_player));
        MCLE_LOG("mcle_game_init: G5 wired localplayers[0] = %p (alias of g_player)",
                 (void*)slot->get());
    }

    // G2d: heap-allocate Options and GameRenderer for the shim. Options
    // ctor calls init() which sets the defaults LevelRenderer reads
    // (viewDistance, fancyGraphics, renderClouds). GameRenderer is a
    // zero buffer - it has no virtual methods and the four methods our
    // render path calls (DisableUpdateThread, EnableUpdateThread,
    // turnOnLightLayer, turnOffLightLayer) are stubbed in link_stubs.cpp
    // and don't deref this.
    try {
        g_minecraftShim->options = new Options();
        // G5: viewDistance left at Options::init default (0). allChanged
        // is now wired to fire properly via setLevel below.
        g_minecraftShim->options->viewDistance = 0;
        MCLE_LOG("mcle_game_init: Options allocated at %p (viewDistance=%d, fancyGraphics=%d)",
                 (void*)g_minecraftShim->options,
                 g_minecraftShim->options->viewDistance,
                 (int)g_minecraftShim->options->fancyGraphics);
    } catch (...) {
        MCLE_LOG("mcle_game_init: Options ctor threw");
    }

    static char s_gameRendererShimBuf[sizeof(GameRenderer)] = {0};
    g_minecraftShim->gameRenderer =
        reinterpret_cast<GameRenderer *>(s_gameRendererShimBuf);
    MCLE_LOG("mcle_game_init: GameRenderer shim at %p (sizeof=%zu)",
             (void*)g_minecraftShim->gameRenderer, sizeof(GameRenderer));

    // Singleton: Minecraft::GetInstance() must return our shim before
    // LevelRenderer::allChanged runs (it calls GetInstance()->gameRenderer
    // -> DisableUpdateThread).
    Minecraft::m_instance = g_minecraftShim;
    MCLE_LOG("mcle_game_init: Minecraft::m_instance set to %p",
             (void*)Minecraft::m_instance);

    // G2b: LevelRenderer construction. Leaf-symbol stubs added in
    // WorldProbe/link_stubs.cpp let the link resolve. The ctor body still
    // executes upstream code - renderStars, createCloudMesh, the sky list
    // build via Tesselator - so this can SIGSEGV at runtime. Behind the
    // state=ticking flip so a crash here doesn't kill the blue-sky
    // milestone.
    MCLE_LOG("mcle_game_init: G2c construct LevelRenderer(shim, nullptr)...");
    try {
        g_levelRenderer = new LevelRenderer(g_minecraftShim, nullptr);
        MCLE_LOG("mcle_game_init: LevelRenderer at %p", (void*)g_levelRenderer);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: LevelRenderer ctor threw: %{public}s", e.what());
    } catch (...) {
        MCLE_LOG("mcle_game_init: LevelRenderer ctor threw unknown");
    }

    // G3e: wire the live ServerLevel as mc->level so upstream renderSky
    // can read getSkyColor / dimension / time-of-day. ServerLevel and
    // MultiPlayerLevel are siblings under Level - the cast keeps virtual
    // dispatch routed through the actual ServerLevel vtable.
    //
    // G5: also call LevelRenderer::setLevel(0, lvl) which sets level[0]
    // and calls allChanged(0) to allocate the chunk mesh array. Without
    // this, chunks[0].length stays at 0 and renderChunks dispatches no
    // geometry. Wrapped: SIGSEGV bypasses but a C++ throw is caught.
    if (g_levelRenderer && g_levels[0]) {
        MultiPlayerLevel *lvl = reinterpret_cast<MultiPlayerLevel *>(g_levels[0]);
        g_minecraftShim->level = lvl;
        MCLE_LOG("mcle_game_init: G5 calling LR->setLevel(0, %p)", (void*)lvl);
        try {
            g_levelRenderer->setLevel(0, lvl);
            MCLE_LOG("mcle_game_init: G5 setLevel returned, chunks[0].length=%u",
                     (unsigned)g_levelRenderer->chunks[0].length);
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: G5 setLevel threw: %{public}s", e.what());
        } catch (...) {
            MCLE_LOG("mcle_game_init: G5 setLevel threw unknown");
        }
    }

    // G3e-step2: Textures shim for LevelRenderer. Zero buffer, no
    // virtuals - the bindTexture / addMemTexture / removeMemTexture
    // stubs in link_stubs.cpp don't deref this. Lets renderClouds /
    // renderAdvancedClouds proceed past textures->bindTexture without
    // calling on a null pointer.
    static char s_texturesShimBuf[sizeof(Textures)] = {0};
    if (g_levelRenderer) {
        g_levelRenderer->textures =
            reinterpret_cast<Textures *>(s_texturesShimBuf);
        MCLE_LOG("mcle_game_init: Textures shim at %p (sizeof=%zu)",
                 (void*)g_levelRenderer->textures, sizeof(Textures));
    }

    MCLE_LOG("mcle_game_init: initImpl returning");
}

} // anonymous namespace

// Crash logger: install handlers for SIGSEGV/SIGBUS that print the
// faulting signal + address before re-raising the default handler so
// the os_log capture has the information even though iOS still kills
// the process. One-shot - resets to default after firing.
static void mcle_crash_handler(int sig, siginfo_t *info, void *) {
    MCLE_LOG("mcle: caught signal %d at addr %p (code=%d)",
             sig,
             info ? info->si_addr : (void *)0,
             info ? info->si_code : 0);
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, nullptr);
    raise(sig);
}

static void mcle_install_crash_handler() {
    struct sigaction sa{};
    sa.sa_sigaction = &mcle_crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
}

extern "C" void mcle_game_init(void) {
    if (g_initState != kStateUnstarted) return;
    mcle_install_crash_handler();
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

    // Render thread TLS init - mirrors the bootstrap thread setup.
    // Tile::setShape and Tesselator are both TLS-singletons; without
    // these on this thread the first tick null-derefs.
    static bool s_tickThreadTlsReady = false;
    if (!s_tickThreadTlsReady) {
        Tile::CreateNewThreadStorage();
        Tesselator::CreateNewThreadStorage(16 * 1024);
        Vec3::CreateNewThreadStorage();
        s_tickThreadTlsReady = true;
        MCLE_LOG("mcle_game_tick: render-thread Tile + Tesselator + Vec3 TLS initialized");
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
        // G2a: include cumulative DrawVertices count so we can see the
        // moment upstream renderer code starts driving the Metal hook.
        unsigned int chunksLen = 0;
        try { if (g_levelRenderer) chunksLen = (unsigned int)g_levelRenderer->chunks[0].length; } catch (...) {}
        MCLE_LOG("tick %llu - overworld=%p entities=%zu draws=%llu lists=%llu chunks0=%u",
                 static_cast<unsigned long long>(g_tickCount),
                 (void*)g_levels[0],
                 entityCount,
                 mcle_metal_draw_count(),
                 mcle_glbridge_list_count(),
                 chunksLen);
    }

    // G1B-probe runs once per second from the simulation tick (not the
    // render thread, where the upstream getSkyColor crash actually fires).
    // Different thread but same upstream code path - if the probe lands
    // cleanly here, the crash is render-thread-specific (TLS / state).
    mcle_world_g1b_probe_tick();
}

// G2c: drive upstream LevelRenderer::render() per frame from the iOS
// render thread. Increments DrawVertices counter when the renderer
// dispatches Tesselator batches (chunk geometry, entities, clouds,
// terrain). Wrapped in try/catch but SIGSEGV bypasses - the Minecraft::
// m_instance null chain (Biome::getSkyColor etc) may bite here. If so
// the signal handler logs the address, we patch the next null deref.
extern "C" void mcle_glbridge_replay_all_lists(void);
extern "C" void mcle_glbridge_metal_perspective(float fov_y_deg, float aspect,
                                                 float near_z, float far_z);
extern "C" void mcle_glbridge_matrix_mode(int mode);
extern "C" void mcle_glbridge_load_identity(void);
extern "C" void mcle_glbridge_translate(float, float, float);
extern "C" void mcle_glbridge_rotate(float angle_deg, float x, float y, float z);
extern "C" void mcle_metal_current_size(int*, int*);
extern "C" int  mcle_ios_input_poll_rx(int pad);
extern "C" int  mcle_ios_input_poll_ry(int pad);
extern "C" int  mcle_ios_input_poll_lx(int pad);
extern "C" int  mcle_ios_input_poll_ly(int pad);

extern "C" void mcle_world_drive_renderer(void) {
    if (g_initState != kStateTicking || !g_levelRenderer || !g_player) return;

    // Render-thread TLS init - mirrors the bootstrap + tick threads.
    // Tile / Tesselator / Vec3 are TLS-singletons; without these on this
    // thread the first frame null-derefs inside Tesselator getInstance,
    // Vec3::newTemp (Level::getSkyColor), or Tile::setShape.
    static bool s_renderThreadTlsReady = false;
    if (!s_renderThreadTlsReady) {
        Tile::CreateNewThreadStorage();
        Tesselator::CreateNewThreadStorage(16 * 1024);
        Vec3::CreateNewThreadStorage();
        s_renderThreadTlsReady = true;
        MCLE_LOG("mcle_world_drive_renderer: render-thread Tile + Tesselator + Vec3 TLS initialized");
    }

    try {
        // G3d-step3: set up projection + view via the GL matrix stack
        // each frame. Once upstream renderSky/renderClouds drive replays
        // naturally (G3e), they'll layer their per-pass glPush/Translate
        // /Rotate calls on top of this base camera.
        int vw = 0, vh = 0;
        mcle_metal_current_size(&vw, &vh);
        const float aspect = (vh > 0) ? ((float)vw / (float)vh) : 1.0f;

        mcle_glbridge_matrix_mode(0x1701 /* GL_PROJECTION */);
        mcle_glbridge_load_identity();
        mcle_glbridge_metal_perspective(70.0f, aspect, 0.05f, 1024.0f);

        mcle_glbridge_matrix_mode(0x1700 /* GL_MODELVIEW */);
        mcle_glbridge_load_identity();

        // G5-step8 TEMP non-parity: drive g_player->xRot/yRot/x/y/z
        // directly from the controller sticks so look-around + walk work
        // without a real LocalPlayer + Input::tick path. Upstream's
        // parity flow is LocalPlayer::tickInput -> Input::tick ->
        // InputManager.GetJoypadStick_* -> player. We can't run that
        // chain because LocalPlayer.cpp doesn't compile yet (UI/render
        // deps), and Input::tick crashes on Minecraft::localgameModes
        // which our shim doesn't have. Until LocalPlayer is wired,
        // this short-circuit makes the controller drive the camera +
        // position. No collision, no gravity - we float through the
        // world freely.
        {
            const float kLook = 4.0f / 1000.0f;     // ~4 degrees/frame at full deflection
            const float rx = (float)mcle_ios_input_poll_rx(0) * kLook;
            const float ry = (float)mcle_ios_input_poll_ry(0) * kLook;
            g_player->yRot += rx;
            g_player->xRot += ry;
            if (g_player->xRot > 90.0f)  g_player->xRot = 90.0f;
            if (g_player->xRot < -90.0f) g_player->xRot = -90.0f;

            // Left stick: y = forward/back along the player's facing
            // direction, x = strafe right/left. Free-fly (ignores y / pitch).
            const float kWalk = 0.4f / 1000.0f;       // blocks/frame at full deflection
            const float lx = (float)mcle_ios_input_poll_lx(0) * kWalk;
            const float ly = (float)mcle_ios_input_poll_ly(0) * kWalk;
            const float yawRad = g_player->yRot * (float)M_PI / 180.0f;
            const float fwdX   = -sinf(yawRad);  // upstream yaw convention: 0 = south (-Z)
            const float fwdZ   =  cosf(yawRad);
            const float strafeX = -fwdZ;         // 90deg right of forward
            const float strafeZ =  fwdX;
            g_player->x += fwdX  * (-ly) + strafeX * lx;
            g_player->z += fwdZ  * (-ly) + strafeZ * lx;

            // TEMP: preload radius is currently 0, so only the spawn chunk
            // is loaded server-side. Walking out of it makes Level::getBiome
            // deref into an unloaded chunk and crash at 0x68. Clamp x/z to
            // the spawn chunk's 16x16 footprint until preload covers more.
            static bool s_spawnChunkInit = false;
            static float s_minX, s_maxX, s_minZ, s_maxZ;
            if (!s_spawnChunkInit) {
                int cx = ((int)floorf(g_player->x)) >> 4;
                int cz = ((int)floorf(g_player->z)) >> 4;
                s_minX = (float)(cx * 16);
                s_maxX = (float)(cx * 16 + 15) + 0.999f;
                s_minZ = (float)(cz * 16);
                s_maxZ = (float)(cz * 16 + 15) + 0.999f;
                s_spawnChunkInit = true;
            }
            if (g_player->x < s_minX) g_player->x = s_minX;
            if (g_player->x > s_maxX) g_player->x = s_maxX;
            if (g_player->z < s_minZ) g_player->z = s_minZ;
            if (g_player->z > s_maxZ) g_player->z = s_maxZ;
        }

        // Apply player rotation (xRot=pitch, yRot=yaw) so the camera
        // looks where the player is facing. Order matches upstream
        // GameRenderer: pitch first, then yaw. yaw + 180 because Minecraft
        // yaw=0 looks south but our default camera looks down -Z (north).
        mcle_glbridge_rotate((float)g_player->xRot, 1.0f, 0.0f, 0.0f);
        mcle_glbridge_rotate((float)g_player->yRot + 180.0f, 0.0f, 1.0f, 0.0f);
        mcle_glbridge_translate(-(float)g_player->x,
                                -(float)g_player->y,
                                -(float)g_player->z);

        // G5-step3: process the dirty-chunk rebuild queue. Throttled to
        // every 15 frames (~250ms at 60fps) because upstream's
        // updateDirtyChunks has a 125ms cooldown - calling every 16ms
        // keeps dirtyChunkPresent stuck at false and the search never
        // re-enters. 250ms gives the cooldown room to expire so the
        // search loop runs each pass.
        //
        // G5-step7: bracket each call with os_log so signal-killed runs
        // still pin which upstream call took the SIGSEGV. NSLog buffers
        // can lose lines if process dies fast; os_log via MCLE_LOG is
        // retained by the system.
        {
            static int s_frameCounter = 0;
            static int s_dirtyCalls = 0;
            if ((s_frameCounter++ % 15) == 0) {
                if (s_dirtyCalls < 5) {
                    MCLE_LOG("WD_CKPT before updateDirtyChunks call=%d", s_dirtyCalls);
                }
                try { g_levelRenderer->updateDirtyChunks(); } catch (...) {}
                if (s_dirtyCalls < 5) {
                    MCLE_LOG("WD_CKPT after updateDirtyChunks call=%d", s_dirtyCalls);
                }
                s_dirtyCalls++;
            }
        }

        {
            static int s_renderCalls = 0;
            if (s_renderCalls < 3) MCLE_LOG("WD_CKPT before render call=%d", s_renderCalls);
            g_levelRenderer->render(g_player, /*layer*/0, /*alpha*/1.0,
                                    /*updateChunks*/false);
            if (s_renderCalls < 3) MCLE_LOG("WD_CKPT after render call=%d", s_renderCalls);
            s_renderCalls++;
        }

        // G3e-step5: re-enable renderSky/renderClouds with line-by-line
        // checkpoints inside Level::getSkyColor (LR_GSC tags). Last LR_GSC
        // line printed before crash pins the offending deref.
        try { g_levelRenderer->renderSky(1.0f);    } catch (...) {}
        try { g_levelRenderer->renderClouds(1.0f); } catch (...) {}

        mcle_glbridge_replay_all_lists();
    } catch (...) {}
}

// G3d: expose player position so the Metal pipeline can place the
// view matrix correctly. Returns 0 if the player is not yet constructed.
extern "C" int mcle_world_get_player_pos(float *out_x, float *out_y, float *out_z) {
    if (!g_player) return 0;
    if (out_x) *out_x = (float)g_player->x;
    if (out_y) *out_y = (float)g_player->y;
    if (out_z) *out_z = (float)g_player->z;
    return 1;
}

// Render-side bridge: lets the iOS frame driver know whether the world
// simulation is alive so it can switch the clear color and stop drawing
// the SWF menu over the world.
extern "C" int mcle_world_is_ticking(void) {
    return (g_initState == kStateTicking && g_levels[0] != nullptr) ? 1 : 0;
}

// G1B (interim): day-night sky driven by upstream Level::getTimeOfDay.
// Mirrors the brightness formula at Level.cpp:2049-2053 + the standard
// plains biome sky color (0x78A7FF) without going through getBiome,
// which crashes at offset 0x130 (separate investigation). Once that
// is fixed this routes through Level::getSkyColor for full parity.
extern "C" void mcle_world_get_sky_color(float *r, float *g, float *b) {
    // Plains biome sky as the static base (RGB 120, 167, 255).
    float baseR = 120.0f / 255.0f;
    float baseG = 167.0f / 255.0f;
    float baseB = 255.0f / 255.0f;
    float br = 1.0f;

    if (g_initState == kStateTicking && g_levels[0]) {
        try {
            // Mirrors Level::getSkyColor brightness term.
            float td = g_levels[0]->getTimeOfDay(1.0f);
            const float kPi = 3.14159265358979323846f;
            br = std::cos(td * kPi * 2.0f) * 2.0f + 0.5f;
            if (br < 0.0f) br = 0.0f;
            if (br > 1.0f) br = 1.0f;
        } catch (...) {
            br = 1.0f;
        }
    }

    if (r) *r = baseR * br;
    if (g) *g = baseG * br;
    if (b) *b = baseB * br;
}

// G1B-probe: layered call into upstream Level::getSkyColor pieces so
// the next sideload's log pins which step crashed at addr 0x130. Logs
// once per second to avoid spamming. Try/catch only catches C++
// throws; SIGSEGV bypasses but the per-step log still tells us which
// step was last reached before the kill.
extern "C" void mcle_world_g1b_probe_tick(void) {
    if (g_initState != kStateTicking || !g_levels[0] || !g_player) return;
    if ((g_tickCount % kLogEveryN) != 0) return;
    try {
        float td = g_levels[0]->getTimeOfDay(1.0f);
        MCLE_LOG("G1B-probe: getTimeOfDay=%f", (double)td);
    } catch (...) { MCLE_LOG("G1B-probe: getTimeOfDay threw"); return; }
    try {
        float rain = g_levels[0]->getRainLevel(1.0f);
        MCLE_LOG("G1B-probe: getRainLevel=%f", (double)rain);
    } catch (...) { MCLE_LOG("G1B-probe: getRainLevel threw"); return; }
    try {
        int xx = (int)g_player->x;
        int zz = (int)g_player->z;
        MCLE_LOG("G1B-probe: player xx=%d zz=%d", xx, zz);
        Biome *biome = g_levels[0]->getBiome(xx, zz);
        MCLE_LOG("G1B-probe: biome=%p", (void*)biome);
        if (!biome) return;
        float temp = biome->getTemperature();
        MCLE_LOG("G1B-probe: biome->getTemperature=%f", (double)temp);
        // G3e-step3: now that the Minecraft singleton + ColourTable shim
        // are wired, retry the path that crashed renderSky. If
        // biome->getSkyColor logs cleanly, the bad pointer is downstream
        // (Vec3::newTemp / shared_ptr dtor / virtual on level). If this
        // SIGSEGVs, the ColourTable shim isn't enough.
        int sc = biome->getSkyColor(temp);
        MCLE_LOG("G1B-probe: biome->getSkyColor=0x%08x", (unsigned)sc);
    } catch (...) { MCLE_LOG("G1B-probe: biome chain threw"); return; }
    try {
        Vec3 *sky = g_levels[0]->getSkyColor(g_player, 1.0f);
        MCLE_LOG("G1B-probe: level->getSkyColor=%p", (void*)sky);
        if (sky) {
            MCLE_LOG("G1B-probe: skyColor RGB=(%f, %f, %f)",
                     (double)sky->x, (double)sky->y, (double)sky->z);
        }
    } catch (...) { MCLE_LOG("G1B-probe: level->getSkyColor threw"); return; }
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
