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
#include <chrono>
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
extern "C" unsigned int mcle_glbridge_get_bound_texture(void);
extern "C" unsigned int mcle_glbridge_load_or_get_png_path(const char *path);
extern "C" void mcle_glbridge_call_list_stats(unsigned long *hits, unsigned long *misses,
                                                int *first_miss, int *first_hit, unsigned long *list_count);
extern "C" void mcle_glbridge_call_list_stats_ext(unsigned long *hits_low,
                                                    unsigned long *hits_high,
                                                    int *last_hit);
extern "C" void mcle_glbridge_fmt_stats(unsigned long *out, int max_count);
extern "C" void mcle_glbridge_skip_autoreplay(int id);

// G1B-probe: defined later in this same TU; forward-declared here so
// the tick path can call it before the definition appears.
extern "C" void mcle_world_g1b_probe_tick(void);

#include "../../../upstream/Minecraft.World/Minecraft.World.h"
#include "../../../upstream/Minecraft.World/Compression.h"
#include "../../../upstream/Minecraft.World/IntCache.h"
#include "../../../upstream/Minecraft.World/AABB.h"
#include "../../../upstream/Minecraft.World/OldChunkStorage.h"
#include "../../../upstream/Minecraft.Client/Chunk.h"
#include "../../../upstream/Minecraft.World/ConsoleSaveFileOriginal.h"
#include "../../../upstream/Minecraft.World/File.h"
#include "../../../upstream/Minecraft.World/FileInputStream.h"
#include "../../../upstream/Minecraft.World/Level.h"
#include "../../../upstream/Minecraft.World/Dimension.h"
#include "../../../upstream/Minecraft.World/LevelData.h"
#include "../../../upstream/Minecraft.World/LevelSettings.h"
#include "../../../upstream/Minecraft.World/LevelType.h"
#include "../../../upstream/Minecraft.World/MaterialColor.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/Recipes.h"
#include "../../../upstream/Minecraft.World/Tile.h"
#include "../../../upstream/Minecraft.World/GrassTile.h"
#include "../../../upstream/Minecraft.World/Icon.h"
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
#include "../../../upstream/Minecraft.Client/Gui.h"
#include "../../../upstream/Minecraft.Client/GameRenderer.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLevel.h"
#include "../../../upstream/Minecraft.Client/PlayerChunkMap.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLocalPlayer.h"
#include "../../../upstream/Minecraft.Client/Textures.h"
#include "../../../upstream/Minecraft.Client/TexturePackRepository.h"
#include "../../../upstream/Minecraft.Client/FolderTexturePack.h"
#include "../../../upstream/Minecraft.Client/TextureManager.h"
#include "../../../upstream/Minecraft.Client/TextureAtlas.h"

namespace {
// AbstractTexturePack.cpp can't compile (pulls UIScene_LanguageSelector
// + Credits which need XC_LANGUAGE/XC_LOCALE enums we don't have).
// Override every virtual that would normally come from there. Only
// hasFile/getResourceImplementation/getImageResource matter for the
// stitch path - the rest can return empty/false.
class IosFolderTexturePack : public FolderTexturePack {
public:
    IosFolderTexturePack(DWORD id, const std::wstring &name,
                          File *folder, TexturePack *fallback)
        : FolderTexturePack(id, name, folder, fallback) {}

    // From TexturePack pure virtuals
    bool hasData() override { return true; }
    bool isLoadingData() override { return false; }
    DLCPack *getDLCPack() override { return nullptr; }

    // From AbstractTexturePack virtuals - inlined stubs since the .cpp
    // doesn't compile for us. Stitch only really needs hasFile + the
    // image load path; rest are effectively no-ops.
    void loadIcon() override {}
    void loadComparison() override {}
    void loadDescription() override {}
    void loadName() override {}
    InputStream *getResource(const std::wstring &, bool) override { return nullptr; }
    void unload(Textures *) override {}
    void load(Textures *) override {}
    bool hasFile(const std::wstring &name, bool) override { return hasFile(name); }
    bool hasFile(const std::wstring &name) override {
        // FolderTexturePack uses File-based lookup; we just check the
        // path under our root folder. Conservative: report yes so stitch
        // takes the loadable path. Actual file check happens in
        // BufferedImage::BufferedImage(File) via our LoadTextureData.
        (void)name;
        return true;
    }
    DWORD getId() override { return 0; }
    std::wstring getName() override { return L"default"; }
    std::wstring getDesc1() override { return L""; }
    std::wstring getDesc2() override { return L""; }
    std::wstring getWorldName() override { return L""; }
    std::wstring getAnimationString(const std::wstring &, const std::wstring &, bool) override { return L""; }
    std::wstring getAnimationString(const std::wstring &, const std::wstring &) override { return L""; }
    // FolderTexturePack::getPath returns "Common\\" + file->getPath() + "\\"
    // which embeds the absolute Documents path on iOS. Drop the file path
    // chunk so callers get a relative "Common/" prefix - BufferedImage
    // then builds a clean relative name and our load shim resolves it
    // against Documents/.
    std::wstring getPath(bool /*bTitleUpdateTexture*/ = false, const char * /*pchBDPatchFilename*/ = nullptr) override {
        return L"Common/";
    }
    BufferedImage *getImageResource(const std::wstring &filename, bool filenameHasExtension, bool bTitleUpdateTexture, const std::wstring &drive) override {
        // BufferedImage builds path as wDrive + L"res" + filePath... with
        // no separator. So filePath needs a leading slash for the result
        // to be "Common/res/<file>.png" not "Common/resterrain.png".
        std::wstring path = (filename.empty() || filename[0] == L'/')
            ? filename : (L"/" + filename);
        return new BufferedImage(path, filenameHasExtension, bTitleUpdateTexture, drive);
    }
    void loadColourTable() override {}
    void loadUI() override {}
    void unloadUI() override {}
    std::wstring getXuiRootPath() override { return L""; }
    PBYTE getPackIcon(DWORD &) override { return nullptr; }
    PBYTE getPackComparison(DWORD &) override { return nullptr; }
    unsigned int getDLCParentPackId() override { return 0; }
    unsigned char getDLCSubPackId() override { return 0; }
};
} // namespace

#include "4JLibs/inc/4J_Storage.h"

extern "C" int mcle_log_msg(const char *msg);
#define MCLE_LOG(fmt, ...) do { \
    char _mcle_buf[1024]; \
    snprintf(_mcle_buf, sizeof(_mcle_buf), "[MCLE] " fmt, ##__VA_ARGS__); \
    mcle_log_msg(_mcle_buf); \
} while (0)

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
// Wall-clock ns of the last simulation tick (set by mcle_game_tick when the
// 20Hz throttle gate fires). Render thread reads this to compute the
// partial-tick alpha for camera-position interpolation.
std::atomic<uint64_t>         g_lastSimTickNs{ 0 };
// Gui instance for HUD rendering (hotbar, health bars, crosshair).
// Constructed in mcle_game_init after textures->stitch (parity with
// upstream Minecraft::init). Render thread calls g_gui->render() each frame.
Gui                          *g_gui = nullptr;
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

    // IntCache is per-thread TLS too. BiomeSource::getRawBiomeBlock and
    // every Layer::getArea path reads tls->toosmall at offset 0x68; if
    // CreateNewThreadStorage hasn't been called the deref crashes at
    // 0x68 (the chunk preload (16,15) crash for procgen).
    IntCache::CreateNewThreadStorage();
    MCLE_LOG("mcle_game_init: IntCache::CreateNewThreadStorage done");

    // The remaining TLS singletons Win64_Minecraft.cpp:1288-1295 sets up.
    // AABB used by entity collision; OldChunkStorage used by saveData.ms
    // legacy load path; Level lighting cache used by chunk lighting +
    // post-process. Missing these can cause 0x128 / similar offsets in
    // procgen for chunks that exercise structure features or lighting.
    AABB::CreateNewThreadStorage();
    MCLE_LOG("mcle_game_init: AABB::CreateNewThreadStorage done");
    OldChunkStorage::CreateNewThreadStorage();
    MCLE_LOG("mcle_game_init: OldChunkStorage::CreateNewThreadStorage done");
    Level::enableLightingCache();
    MCLE_LOG("mcle_game_init: Level::enableLightingCache done");
    Chunk::CreateNewThreadStorage();
    MCLE_LOG("mcle_game_init: Chunk::CreateNewThreadStorage done");

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
        // G5-step13: bump radius to 3 now that IntCache TLS is init'd.
        // Original target was 7 (mirroring upstream MinecraftServer::run
        // which preloads 13x13 = ~208-block radius). r=3 = 7x7 = 49
        // chunks, enough to walk a meaningful distance without leaving
        // the loaded area. Stepping further is still capped via the
        // walk clamp below until preload covers the full target.
        static constexpr int kPreloadRadiusChunks = 3;
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
                    // asyncPostProcess=false: hit the synchronous branch
                    // (chunk->checkPostProcess) instead of routing through
                    // MinecraftServer::addPostProcessRequest, which derefs
                    // m_postProcessCS - that's only InitializeCriticalSection'd
                    // late inside MinecraftServer::run() which we don't call.
                    if (g_levels[0]->cache && g_levels[0]->cache->create(cx, cz, false)) {
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

        // Register player with chunkMap. With patch-playerchunkmap-add-
        // async, every chunk in the radius is queued via addRequests
        // (no synchronous 14-ring spiral that crashed before). tick()
        // drains one chunk per call, so the full radius streams in
        // gradually over a few seconds.
        try {
            PlayerChunkMap *cm = g_levels[0]->getChunkMap();
            MCLE_LOG("mcle_game_init: chunkMap=%p add(player)", (void*)cm);
            if (cm) cm->add(g_player);
            MCLE_LOG("mcle_game_init: chunkMap->add returned, players=%zu",
                     cm ? cm->players.size() : 0);
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: chunkMap->add threw: %{public}s", e.what());
        } catch (...) {
            MCLE_LOG("mcle_game_init: chunkMap->add threw unknown");
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
        // fancyGraphics=true gives leaves the cutout-transparency path
        // (LevelRenderer.cpp:427 Tile::leaves->setFancy). Clouds normally
        // branch to renderAdvancedClouds when this is true, but our
        // patch-renderclouds-force-simple.py rewires renderClouds to
        // always take the simple 2D path on iOS - so we get fancy leaves
        // without the unsupported advanced cloud path.
        g_minecraftShim->options->fancyGraphics = true;
        // Ambient occlusion controls TileRenderer's per-vertex
        // light-blending pass (TileRenderer.cpp:5246+). With it off, every
        // face gets a single uniform light value from one block and
        // shadows look blocky. With it on, each face corner averages the
        // light of 4 neighbour blocks so shadows fade smoothly across
        // tree canopies, terrain folds, etc. Upstream Options::init sets
        // ambientOcclusion = true by default; our zero-buffer Options
        // skips init so we have to set it explicitly.
        g_minecraftShim->options->ambientOcclusion = true;
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
        g_minecraftShim->levelRenderer = g_levelRenderer;
        MCLE_LOG("mcle_game_init: LevelRenderer at %p", (void*)g_levelRenderer);

        // Exclude world-decoration display lists from the per-frame
        // auto-replay. They're already drawn by upstream renderSky /
        // renderAdvancedClouds via glCallList - auto-replaying them
        // again draws a second copy anchored to the player. The 100-
        // radius haloRingList is the worst offender: upstream never
        // calls it (gated on a specific skin id) so without skipping it
        // we render a textured ring on every frame that sits as a
        // stuck "wedge" across the upper sky.
        const int starList     = g_levelRenderer->starList;
        const int skyList      = starList + 1;
        const int darkList     = starList + 2;
        const int haloRingList = starList + 3;
        mcle_glbridge_skip_autoreplay(starList);
        mcle_glbridge_skip_autoreplay(skyList);
        mcle_glbridge_skip_autoreplay(darkList);
        mcle_glbridge_skip_autoreplay(haloRingList);
        // createCloudMesh allocates 7 list IDs immediately after the
        // four above. The 7-deep range stays in MemoryTracker's
        // genLists order so we can derive it from haloRingList + 1.
        for (int i = 0; i < 7; i++) {
            mcle_glbridge_skip_autoreplay(haloRingList + 1 + i);
        }
        MCLE_LOG("mcle_game_init: registered %d world-decoration lists "
                 "for autoreplay skip (starList=%d, haloRingList=%d, "
                 "cloudList=%d..%d)",
                 4 + 7, starList, haloRingList,
                 haloRingList + 1, haloRingList + 7);
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

        // G5-step16: re-center the render grid around the player.
        // setLevel -> allChanged calls resortChunks but only if mc->
        // cameraTargetPlayer was wired BEFORE setLevel ran. Calling
        // it explicitly here with current player coords guarantees
        // the grid covers our loaded chunks.
        try {
            int px = (int)floorf(g_player->x);
            int py = (int)floorf(g_player->y);
            int pz = (int)floorf(g_player->z);
            g_levelRenderer->resortChunks(px, py, pz);
            MCLE_LOG("mcle_game_init: G5 resortChunks(%d,%d,%d) done", px, py, pz);
        } catch (...) {
            MCLE_LOG("mcle_game_init: G5 resortChunks threw");
        }

        // Force-dirty every render chunk in the now-recentered grid +
        // signal the rebuild thread.
        try {
            int dirtied = 0;
            for (size_t i = 0; i < g_levelRenderer->chunks[0].length; i++) {
                Chunk *c = g_levelRenderer->chunks[0][i].chunk;
                if (!c) continue;
                g_levelRenderer->setGlobalChunkFlag(
                    c->x, c->y, c->z, lvl,
                    LevelRenderer::CHUNK_FLAG_DIRTY);
                dirtied++;
            }
            MCLE_LOG("mcle_game_init: G5 force-dirty %d chunks", dirtied);
            g_levelRenderer->nonStackDirtyChunksAdded();
            MCLE_LOG("mcle_game_init: G5 nonStackDirtyChunksAdded done");
        } catch (...) {
            MCLE_LOG("mcle_game_init: G5 force-dirty / nonStackDirtyChunksAdded threw");
        }
    }

    // Real Textures + stitch path. PreStitchedTextureMap::stitch reads
    // Minecraft::GetInstance()->skins->getSelected()->getImageResource(
    // L"terrain.png", ...) so we need:
    //   1. a real TexturePack pointing at Documents/Common (FolderTexturePack
    //      walks res/<filename>)
    //   2. it assigned to TexturePackRepository::DEFAULT_TEXTURE_PACK so the
    //      stub getSelected returns it
    //   3. g_minecraftShim->skins pointing to our zero-buffer TPR so the
    //      Minecraft::GetInstance()->skins->getSelected() chain reaches it
    {
        const char *root = StorageManager.GetSaveRootPath();
        if (root && *root) {
            std::string commonDir = std::string(root) + "/Common";
            std::wstring wCommonDir(commonDir.begin(), commonDir.end());
            File *commonFile = new File(wCommonDir);
            IosFolderTexturePack *pack = new IosFolderTexturePack(
                TexturePackRepository::DEFAULT_TEXTURE_PACK_ID,
                L"default", commonFile, nullptr);
            TexturePackRepository::DEFAULT_TEXTURE_PACK = pack;
            MCLE_LOG("mcle_game_init: DEFAULT_TEXTURE_PACK at %p (root=%s)",
                     (void*)pack, root);
        }
    }

    static char s_tprBuf[sizeof(TexturePackRepository)] = {0};
    static char s_optsBuf[sizeof(Options)] = {0};
    TexturePackRepository *skins =
        reinterpret_cast<TexturePackRepository *>(s_tprBuf);
    Options *options = reinterpret_cast<Options *>(s_optsBuf);
    g_minecraftShim->skins = skins;

    // TextureManager is a singleton - createTextureID() and createTexture()
    // both go through getInstance() which returns nullptr until we allocate
    // one. Upstream's per-platform main does this at boot.
    if (TextureManager::getInstance() == nullptr) {
        TextureManager::createInstance();
        MCLE_LOG("mcle_game_init: TextureManager::createInstance done at %p",
                 (void*)TextureManager::getInstance());
    }

    if (g_levelRenderer) {
        try {
            Textures *textures = new Textures(skins, options);
            g_levelRenderer->textures = textures;
            g_minecraftShim->textures = textures;
            MCLE_LOG("mcle_game_init: Textures ctor done at %p", (void*)textures);
            try {
                textures->stitch();
                MCLE_LOG("mcle_game_init: textures->stitch done");
                // Diagnostic: confirm grass / dirt icons resolved to distinct
                // atlas slots. If grass-DOWN icon == grass-UP icon, the bug
                // isn't in tessellation - it's in icon registration / lookup.
                try {
                    // Tile::grass is GrassTile* and that's only fwd-declared
                    // here, so go through Tile::tiles[id] which is Tile* and
                    // dispatch virtually.
                    Tile *grassTile = Tile::tiles[2]; // grass id
                    Tile *dirtTile  = Tile::tiles[3]; // dirt id
                    Icon *grassUp   = grassTile ? grassTile->getTexture(1) : nullptr;
                    Icon *grassDown = grassTile ? grassTile->getTexture(0) : nullptr;
                    Icon *dirtIcon  = dirtTile  ? dirtTile->getTexture(0)  : nullptr;
                    MCLE_LOG("ICON_CKPT grass UP=%p DOWN=%p, dirt=%p (DOWN should == dirt)",
                             grassUp, grassDown, dirtIcon);
                    if (grassUp) {
                        MCLE_LOG("ICON_CKPT grassUp uv u0=%f v0=%f u1=%f v1=%f",
                                 grassUp->getU0(true), grassUp->getV0(true),
                                 grassUp->getU1(true), grassUp->getV1(true));
                    }
                    if (grassDown) {
                        MCLE_LOG("ICON_CKPT grassDown uv u0=%f v0=%f u1=%f v1=%f",
                                 grassDown->getU0(true), grassDown->getV0(true),
                                 grassDown->getU1(true), grassDown->getV1(true));
                    }
                    if (dirtIcon) {
                        MCLE_LOG("ICON_CKPT dirt uv u0=%f v0=%f u1=%f v1=%f",
                                 dirtIcon->getU0(true), dirtIcon->getV0(true),
                                 dirtIcon->getU1(true), dirtIcon->getV1(true));
                    }
                    // Grass side: check the IS_GRASS_SIDE flag (1) is set
                    // and the iconSideOverlay pointer is non-null. Either
                    // missing breaks the second-pass biome-tint overlay
                    // at TileRenderer.cpp:5599+ that gives grass sides
                    // their biome color.
                    Icon *grassSide = grassTile ? grassTile->getTexture(2) : nullptr;
                    int sideFlags = grassSide ? grassSide->getFlags() : -1;
                    Icon *sideOverlay = GrassTile::getSideTextureOverlay();
                    MCLE_LOG("ICON_CKPT grassSide=%p flags=%d (IS_GRASS_SIDE=1) sideOverlay=%p",
                             grassSide, sideFlags, sideOverlay);
                    if (sideOverlay) {
                        MCLE_LOG("ICON_CKPT sideOverlay uv u0=%f v0=%f u1=%f v1=%f",
                                 sideOverlay->getU0(true), sideOverlay->getV0(true),
                                 sideOverlay->getU1(true), sideOverlay->getV1(true));
                    }
                } catch (...) {
                    MCLE_LOG("ICON_CKPT diagnostic threw");
                }
            } catch (...) {
                MCLE_LOG("mcle_game_init: textures->stitch threw");
            }
        } catch (...) {
            MCLE_LOG("mcle_game_init: Textures ctor threw");
        }
    }

    // Wire minecraft->player. Gui::render reads
    // player->m_iScreenSection (patched out in patch-gui-screensection.py),
    // getXboxPad(), inventory, etc. ServerPlayer-as-MultiplayerLocalPlayer
    // via reinterpret_pointer_cast - safe for the read-only HUD path now
    // that the only non-virtual layout-dependent access (m_iScreenSection)
    // is gone.
    if (g_player && g_minecraftShim) {
        std::shared_ptr<MultiplayerLocalPlayer> *slot =
            reinterpret_cast<std::shared_ptr<MultiplayerLocalPlayer> *>(
                &g_minecraftShim->player);
        new (slot) std::shared_ptr<MultiplayerLocalPlayer>(
            std::reinterpret_pointer_cast<MultiplayerLocalPlayer>(g_player));
        MCLE_LOG("mcle_game_init: minecraft->player aliased to g_player");
    }

    // Construct Gui (HUD renderer) - parity with upstream Minecraft::init.
    // Stores minecraft pointer + initialises bookkeeping. Actual render
    // happens in mcle_world_drive_renderer per frame.
    if (g_minecraftShim) {
        try {
            g_gui = new Gui(g_minecraftShim);
            MCLE_LOG("mcle_game_init: Gui ctor at %p", (void*)g_gui);
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: Gui ctor threw: %{public}s", e.what());
        } catch (...) {
            MCLE_LOG("mcle_game_init: Gui ctor threw unknown");
        }
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
    // os_log buffers asynchronously, so any CKPT lines in flight at
    // the moment of the SIGSEGV can be lost when the process dies.
    // Sleep briefly to give the kernel log subsystem time to drain.
    usleep(200000);  // 200ms
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
        IntCache::CreateNewThreadStorage();
        AABB::CreateNewThreadStorage();
        OldChunkStorage::CreateNewThreadStorage();
        Level::enableLightingCache();
        Chunk::CreateNewThreadStorage();
        s_tickThreadTlsReady = true;
        MCLE_LOG("mcle_game_tick: tick-thread full TLS init done");
    }

    g_tickCount++;

    if (g_initState != kStateTicking || !g_levels[0]) {
        if ((g_tickCount % kLogEveryN) == 0) {
            MCLE_LOG("tick %llu (probe lib running, simulation idle, state=%d)",
                     static_cast<unsigned long long>(g_tickCount), g_initState.load());
        }
        return;
    }

    // Throttle simulation to 20Hz (50ms per tick) to match upstream parity.
    // CADisplayLink calls us at 60Hz; without this, aiStep + tickEntities run
    // 3x too fast and walking / gravity / friction all feel sped up.
    // Publish tick start time so the render thread can compute partial-tick
    // alpha for smooth camera interp between 20Hz simulation steps.
    {
        using clock = std::chrono::steady_clock;
        static auto s_lastSimTick = clock::now() - std::chrono::milliseconds(50);
        const auto now = clock::now();
        const auto sinceMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - s_lastSimTick).count();
        if (sinceMs < 50) return;
        s_lastSimTick = now;
        g_lastSimTickNs.store(
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count(),
            std::memory_order_relaxed);
    }

    // Tick all three dimensions in order. Parity with how the upstream
    // server's runUpdate iterates levels[] each frame.
    try {
        for (int i = 0; i < 3; ++i) {
            if (!g_levels[i]) continue;
            g_levels[i]->tick();
            g_levels[i]->tickEntities();
        }
        // LevelRenderer::tick increments the renderer-side `ticks` member
        // which renderClouds reads for the cloud drift offset
        // (time * 0.03f at LevelRenderer.cpp:1330). Without this the cloud
        // layer is frozen in place. Also drives destroying-block animations
        // and the per-second tick maintenance branch.
        if (g_levelRenderer) {
            static int s_lrTickLog = 0;
            try {
                g_levelRenderer->tick();
                if (s_lrTickLog < 3) {
                    MCLE_LOG("LR_TICK_OK call=%d", s_lrTickLog);
                    s_lrTickLog++;
                }
            } catch (const std::exception &e) {
                if (s_lrTickLog < 3) {
                    MCLE_LOG("LR_TICK_THREW: %{public}s", e.what());
                    s_lrTickLog++;
                }
            } catch (...) {
                if (s_lrTickLog < 3) {
                    MCLE_LOG("LR_TICK_THREW unknown");
                    s_lrTickLog++;
                }
            }
        }
        // TEMP-NON-PARITY day-time speedup for visual debug. Each tick
        // already advances dayTime by 1 (parity, 20-min cycle). Adding +N
        // here gives a (20/(N+1))-min cycle. N=9 -> ~2-min full day,
        // visible inside a short test session. Revert to 0 for parity once
        // visual confirmation lands.
        constexpr int kDayTimeSpeedup = 9;
        if (kDayTimeSpeedup > 0 && g_levels[0]) {
            g_levels[0]->setDayTime(g_levels[0]->getDayTime() + kDayTimeSpeedup);
        }
        // DAYTIME diagnostic: every 100 sim ticks, log getDayTime so we can
        // verify ServerLevel::tick is actually incrementing it. If this is
        // flat, virtual dispatch isn't reaching ServerLevel::tick or
        // RULE_DAYLIGHT is false.
        if (g_levels[0]) {
            static int s_dayTimeLog = 0;
            if ((s_dayTimeLog++ % 100) == 0) {
                try {
                    int64_t dt = g_levels[0]->getDayTime();
                    int64_t gt = g_levels[0]->getGameTime();
                    MCLE_LOG("DAYTIME_CKPT log=%d gameTime=%lld dayTime=%lld",
                             s_dayTimeLog, (long long)gt, (long long)dt);
                } catch (...) {}
            }
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

    // Player physics: run aiStep so xxa/yya/jumping (set per-frame from the
    // controller in mcle_world_drive_renderer) drive travel() -> move() with
    // collision against level block AABBs and gravity. tickEntities above
    // already saved xOld via Level::tick before calling ServerPlayer::tick
    // (which does no physics), so the camera-interp anchor is correct.
    if (g_player) {
        try { g_player->aiStep(); } catch (...) {
            MCLE_LOG("mcle_game_tick: aiStep threw");
        }
    }

    // On-demand chunk streaming. Walk clamp is gone; instead drive
    // PlayerChunkMap each tick so new chunks load as the player moves.
    if (g_player && g_levels[0]) {
        try {
            PlayerChunkMap *cm = g_levels[0]->getChunkMap();
            if (cm) {
                // Log player pos vs lastMove + addRequests size every
                // ~30 ticks so we can see whether move() is queuing
                // anything and whether tick() drains it.
                static int s_pcmLog = 0;
                if ((s_pcmLog++ % 30) == 0) {
                    double dx = g_player->lastMoveX - g_player->x;
                    double dz = g_player->lastMoveZ - g_player->z;
                    MCLE_LOG("PCM_CKPT players=%zu px=%.1f pz=%.1f "
                             "lastMoveX=%.1f lastMoveZ=%.1f distSq=%.1f "
                             "(thresh=64)",
                             cm->players.size(),
                             g_player->x, g_player->z,
                             g_player->lastMoveX, g_player->lastMoveZ,
                             dx * dx + dz * dz);
                }
                cm->move(g_player);
                cm->tick();
            }
        } catch (...) {}
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
extern "C" void mcle_glbridge_set_fog_enabled(int enabled);
extern "C" void mcle_glbridge_set_fog_color(float r, float g, float b, float a);
extern "C" void mcle_glbridge_set_fog_start(float v);
extern "C" void mcle_glbridge_set_fog_end(float v);
extern "C" void mcle_world_get_sky_color(float *r, float *g, float *b);
extern "C" void mcle_lightmap_set_entry(int idx, float r, float g, float b);
extern "C" void mcle_lightmap_upload(void);
extern "C" void mcle_metal_current_size(int*, int*);
extern "C" int  mcle_ios_input_poll_rx(int pad);
extern "C" int  mcle_ios_input_poll_ry(int pad);
extern "C" int  mcle_ios_input_poll_lx(int pad);
extern "C" int  mcle_ios_input_poll_ly(int pad);
extern "C" int  mcle_ios_input_poll_buttons(int pad);
extern "C" void mcle_hud_draw_textured_quad(int x, int y, int w, int h,
                                             float u0, float v0,
                                             float u1, float v1,
                                             unsigned int tex_id);

extern "C" void mcle_world_drive_renderer(void) {
    if (g_initState != kStateTicking || !g_levelRenderer || !g_player) return;

    // Render-thread TLS init - mirrors the bootstrap + tick threads.
    // Tile / Tesselator / Vec3 are TLS-singletons; without these on this
    // thread the first frame null-derefs inside Tesselator getInstance,
    // Vec3::newTemp (Level::getSkyColor), or Tile::setShape.
    static bool s_renderThreadTlsReady = false;
    if (!s_renderThreadTlsReady) {
        MCLE_LOG("RTLS_CKPT step=1 Tile");
        Tile::CreateNewThreadStorage();
        MCLE_LOG("RTLS_CKPT step=2 Tesselator");
        Tesselator::CreateNewThreadStorage(16 * 1024);
        MCLE_LOG("RTLS_CKPT step=3 Vec3");
        Vec3::CreateNewThreadStorage();
        MCLE_LOG("RTLS_CKPT step=4 IntCache");
        IntCache::CreateNewThreadStorage();
        MCLE_LOG("RTLS_CKPT step=5 AABB");
        AABB::CreateNewThreadStorage();
        MCLE_LOG("RTLS_CKPT step=6 OldChunkStorage");
        OldChunkStorage::CreateNewThreadStorage();
        MCLE_LOG("RTLS_CKPT step=7 Level lighting cache");
        Level::enableLightingCache();
        MCLE_LOG("RTLS_CKPT step=8 Chunk");
        Chunk::CreateNewThreadStorage();
        s_renderThreadTlsReady = true;
        MCLE_LOG("mcle_world_drive_renderer: render-thread full TLS init done");
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
        // Match upstream: simulation ticks at 20Hz (50ms), camera renders
        // at frame rate using xOld + (x - xOld) * partialTick. Movement
        // speeds match Minecraft: ~4.317 blocks/sec walking, ~140 deg/sec
        // look at full deflection.
        float frame_partial_tick = 1.0f;
        {
            // Look stays per-frame for responsiveness (matches upstream
            // GameRenderer doing rotation outside the tick).
            // Stick polls return -1000..1000. 140 deg/sec at 60fps full deflection
            // = 2.33 deg/frame, so kLook = 140 / 60 / 1000 = 0.00233.
            const float kLook = 140.0f / 60.0f / 1000.0f;
            const float rx = (float)mcle_ios_input_poll_rx(0) * kLook;
            const float ry = (float)mcle_ios_input_poll_ry(0) * kLook;
            g_player->yRot += rx;
            g_player->xRot += ry;
            if (g_player->xRot > 90.0f)  g_player->xRot = 90.0f;
            if (g_player->xRot < -90.0f) g_player->xRot = -90.0f;

            // Walk input fed into the player's xxa/yya fields - LivingEntity::aiStep
            // (called from game tick after tickEntities) reads these and runs
            // travel() -> moveRelative() -> move() with collision against the
            // level's block AABBs. xxa = strafe, yya = forward (upstream sign).
            // Stick poll returns -1000..1000; LivingEntity expects ~[-1, 1].
            // getSpeed() inside travel() applies the actual walking speed.
            const float kStickToInput = 1.0f / 1000.0f;
            // Stick lx>0 = right; xxa>0 = strafe-right in moveRelative. User
            // reported inverted - flipping sign to match upstream convention.
            g_player->xxa = -(float)mcle_ios_input_poll_lx(0) * kStickToInput;
            g_player->yya = -(float)mcle_ios_input_poll_ly(0) * kStickToInput;

            // Jump = A button (bit 0 = _360_JOY_BUTTON_A). LivingEntity::aiStep
            // reads `jumping` and applies jumpFromGround() when onGround.
            const int buttons = mcle_ios_input_poll_buttons(0);
            g_player->setJumping((buttons & 0x1) != 0);

            // Partial-tick alpha for camera position interp. Sim ticks at 20Hz
            // (50ms) but rendering at 60Hz - without interp the camera judders
            // every tick. renderChunks already does
            //   xOff = xOld + (x - xOld) * alpha
            // so we just need to feed alpha = (now - lastTick) / 50ms.
            const uint64_t lastTickNs = g_lastSimTickNs.load(std::memory_order_relaxed);
            if (lastTickNs > 0) {
                using clock = std::chrono::steady_clock;
                const uint64_t nowNs = (uint64_t)std::chrono::duration_cast<
                    std::chrono::nanoseconds>(clock::now().time_since_epoch()).count();
                const uint64_t deltaNs = (nowNs > lastTickNs) ? (nowNs - lastTickNs) : 0;
                float a = (float)((double)deltaNs / 50000000.0);
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;
                frame_partial_tick = a;
            }
        }

        // Apply player rotation (xRot=pitch, yRot=yaw) so the camera
        // looks where the player is facing. Order matches upstream
        // GameRenderer: pitch first, then yaw. yaw + 180 because Minecraft
        // yaw=0 looks south but our default camera looks down -Z (north).
        mcle_glbridge_rotate((float)g_player->xRot, 1.0f, 0.0f, 0.0f);
        mcle_glbridge_rotate((float)g_player->yRot + 180.0f, 0.0f, 1.0f, 0.0f);
        // Eye-height offset. Parity with GameRenderer.cpp:463+576:
        //   float heightOffset = player->heightOffset - 1.62f;
        //   glTranslatef(0, heightOffset, 0);
        // ServerPlayer sets heightOffset=0 (4J fix - see ServerPlayer.cpp:119)
        // so this resolves to glTranslate(0, -1.62, 0). Lifts the camera to
        // standing eye level instead of leaving it at foot level.
        mcle_glbridge_translate(0.0f, (float)g_player->heightOffset - 1.62f, 0.0f);
        // Player translation is applied INSIDE upstream LevelRenderer::renderChunks
        // via glPushMatrix; glTranslatef(-xOff, -yOff, -zOff). Adding it here too
        // would double-translate chunks. Sky/clouds/sun apply their own translations
        // per-pass so they're unaffected.

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
            // Chunk build throughput. updateDirtyChunks builds at most
            // one chunk per call (no worker threads on iOS - upstream's
            // _LARGE_WORLDS spawn path isn't wired). At one call per 15
            // frames, 2304 visible chunks take roughly 10 minutes to
            // appear - the "cyan-below-horizon" gap is just chunks that
            // haven't compiled yet.
            //
            // Drive it every frame and loop a few times per frame so
            // freshly-loaded worlds catch up in seconds. Cap the inner
            // loop so a flood of dirty work can't blow the frame budget.
            static int s_dirtyCalls = 0;
            constexpr int kMaxBuildsPerFrame = 4;
            int builds = 0;
            // Skip the whole loop if textures aren't ready yet. tiles
            // with uninit icons return null from getTexture; the
            // TileRenderer null-guard returns null from the fallback
            // path; the caller (tesselateBlock...) then derefs that
            // null icon to read UV coords and faults at addr 0x0.
            // Wait until textures->stitch has populated icons.
            Minecraft *mcShimRT = Minecraft::GetInstance();
            const bool texturesReady =
                mcShimRT != nullptr && mcShimRT->textures != nullptr;
            for (; texturesReady && builds < kMaxBuildsPerFrame; builds++) {
                // updateDirtyChunks returns true only when it has more
                // atomic-neighbor work to do for the chunk it just built,
                // not "there are more dirty chunks elsewhere". So keep
                // calling regardless - the cap kMaxBuildsPerFrame bounds
                // the per-frame cost.
                try { g_levelRenderer->updateDirtyChunks(); } catch (...) { break; }
            }
            // Log once per second so we can see throughput without
            // spamming.
            static int s_frameCounter = 0;
            if ((s_frameCounter++ % 60) == 0) {
                MCLE_LOG("WD_CKPT updateDirtyChunks call=%d builtThisFrame=%d",
                         s_dirtyCalls, builds);
                s_dirtyCalls++;
            }
        }

        // setupFog equivalent. Mirrors upstream GameRenderer::setupFog
        // (GameRenderer.cpp:2102+) and the fog-color computation at
        // GameRenderer.cpp:1944-1968. Fog color = Level::getFogColor (muted
        // blue tones from the colour table, modulated by time-of-day
        // brightness). Then at low view distance, blend toward the
        // sunrise-color palette in proportion to how much the view vector
        // points toward the sun. That blend creates the "warm horizon
        // toward the sun at dawn/dusk" effect.
        //
        // Upstream renderSky toggles GL_FOG via glEnable/glDisable around
        // sun/moon/stars so they don't fade; our shim tracks those.
        {
            float fr = 0.47f, fg = 0.65f, fb = 1.0f;
            if (g_initState == kStateTicking && g_levels[0] && g_player) {
                try {
                    Vec3 *fc = g_levels[0]->getFogColor(frame_partial_tick);
                    if (fc) { fr = (float)fc->x; fg = (float)fc->y; fb = (float)fc->z; }

                    float td = g_levels[0]->getTimeOfDay(frame_partial_tick);
                    float sunAngle = g_levels[0]->getSunAngle(frame_partial_tick);
                    float sunDirX = (std::sin(sunAngle) > 0.0f) ? -1.0f : 1.0f;
                    Vec3 *vv = g_player->getViewVector(frame_partial_tick);
                    float d_raw = 0.0f, d_blend = 0.0f;
                    int sunrise_ok = 0;
                    float c0 = 0, c1 = 0, c2 = 0, c3 = 0;
                    if (vv) {
                        d_raw = (float)vv->x * sunDirX;
                        float d = d_raw;
                        if (d < 0.0f) d = 0.0f;
                        if (d > 0.0f) {
                            Dimension *dim = g_levels[0]->dimension;
                            if (dim) {
                                float *c = dim->getSunriseColor(td, frame_partial_tick);
                                if (c) {
                                    sunrise_ok = 1;
                                    c0 = c[0]; c1 = c[1]; c2 = c[2]; c3 = c[3];
                                    d *= c[3];
                                    d_blend = d;
                                    fr = fr * (1.0f - d) + c[0] * d;
                                    fg = fg * (1.0f - d) + c[1] * d;
                                    fb = fb * (1.0f - d) + c[2] * d;
                                }
                            }
                        }
                    }
                    // Every 30 frames, dump the blend inputs so we can
                    // see why the sunrise tint isn't showing visually.
                    {
                        static int s_fb = 0;
                        if ((s_fb++ % 30) == 0) {
                            MCLE_LOG("FOG_BLEND td=%.3f sunAngle=%.3f sunDirX=%.1f vv_x=%.3f d_raw=%.3f sunriseOk=%d sunrise=(%.2f,%.2f,%.2f,%.2f) d_blend=%.3f fog=(%.3f,%.3f,%.3f)",
                                     td, sunAngle, sunDirX,
                                     vv ? (float)vv->x : 0.0f, d_raw,
                                     sunrise_ok, c0, c1, c2, c3,
                                     d_blend, fr, fg, fb);
                        }
                    }
                } catch (...) {
                    MCLE_LOG("FOG_BLEND threw");
                }
            }
            const float renderDistance = 256.0f;  // viewDistance=0 in our build
            mcle_glbridge_set_fog_color(fr, fg, fb, 1.0f);
            mcle_glbridge_set_fog_start(renderDistance * 0.25f);
            mcle_glbridge_set_fog_end(renderDistance);
            mcle_glbridge_set_fog_enabled(1);
        }

        // Per-frame lightmap rebuild. Mirrors upstream
        // GameRenderer::updateLightTexture (GameRenderer.cpp:849-946):
        // 256 entries indexed by (skyLevel << 4) | blockLevel. Sky
        // component is dimmed by Level::getSkyDarken so terrain
        // darkens at night; block component is constant so torches
        // stay bright. Rain and thunder slightly brighten the block
        // contribution.
        //
        // brightnessRamp would normally come from dimension->brightnessRamp[16]
        // populated by Dimension::updateLightRamp (Dimension.cpp:28-36).
        // Compute it inline with the same formula so this works whether
        // or not the upstream Dimension ctor fully ran in our build.
        if (g_initState == kStateTicking && g_levels[0]) {
            try {
                Level *lv = g_levels[0];
                float skyDarken = lv->getSkyDarken(frame_partial_tick);
                float darken    = skyDarken * 0.95f + 0.05f;
                float rainLevel    = lv->getRainLevel(frame_partial_tick);
                float thunderLevel = lv->getThunderLevel(frame_partial_tick);
                float blr = rainLevel + thunderLevel;
                // Compute brightnessRamp[0..15] inline, parity with
                // upstream Dimension::updateLightRamp at ambientLight=0.
                float brightnessRamp[16];
                const float MAX_BRIGHTNESS = 15.0f;
                for (int i = 0; i <= 15; i++) {
                    float v = 1.0f - (float)i / MAX_BRIGHTNESS;
                    brightnessRamp[i] = (1.0f - v) / (v * 3.0f + 1.0f);
                }
                for (int i = 0; i < 256; i++) {
                    int skyLevel   = i >> 4;
                    int blockLevel = i & 0xF;
                    float sky   = brightnessRamp[skyLevel]   * darken;
                    float block = brightnessRamp[blockLevel] * (blr * 0.1f + 1.5f);
                    float r = sky * (skyDarken * 0.65f + 0.35f) + block;
                    float g = sky * (skyDarken * 0.65f + 0.35f) + block * ((block * 0.6f + 0.4f) * 0.6f + 0.4f);
                    float b = sky                                + block * ((block * block) * 0.6f + 0.4f);
                    if (r > 1.0f) r = 1.0f; if (r < 0.0f) r = 0.0f;
                    if (g > 1.0f) g = 1.0f; if (g < 0.0f) g = 0.0f;
                    if (b > 1.0f) b = 1.0f; if (b < 0.0f) b = 0.0f;
                    mcle_lightmap_set_entry(i, r, g, b);
                }
                mcle_lightmap_upload();
            } catch (...) {
                // Leave previous frame's lightmap in place on any throw,
                // safer than blanking the world.
            }
        }

        // Bind the terrain atlas before chunks render. Upstream's
        // GameRenderer::renderLevel does this; we don't run GameRenderer
        // so we call it directly here. Routes to our patched
        // Textures::bindTexture which loads terrain.png and binds the GL id.
        {
            Minecraft *mc = Minecraft::GetInstance();
            static int s_bindCount = 0;
            if (s_bindCount < 3) {
                MCLE_LOG("BIND_CKPT pre-render bindTexture(LOCATION_BLOCKS): mc=%p textures=%p",
                         (void*)mc, mc ? (void*)mc->textures : nullptr);
            }
            if (mc && mc->textures) {
                try {
                    mc->textures->bindTexture(&TextureAtlas::LOCATION_BLOCKS);
                } catch (...) {}
            }
            if (s_bindCount < 3) {
                MCLE_LOG("BIND_CKPT post-bind bound_tex_id=%u",
                         mcle_glbridge_get_bound_texture());
                s_bindCount++;
            }
        }

        {
            static int s_renderCalls = 0;
            if (s_renderCalls < 3) MCLE_LOG("WD_CKPT before render call=%d", s_renderCalls);
            // Pass partial tick so renderChunks interpolates the camera
            // translate(-player) between xOld and x. Smooths motion when
            // input ticks at 20Hz but rendering is at 60Hz.
            g_levelRenderer->render(g_player, /*layer*/0,
                                    /*alpha*/(double)frame_partial_tick,
                                    /*updateChunks*/false);
            if (s_renderCalls < 3) MCLE_LOG("WD_CKPT after render call=%d", s_renderCalls);
            s_renderCalls++;
        }

        // G3e-step5: re-enable renderSky/renderClouds with line-by-line
        // checkpoints inside Level::getSkyColor (LR_GSC tags). Last LR_GSC
        // line printed before crash pins the offending deref.

        // Diagnostic: dump modelview matrix + sun angle right before
        // renderSky. This is the matrix all sky/sun/moon/cloud passes
        // operate on. Expected ~ R_pitch * R_yaw * T(0,heightOff-1.62,0).
        {
            static int s_skyDiag = 0;
            if ((s_skyDiag++ % 60) == 0) {
                float mv[16] = {0};
                extern void mcle_glbridge_get_modelview(float *out16);
                mcle_glbridge_get_modelview(mv);
                MCLE_LOG("MV_CKPT log=%d row0=[%.3f %.3f %.3f %.3f] row3=[%.3f %.3f %.3f %.3f]",
                         s_skyDiag,
                         mv[0],  mv[1],  mv[2],  mv[3],
                         mv[12], mv[13], mv[14], mv[15]);
                if (g_levels[0]) {
                    try {
                        float td  = g_levels[0]->getTimeOfDay(frame_partial_tick);
                        float san = g_levels[0]->getSunAngle(frame_partial_tick);
                        // Compute the sun's centre eye-space position after
                        // the renderSky local rotations (R_-90Y * R_td*360X
                        // applied to (0, 100, 0)) plus the current camera
                        // modelview. This tells us exactly where the sun
                        // ends up on screen.
                        const float kPi = 3.14159265358979323846f;
                        float ang = td * 2.0f * kPi;
                        // After R_X(td*360) on (0,100,0): (0, 100*cos, 100*sin)
                        float sx = 0.0f;
                        float sy = 100.0f * std::cos(ang);
                        float sz = 100.0f * std::sin(ang);
                        // After R_Y(-90): (a,b,c)->(-c, b, a)
                        float wx = -sz, wy = sy, wz = sx;
                        // Apply camera modelview (column-major mv).
                        float ex = mv[0]*wx + mv[4]*wy + mv[8] *wz + mv[12];
                        float ey = mv[1]*wx + mv[5]*wy + mv[9] *wz + mv[13];
                        float ez = mv[2]*wx + mv[6]*wy + mv[10]*wz + mv[14];
                        // Crude NDC (no projection matrix applied; just check sign of z).
                        const char *zside = (ez < 0) ? "front" : "behind";
                        MCLE_LOG("SUN_ANGLE log=%d xRot=%.1f yRot=%.1f td=%.4f sunAngle=%.4f world=(%.1f,%.1f,%.1f) eye=(%.1f,%.1f,%.1f) zside=%s",
                                 s_skyDiag,
                                 g_player ? g_player->xRot : 0.0f,
                                 g_player ? g_player->yRot : 0.0f,
                                 td, san,
                                 wx, wy, wz,
                                 ex, ey, ez,
                                 zside);
                    } catch (...) {}
                }
            }
        }

        // Diagnostic for star rendering. getStarBrightness should be >0
        // at night so glCallList(starList) actually fires inside renderSky
        // (upstream LevelRenderer.cpp:1138-1142). If brightness is 0 at
        // what looks like night, the time-of-day or dimension check is
        // wrong. If brightness is >0 but no stars appear, the starList
        // capture didn't happen at LevelRenderer ctor time.
        {
            static int s_starDiag = 0;
            if ((s_starDiag++ % 60) == 0 && g_levels[0]) {
                try {
                    float sb = g_levels[0]->getStarBrightness(frame_partial_tick);
                    MCLE_LOG("STAR_CKPT log=%d starBrightness=%.4f",
                             s_starDiag, sb);
                } catch (...) {}
            }
        }
        // Upstream's GameRenderer::setupFog(-1, a) called right before
        // renderSky uses different start/end values than the chunk pass:
        // FOG_START=0, FOG_END=renderDistance*0.8 (GameRenderer.cpp:2209-
        // 2213). This pulls the entire sky dome toward the fog color
        // (warm tones near the sun at sunset) even close to the camera,
        // which is what produces the smooth blue-to-orange gradient at
        // the horizon. With only the chunk-pass values (start=64,
        // end=256) the dome stays nearly full sky color and never blends
        // to fog - that's the "blue layer covering the orange horizon"
        // visible at sunset. Swap start/end here, then restore the
        // chunk values right after renderSky for the later passes.
        {
            const float renderDistance = 256.0f;
            mcle_glbridge_set_fog_start(0.0f);
            mcle_glbridge_set_fog_end(renderDistance * 0.8f);
        }
        try { g_levelRenderer->renderSky(frame_partial_tick);    } catch (...) {}
        {
            const float renderDistance = 256.0f;
            mcle_glbridge_set_fog_start(renderDistance * 0.25f);
            mcle_glbridge_set_fog_end(renderDistance);
        }
        // Modelview at this point should match what entered renderSky.
        // Upstream renderClouds expects: rotations + eye-height translate,
        // no XZ player translate. Log every 60 frames so we can confirm
        // matrix state hasn't been corrupted by renderChunks/renderSky's
        // push/pops.
        {
            static int s_mvCloud = 0;
            if ((s_mvCloud++ % 60) == 0) {
                float mv[16] = {0};
                extern void mcle_glbridge_get_modelview(float *out16);
                mcle_glbridge_get_modelview(mv);
                MCLE_LOG("MV_CLOUD log=%d row0=[%.3f %.3f %.3f %.3f] row1=[%.3f %.3f %.3f %.3f] row2=[%.3f %.3f %.3f %.3f] row3=[%.3f %.3f %.3f %.3f]",
                         s_mvCloud,
                         mv[0],  mv[1],  mv[2],  mv[3],
                         mv[4],  mv[5],  mv[6],  mv[7],
                         mv[8],  mv[9],  mv[10], mv[11],
                         mv[12], mv[13], mv[14], mv[15]);
                if (g_player) {
                    MCLE_LOG("MV_CLOUD log=%d player_x=%.2f y=%.2f z=%.2f xRot=%.1f yRot=%.1f",
                             s_mvCloud,
                             (float)g_player->x, (float)g_player->y, (float)g_player->z,
                             g_player->xRot, g_player->yRot);
                }
            }
        }
        try { g_levelRenderer->renderClouds(frame_partial_tick); } catch (...) {}

        // HUD: parity with upstream GameRenderer::render which calls
        // Gui::render after the world. Hotbar, health/hunger/xp bars,
        // crosshair all happen inside this call. Push screen size into
        // the shim so ScreenSizeCalculator gets the right pixels.
        // g_minecraftShim is function-local in initImpl; reach the same
        // pointer via Minecraft::GetInstance() (set in initImpl).
        Minecraft *mcShim = Minecraft::GetInstance();
        // HUD rendering disabled: Gui::render's pre-RENDER_HUD overlays
        // (renderPumpkin / renderTp / renderVignette) kept firing on our
        // partially-initialised player state, painting full-screen blue/
        // pumpkin overlays. Real HUD lands via the Ruffle/SWF path - phases
        // 2-4 of project_hud_swf_plan.md - which renders HUD1080.swf on
        // top of the world. Until then, skip gui->render entirely.
        constexpr bool kEnableNativeGuiRender = false;
        if (kEnableNativeGuiRender && g_gui && mcShim) {
            int sw = 0, sh = 0;
            mcle_metal_current_size(&sw, &sh);
            mcShim->width       = sw;
            mcShim->height      = sh;
            // width_phys / height_phys are the physical-pixel dims (vs the
            // potentially-scaled width/height). GuiComponent::blit divides
            // by width_phys; without this it's 0 -> NaN vertex coords ->
            // HUD invisible. iOS retina has the framebuffer already at
            // physical size so width == width_phys.
            mcShim->width_phys  = sw;
            mcShim->height_phys = sh;
            try {
                g_gui->render(frame_partial_tick, /*mouseFree*/false,
                              /*xMouse*/0, /*yMouse*/0);
            } catch (const std::exception &e) {
                static int s_huderr = 0;
                if (s_huderr < 3) {
                    MCLE_LOG("Gui::render threw: %{public}s", e.what());
                    s_huderr++;
                }
            } catch (...) {
                static int s_huderr2 = 0;
                if (s_huderr2 < 3) {
                    MCLE_LOG("Gui::render threw unknown");
                    s_huderr2++;
                }
            }
        }

        mcle_glbridge_replay_all_lists();

        // G5: every ~120 frames, log the call_list stats so we know whether
        // the chunk display lists are actually being replayed.
        static int s_statsFrame = 0;
        if ((s_statsFrame++ % 120) == 0) {
            unsigned long hits = 0, misses = 0, count = 0;
            int firstMiss = -1, firstHit = -1;
            mcle_glbridge_call_list_stats(&hits, &misses, &firstMiss, &firstHit, &count);
            unsigned long hitsLow = 0, hitsHigh = 0;
            int lastHit = -1;
            mcle_glbridge_call_list_stats_ext(&hitsLow, &hitsHigh, &lastHit);
            MCLE_LOG("CL_CKPT call_list: hits=%lu (low=%lu high=%lu) misses=%lu firstHit=%d lastHit=%d firstMiss=%d totalLists=%lu",
                     hits, hitsLow, hitsHigh, misses, firstHit, lastHit, firstMiss, count);

            unsigned long fmts[16] = {0};
            mcle_glbridge_fmt_stats(fmts, 16);
            MCLE_LOG("FMT_CKPT fmt counts: 1=%lu 2=%lu 3=%lu 4=%lu 5=%lu other=%lu+%lu+%lu+%lu+%lu+%lu+%lu",
                     fmts[1], fmts[2], fmts[3], fmts[4], fmts[5],
                     fmts[0], fmts[6], fmts[7], fmts[8], fmts[9], fmts[10], fmts[11]);
        }
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

// Route through upstream Level::getSkyColor for full parity with LCE
// Win64. That function returns biome-tinted RGB modulated by time-of-day
// brightness, rain darkening, thunder darkening, and sky-flash. The
// older stub used a hardcoded plains-biome color (no biome tint) - which
// is why dawn/dusk sky colors looked wrong vs LCE.
extern "C" void mcle_world_get_sky_color(float *r, float *g, float *b) {
    // Fallback values: plains biome at full brightness. Used pre-world-tick
    // (menu / loading), or if upstream call throws.
    float fr = 120.0f / 255.0f;
    float fg = 167.0f / 255.0f;
    float fb = 255.0f / 255.0f;

    if (g_initState == kStateTicking && g_levels[0] && g_player) {
        try {
            Vec3 *sky = g_levels[0]->getSkyColor(g_player, /*alpha*/1.0f);
            if (sky) {
                fr = (float)sky->x;
                fg = (float)sky->y;
                fb = (float)sky->z;
            }
        } catch (...) {
            // Keep fallback values on any throw - matches the old stub's
            // behavior so we don't regress visible state.
        }
    }

    if (r) *r = fr;
    if (g) *g = fg;
    if (b) *b = fb;
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
