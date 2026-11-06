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
#include <fcntl.h>
#include <unistd.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <os/proc.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <typeinfo>
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
extern "C" unsigned long long mcle_glbridge_list_bytes(void);
extern "C" unsigned long long mcle_glbridge_total_draws(void);
extern "C" unsigned int mcle_glbridge_get_bound_texture(void);
extern "C" unsigned int mcle_glbridge_load_or_get_png_path(const char *path);
extern "C" void mcle_glbridge_call_list_stats(unsigned long *hits, unsigned long *misses,
                                                int *first_miss, int *first_hit, unsigned long *list_count);
extern "C" void mcle_glbridge_call_list_stats_ext(unsigned long *hits_low,
                                                    unsigned long *hits_high,
                                                    int *last_hit);
extern "C" void mcle_glbridge_fmt_stats(unsigned long *out, int max_count);
extern "C" void mcle_glbridge_skip_autoreplay(int id);
extern "C" void mcle_glbridge_set_autoreplay_range(int first, int count);
extern "C" void mcle_metal_clear_depth(void);

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
#include "../../../upstream/Minecraft.World/BiomeSource.h"
#include "../../../upstream/Minecraft.World/MapItemSavedData.h"
#include "../../../upstream/Minecraft.World/MaterialColor.h"
#include "../../../upstream/Minecraft.World/Material.h"
#include "../../../upstream/Minecraft.World/Recipes.h"
#include "../../../upstream/Minecraft.World/Tile.h"
#include "../../../upstream/Minecraft.World/GrassTile.h"
#include "../../../upstream/Minecraft.World/PortalTile.h"
#include "../../../upstream/Minecraft.World/Facing.h"
#include "../../../upstream/Minecraft.World/Icon.h"
#include "../../../upstream/Minecraft.World/Item.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"
#include "../../../upstream/Minecraft.World/Entity.h"
#include "../../../upstream/Minecraft.World/Biome.h"
#include "../../../upstream/Minecraft.World/GenericStats.h"
#include "../../../upstream/Minecraft.World/CommonStats.h"
#include "../../../upstream/Minecraft.World/Stats.h"
#include "../../../upstream/Minecraft.World/Packet.h"
#include "../../../upstream/Minecraft.World/ExplodePacket.h"
#include "../../../upstream/Minecraft.World/Explosion.h"
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
#include "../../../upstream/Minecraft.Client/ParticleEngine.h"
#include "../../../upstream/Minecraft.Client/Camera.h"
#include "../../../upstream/Minecraft.World/EnchantmentHelper.h"
#include "../../../upstream/Minecraft.Client/CritParticle.h"
#include "../../../upstream/Minecraft.Client/TakeAnimationParticle.h"
#include "../../../upstream/Minecraft.Client/FireworksParticles.h"
#include "../../../upstream/Minecraft.World/FireworksItem.h"
#include "../../../upstream/Minecraft.World/EntityEvent.h"
#include "../../../upstream/Minecraft.World/SynchedEntityData.h"
#include "../../../upstream/Minecraft.World/ItemInstance.h"
#include "../../../upstream/Minecraft.World/CompressedTileStorage.h"
#include "../../../upstream/Minecraft.World/SparseDataStorage.h"
#include "../../../upstream/Minecraft.World/McRegionChunkStorage.h"
#include "../../../upstream/Minecraft.World/Villager.h"
#include "../../../upstream/Minecraft.World/BeaconTileEntity.h"
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
#include "../../../upstream/Minecraft.Client/ScreenSizeCalculator.h"
#include "../../../upstream/Minecraft.Client/Gui.h"
#include "../../../upstream/Minecraft.Client/GameRenderer.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLevel.h"
#include "../../../upstream/Minecraft.Client/PlayerChunkMap.h"
#include "../../../upstream/Minecraft.Client/MultiPlayerLocalPlayer.h"
#include "../../../upstream/Minecraft.Client/Textures.h"
#include "../../../upstream/Minecraft.Client/TexturePackRepository.h"
#include "../../../upstream/Minecraft.Client/TextureManager.h"
#include "../../../upstream/Minecraft.Client/TextureAtlas.h"
#include "../../../upstream/Minecraft.Client/EntityRenderDispatcher.h"
#include "../../../upstream/Minecraft.Client/EntityRenderer.h"
#include "../../../upstream/Minecraft.Client/TileEntityRenderDispatcher.h"
#include "../../../upstream/Minecraft.Client/User.h"
#include "../../../upstream/Minecraft.Client/Font.h"
#include "../../../upstream/Minecraft.Client/ResourceLocation.h"
#include "../../../upstream/Minecraft.Client/Lighting.h"
#include "../../../upstream/Minecraft.Client/FrustumCuller.h"
#include "../../../upstream/Minecraft.Client/Frustum.h"
#include "../../../upstream/Minecraft.Client/Common/UI/UIFontData.h"
#include "../../../upstream/Minecraft.Client/Common/Colours/ColourTable.h"
#include "../../../upstream/Minecraft.Client/BossMobGuiInfo.h"
#include "../../../upstream/Minecraft.World/JavaMath.h"
#include "../../../upstream/Minecraft.World/MobEffect.h"
#include "../../../upstream/Minecraft.World/MobEffectInstance.h"
#include "../../../upstream/Minecraft.World/FoodConstants.h"
#include "../../../upstream/Minecraft.World/SharedConstants.h"
#include "../../../upstream/Minecraft.World/Dimension.h"
#include "../../../upstream/Minecraft.World/Mth.h"
#include "../../../upstream/Minecraft.World/Random.h"
#include "../../../upstream/Minecraft.World/ChunkSource.h"
#include "../../../upstream/Minecraft.World/HitResult.h"
#include "../../../upstream/Minecraft.Client/ItemInHandRenderer.h"
#include "MCLEPlayerInput.h"
#include "MCLESound.h"
#include "MCLEStartGame.h"
#include "MCLEHud.h"
#include "../../../upstream/Minecraft.Client/EntityTracker.h"
#include <dispatch/dispatch.h>
#include <pthread.h>
#include "MCLETooltips.h"
#include "MCLEWeather.h"
#include "MCLETexturePacks.h"
#include "MCLETutorial.h"
#include "MCLEPortal.h"
#include "../../../upstream/Minecraft.World/PlayerIO.h"
#include "../../../upstream/Minecraft.Client/PlayerConnection.h"
#include "../../../upstream/Minecraft.Client/HumanoidModel.h"

extern "C" unsigned char mcle_settings_get(int setting); // MCLE_iOS_Settings.mm

// ActionGameSettings(eGameSetting_RenderDistance): the chunk count picked in
// the graphics menu becomes Options::viewDistance, 3 minus its level there
// (UIScene_SettingsGraphicsMenu::DistanceToLevel)
static int mcle_view_distance_setting() {
    static const int kTable[6] = {2, 4, 8, 16, 32, 64};
    const int dist = mcle_settings_get(2 /*RenderDistance*/);
    int level = 3;
    for (int i = 0; i < 6; ++i) {
        if (kTable[i] == dist) level = i;
    }
    return 3 - level;
}
void mcle_creative_menu_static_ctor();                    // MCLECreativeMenu.cpp

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
    kStateExiting   = 3,   // leaving the world, ticks and frames stay out
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
// GameRenderer's itemInHandRenderer. Ticked on the sim thread, drawn on the
// render thread, so the two take turns.
ItemInHandRenderer *g_itemInHand = nullptr;
std::mutex g_itemInHandMutex;
// LevelRenderer::destroyingBlocks, changed on the sim thread and drawn by
// renderDestroyAnimation on the render thread
std::mutex g_destroyingBlocksMutex;
// ParticleEngine: particles get added and ticked on the sim thread and
// drawn on the render thread. Recursive since a level event can add more
// than one.
std::recursive_mutex g_particleMutex;
// Single player pause (UIScene_PauseMenu): the server stops ticking
std::atomic<bool> g_gamePaused{false};
// Death menu: whether the player is dead, and its Respawn press waiting for
// the next tick. The render driver holds g_frameMutex for a frame, the
// respawn swaps the player under it.
std::atomic<bool> g_playerDead{false};
std::atomic<bool> g_respawnRequested{false};
std::mutex g_frameMutex;
// Held by the sim thread for each tick, so leaving the world can wait for
// the one in flight
std::mutex g_simTickMutex;
// Held while the world's objects are saved or deleted, so a background save
// and leaving the world don't overlap
std::mutex g_worldLifeMutex;
// GameRenderer's blr and darkenWorldAmount: ticked on the sim thread, read
// when the render thread builds the light texture
std::atomic<float> g_lightBlr{0.0f};
std::atomic<float> g_darkenWorldAmount{0.0f};
std::atomic<float> g_darkenWorldAmountO{0.0f};
// GameRenderer's fogBr / fogBrO: the fog follows the light where the
// player stands
std::atomic<float> g_fogBr{0.0f};
std::atomic<float> g_fogBrO{0.0f};
std::shared_ptr<ServerPlayer> g_player;
ServerPlayerGameMode         *g_playerGameMode = nullptr;
LevelRenderer                *g_levelRenderer  = nullptr;
std::wstring                  g_levelName;

// The LevelRenderer listens to the server level here, not a client one.
// ServerLevelListener never sends a player its own crack progress, its
// client draws its own guess (MultiPlayerGameMode), so the server's calls
// for the local player stop here and mcle_client_destroy_progress makes
// the client ones.
class ClientLevelListener : public LevelListener {
public:
    explicit ClientLevelListener(LevelListener *renderer) : r(renderer) {}
    void tileChanged(int x, int y, int z) override { r->tileChanged(x, y, z); }
    void tileLightChanged(int x, int y, int z) override { r->tileLightChanged(x, y, z); }
    void setTilesDirty(int x0, int y0, int z0, int x1, int y1, int z1, Level *level) override {
        r->setTilesDirty(x0, y0, z0, x1, y1, z1, level);
    }
    void allChanged() override { r->allChanged(); }
    void playSound(int iSound, double x, double y, double z, float volume, float pitch, float clip) override {
        r->playSound(iSound, x, y, z, volume, pitch, clip);
    }
    void playSoundExceptPlayer(shared_ptr<Player> player, int iSound, double x, double y, double z, float volume,
                               float pitch, float clip) override {
        r->playSoundExceptPlayer(player, iSound, x, y, z, volume, pitch, clip);
    }
    void addParticle(ePARTICLE_TYPE name, double x, double y, double z, double xa, double ya, double za) override {
        std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
        r->addParticle(name, x, y, z, xa, ya, za);
    }
    void entityAdded(shared_ptr<Entity> entity) override { r->entityAdded(entity); }
    void entityRemoved(shared_ptr<Entity> entity) override { r->entityRemoved(entity); }
    void playerRemoved(shared_ptr<Entity> entity) override { r->playerRemoved(entity); }
    void skyColorChanged() override { r->skyColorChanged(); }
    void playStreamingMusic(const wstring &name, int x, int y, int z) override { r->playStreamingMusic(name, x, y, z); }
    void globalLevelEvent(int type, int x, int y, int z, int data) override { r->globalLevelEvent(type, x, y, z, data); }
    void levelEvent(shared_ptr<Player> source, int type, int x, int y, int z, int data) override {
        std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
        r->levelEvent(source, type, x, y, z, data);
    }
    void destroyTileProgress(int id, int x, int y, int z, int progress) override {
        if (g_player && id == g_player->entityId) return;
        std::lock_guard<std::mutex> lock(g_destroyingBlocksMutex);
        r->destroyTileProgress(id, x, y, z, progress);
    }

private:
    LevelListener *r;
};
// The one on the client's level, dropped when leaving the world
ClientLevelListener *g_clientLevelListener = nullptr;
// The level the client is in, the one the renderer, the particles and that
// listener follow. It changes with the player's dimension, the way
// ClientConnection::handleRespawn sets its level, under g_frameMutex.
ServerLevel *g_clientLevel = nullptr;
// From the start of a level change until the client has the new level the
// world isn't drawn, ConnectingProgress is over it
std::atomic<bool> g_levelChanging{false};

// Minecraft::setLevel's levelRenderer->setLevel and particleEngine->setLevel,
// for the level the local player is in. The renderer hears the level through
// ClientLevelListener instead of as its own listener. Once the world is drawn
// this runs with g_frameMutex held.
//
// G3e: the live ServerLevel is mc->level so upstream renderSky can read
// getSkyColor / dimension / time-of-day. ServerLevel and MultiPlayerLevel
// are siblings under Level - the cast keeps virtual dispatch routed through
// the actual ServerLevel vtable.
//
// G5: LevelRenderer::setLevel(0, lvl) sets level[0] and calls allChanged(0)
// to allocate the chunk mesh array. Without this, chunks[0].length stays at
// 0 and renderChunks dispatches no geometry.
void mcle_client_set_level(ServerLevel *level) {
    Minecraft *mc = Minecraft::GetInstance();
    MultiPlayerLevel *lvl = reinterpret_cast<MultiPlayerLevel *>(level);
    if (g_clientLevel && g_clientLevelListener) g_clientLevel->removeListener(g_clientLevelListener);
    g_clientLevel = level;
    mc->level = lvl;
    {
        std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
        if (mc->particleEngine) mc->particleEngine->setLevel(lvl);
    }
    MCLE_LOG("client level: dimension %d at %p", level->dimension->id, (void*)lvl);
    try {
        g_levelRenderer->setLevel(0, lvl);
        lvl->removeListener(g_levelRenderer);
        if (!g_clientLevelListener) g_clientLevelListener = new ClientLevelListener(g_levelRenderer);
        lvl->addListener(g_clientLevelListener);
        MCLE_LOG("client level: setLevel returned, chunks[0].length=%u",
                 (unsigned)g_levelRenderer->chunks[0].length);
    } catch (const std::exception &e) {
        MCLE_LOG("client level: setLevel threw: %{public}s", e.what());
    } catch (...) {
        MCLE_LOG("client level: setLevel threw unknown");
    }

    // G5-step16: re-center the render grid around the player.
    // setLevel -> allChanged calls resortChunks but only if mc->
    // cameraTargetPlayer was wired BEFORE setLevel ran. Calling
    // it explicitly here with current player coords guarantees
    // the grid covers the loaded chunks.
    try {
        int px = (int)floorf(g_player->x);
        int py = (int)floorf(g_player->y);
        int pz = (int)floorf(g_player->z);
        g_levelRenderer->resortChunks(px, py, pz);
        MCLE_LOG("client level: resortChunks(%d,%d,%d) done", px, py, pz);
    } catch (...) {
        MCLE_LOG("client level: resortChunks threw");
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
        MCLE_LOG("client level: force-dirty %d chunks", dirtied);
        g_levelRenderer->nonStackDirtyChunksAdded();
        MCLE_LOG("client level: nonStackDirtyChunksAdded done");
    } catch (...) {
        MCLE_LOG("client level: force-dirty / nonStackDirtyChunksAdded threw");
    }
}

// MultiPlayerLevel::animateTick then animateTickDoWork, for the one local
// player: random tiles in the 3x3x3 block of 8x8x8 chunks round the player
// get Tile::animateTick (torch smoke, dripping water, lava pops, portal
// sounds...). The client level has its own Random, so the server level's
// isn't used for picking the tiles.
void animateTick(Level *level, int xt, int yt, int zt) {
    static Random s_random;
    std::vector<unsigned int> chunksToAnimate;
    int cx = xt >> 3;
    int cy = yt >> 3;
    int cz = zt >> 3;

    for (int xx = -1; xx <= 1; xx++)
        for (int yy = -1; yy <= 1; yy++)
            for (int zz = -1; zz <= 1; zz++) {
                if ((cy + yy) < 0) continue;
                if ((cy + yy) > 15) continue;
                if ((cx + xx) >= LEVEL_MAX_WIDTH) continue;
                if ((cx + xx) < -LEVEL_MAX_WIDTH) continue;
                if ((cz + zz) >= LEVEL_MAX_WIDTH) continue;
                if ((cz + zz) < -LEVEL_MAX_WIDTH) continue;
                chunksToAnimate.push_back((((cx + xx) & 0xff) << 16) | (((cy + yy) & 0xff) << 8) | (((cz + zz) & 0xff)));
            }

    const int ticksPerChunk = 16;
    Minecraft::GetInstance()->animateTickLevel = level;
    Random *animateRandom = new Random();

    for (int i = 0; i < ticksPerChunk; i++) {
        for (unsigned int packedBits : chunksToAnimate) {
            int packed = static_cast<int>(packedBits);
            int px = ((packed << 8) >> 24) << 3;
            int py = ((packed << 16) >> 24) << 3;
            int pz = ((packed << 24) >> 24) << 3;
            int x = px + s_random.nextInt(8);
            int y = py + s_random.nextInt(8);
            int z = pz + s_random.nextInt(8);
            int t = level->getTile(x, y, z);
            if (s_random.nextInt(8) > y && t == 0 && level->dimension->hasBedrockFog()) {
                level->addParticle(eParticleType_depthsuspend, x + s_random.nextFloat(), y + s_random.nextFloat(),
                                   z + s_random.nextFloat(), 0, 0, 0);
            } else if (t > 0) {
                Tile::tiles[t]->animateTick(level, x, y, z, animateRandom);
            }
        }
    }

    Minecraft::GetInstance()->animateTickLevel = nullptr;
    delete animateRandom;
}

// std::atomic so the main-thread tick reliably reads progress from the
// background init thread. On arm64 plain int loads/stores are atomic
// but the compiler can still reorder them with surrounding writes
// (g_levels[0] = ...) and starve the tick of a happens-before edge.
std::atomic<int>              g_initState{ kStateUnstarted };
// Wall-clock ns of the last simulation tick (set by mcle_game_tick when the
// 20Hz throttle gate fires). Render thread reads this to compute the
// partial-tick alpha for camera-position interpolation.
std::atomic<uint64_t>         g_lastSimTickNs{ 0 };
// When the next 20Hz tick is due, so the sim thread can sleep until then
std::atomic<uint64_t>         g_nextSimTickNs{ 0 };
uint64_t                      g_pendingSimTickNs = 0;
// Gui instance for HUD rendering (hotbar, health bars, crosshair).
// Constructed in mcle_game_init after textures->stitch (parity with
// upstream Minecraft::init). Render thread calls g_gui->render() each frame.
Gui                          *g_gui = nullptr;
uint64_t                      g_tickCount    = 0;
// Init runs on a background pthread so the main-thread tick stays free
// to render the placeholder SWF + handle input while save loading is
// in progress. Set once on first tick, never reset.
std::atomic<bool>             g_initStarted{ false };
// Phase 3a: dedicated sim thread. Once init succeeds we spawn this
// thread and it runs the body of mcle_game_tick in a loop. The
// main thread's call becomes a no-op so chunk-rebuild WaitForAll
// barriers and entity ticks never block the render frame.
std::atomic<bool>             g_simThreadStarted{ false };

// Save folder picked in LoadMenu. Written before the init thread starts.
std::wstring                  g_pendingLevelId;
// CreateWorldMenu's NetworkGameInitData for a new level, no save data
struct NewWorldParams {
    bool active = false;
    int64_t seed = 0;
    bool findSeed = false;
    int xzSize = LEVEL_MAX_WIDTH;
    int hellScale = HELL_LEVEL_MAX_SCALE;
};
NewWorldParams                g_newWorld;
// The level being loaded was made new, and findSeed is looking for its
// seed, for the loading screen
std::atomic<bool>             g_loadIsNew{ false };
std::atomic<bool>             g_loadFindingSeed{ false };
// Loading screen state for the view controller: 0 initialising server,
// 1 loading spawn area, 2 finalizing. g_renderReady flips once the
// renderer, textures and HUD exist, so the menu stays up until then.
std::atomic<int>              g_loadStage{ 0 };
std::atomic<int>              g_loadPercent{ 0 };
std::atomic<bool>             g_renderReady{ false };
pthread_t                     g_simThread{};
// Auto-save flag - file-scope so the detached save thread can clear it.
std::atomic<bool>             g_autoSaveInFlight{ false };

// The thread storage ServerThreadProc makes for the server thread, for a
// thread here that saves or leaves the world, and what it lets go of when
// the thread ends
static void mcle_world_thread_begin() {
    Tile::CreateNewThreadStorage();
    AABB::CreateNewThreadStorage();
    Vec3::CreateNewThreadStorage();
    IntCache::CreateNewThreadStorage();
    Compression::CreateNewThreadStorage();
    OldChunkStorage::CreateNewThreadStorage();
    Level::enableLightingCache();
}

static void mcle_world_thread_end() {
    Tile::ReleaseThreadStorage();
    AABB::ReleaseThreadStorage();
    Vec3::ReleaseThreadStorage();
    IntCache::ReleaseThreadStorage();
    Compression::ReleaseThreadStorage();
    OldChunkStorage::ReleaseThreadStorage();
    Level::destroyLightingCache();
}

// MinecraftServer's SaveGame action, or stopServer's save when leaving the
// world: PlayerList::saveAll, the levels in reverse so the nether's level
// data doesn't overwrite the overworld's, the game rules, then the save
// file goes to disc. Leaving also closes the level storage after the last
// level (saveAllChunks) and unloads the rules before the flush.
static void mcle_save_world(bool exiting) {
    const auto start = std::chrono::steady_clock::now();
    // PlayerList::saveAll for the one player
    if (g_levelStorage) {
        PlayerIO *playerIo = g_levelStorage->getPlayerIO();
        try {
            playerIo->saveAllCachedData();
            if (g_player) playerIo->save(g_player);
            playerIo->clearOldPlayerFiles();
            playerIo->saveMapIdLookup();
        } catch (...) {
            MCLE_LOG("BG_SAVE: player save threw");
        }
    }
    for (int i = 2; i >= 0; --i) {
        if (!g_levels[i]) continue;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            g_levels[i]->save(true, nullptr);
            if (exiting && i == 0) g_levels[i]->closeLevelStorage();
        } catch (...) {
            MCLE_LOG("BG_SAVE: level[%d] threw", i);
        }
        const auto dms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
        MCLE_LOG("BG_SAVE: level[%d] done took=%lldms", i, (long long)dms);
    }
    if (g_levels[0]) {
        // MinecraftServer::saveGameRules
        byteArray ba;
        ba.data = nullptr;
        app.m_gameRules.saveGameRules(&ba.data, &ba.length);
        if (ba.data != nullptr) {
            ConsoleSaveFile *csf = g_levels[0]->getLevelStorage()->getSaveFile();
            FileEntry *fe = csf->createFile(ConsoleSavePath(GAME_RULE_SAVENAME));
            csf->setFilePointer(fe, 0, nullptr, FILE_BEGIN);
            DWORD length;
            csf->writeFile(fe, ba.data, ba.length, &length);
            delete[] ba.data;
            csf->closeHandle(fe);
        }
        if (exiting) app.m_gameRules.unloadCurrentGameRules();
        try { g_levels[0]->saveToDisc(nullptr, false); }
        catch (...) { MCLE_LOG("BG_SAVE: saveToDisc threw"); }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count();
    MCLE_LOG("BG_SAVE: done took=%lldms", (long long)ms);
}

static void *mcle_save_now_thread(void *) {
    mcle_world_thread_begin();
    {
        // Not while the world is being torn down
        std::lock_guard<std::mutex> life(g_worldLifeMutex);
        if (g_initState == kStateTicking) {
            MCLE_LOG("BG_SAVE: starting");
            mcle_save_world(false);
        } else {
            MCLE_LOG("BG_SAVE: no world to save");
        }
    }
    mcle_world_thread_end();
    g_autoSaveInFlight.store(false, std::memory_order_release);
    return nullptr;
}

// Detached variant - fire-and-forget, used by paths that cannot block
// (eg the periodic auto-save in the sim loop, which historically
// deadlocked when run inline).
extern "C" void mcle_save_now(void) {
    if (g_autoSaveInFlight.load(std::memory_order_acquire)) return;
    bool expected = false;
    if (!g_autoSaveInFlight.compare_exchange_strong(expected, true)) return;

    pthread_t saveThread;
    int rc = pthread_create(&saveThread, nullptr, &mcle_save_now_thread, nullptr);
    if (rc == 0) {
        pthread_detach(saveThread);
    } else {
        g_autoSaveInFlight.store(false, std::memory_order_release);
        MCLE_LOG("BG_SAVE: pthread_create failed rc=%d", rc);
    }
}

// Blocking variant - applicationDidEnterBackground calls this so it
// can hold the iOS background task open until the save actually
// finishes. Without this, the detached pthread above raced against
// iOS suspension and never logged a "done" line.
extern "C" void mcle_save_now_sync(void) {
    // A save already going is the one this waits for, so the app isn't
    // suspended halfway through it
    bool expected = false;
    if (!g_autoSaveInFlight.compare_exchange_strong(expected, true)) {
        MCLE_LOG("BG_SAVE: sync requested, waiting for the save in flight");
        while (g_autoSaveInFlight.load(std::memory_order_acquire)) usleep(10 * 1000);
        return;
    }

    pthread_t saveThread;
    int rc = pthread_create(&saveThread, nullptr, &mcle_save_now_thread, nullptr);
    if (rc != 0) {
        g_autoSaveInFlight.store(false, std::memory_order_release);
        MCLE_LOG("BG_SAVE: sync pthread_create failed rc=%d", rc);
        return;
    }
    pthread_join(saveThread, nullptr);
}

// Leak hunt: atomic counters bracketed around ctor/dtor of suspect
// upstream classes via patch scripts. If a counter grows unbounded
// each chunk rebuild, that class is leaking. Tags:
//   0 = Region
//   1 = TileRenderer
namespace {
constexpr int kMaxLeakTags = 4;
std::atomic<long> g_leak_count[kMaxLeakTags]{};
std::atomic<long> g_leak_total[kMaxLeakTags]{};
}

extern "C" void mcle_alloc_inc(int tag) {
    if (tag < 0 || tag >= kMaxLeakTags) return;
    g_leak_count[tag].fetch_add(1, std::memory_order_relaxed);
    g_leak_total[tag].fetch_add(1, std::memory_order_relaxed);
}
extern "C" void mcle_alloc_dec(int tag) {
    if (tag < 0 || tag >= kMaxLeakTags) return;
    g_leak_count[tag].fetch_sub(1, std::memory_order_relaxed);
}
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

// Reads a save's .ms bundle into g_saveFile, for initImpl
bool mcle_open_save_bundle(const std::wstring &saveFolder) {
    // Step 2.5: locate the .ms bundle inside the save folder. Win64 LCE
    // names it after the world ("New World.ms" by default), not a fixed
    // filename - so list the dir and pick the first .ms entry.
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
        return false;
    }
    File saveDataFile(saveDataPath);
    int64_t saveSize = saveDataFile.length();
    if (saveSize <= 0) {
        MCLE_LOG("mcle_game_init: .ms missing or empty (%lld bytes)",
                 (long long)saveSize);
        g_initState = kStateFailed;
        return false;
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
        delete[] saveBytes.data;
        g_initState = kStateFailed;
        return false;
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
        delete[] saveBytes.data;
        g_initState = kStateFailed;
        return false;
    }
    // MinecraftServer::main deletes the passed in save data once it's loaded,
    // the save file has its own copy
    delete[] saveBytes.data;
    saveBytes.data = nullptr;
    if (!g_saveFile) {
        MCLE_LOG("mcle_game_init: ConsoleSaveFileOriginal ctor returned null");
        g_initState = kStateFailed;
        return false;
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
    return true;
}

// The Minecraft instance, a zero filled buffer standing in for upstream's
// object (the class has no virtual methods). It lives for the whole process
// like Minecraft itself, the fields the port uses filled in as they're made.
static Minecraft *mcle_minecraft_shim() {
    alignas(Minecraft) static char s_minecraftShimBuf[sizeof(Minecraft)] = {0};
    return reinterpret_cast<Minecraft *>(s_minecraftShimBuf);
}

// Minecraft::main runs these once per process, not per world, before the
// menus. Whichever thread gets here first runs them, the other waits.
void mcle_world_static_ctors() {
    static std::once_flag s_once;
    std::call_once(s_once, [] {
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
        // Rest of MinecraftWorld_RunStaticCtors. McRegionChunkStorage starts the
        // save threads that drain s_chunkDataQueue; without them every saved
        // chunk's buffers pile up forever.
        MCLE_STATIC("CompressedTileStorage::staticCtor", CompressedTileStorage::staticCtor());
        MCLE_STATIC("SparseDataStorage::staticCtor", SparseDataStorage::staticCtor());
        MCLE_STATIC("McRegionChunkStorage::staticCtor", McRegionChunkStorage::staticCtor());
        MCLE_STATIC("Villager::staticCtor",        Villager::staticCtor());
        MCLE_STATIC("BeaconTileEntity::staticCtor", BeaconTileEntity::staticCtor());

        // Minecraft::main runs these right after the world ones. ColourTable's
        // fills the name map colours.col is read through.
        MCLE_STATIC("EntityRenderDispatcher::staticCtor", EntityRenderDispatcher::staticCtor());
        MCLE_STATIC("TileEntityRenderDispatcher::staticCtor", TileEntityRenderDispatcher::staticCtor());
        MCLE_STATIC("User::staticCtor",            User::staticCtor());
        MCLE_STATIC("Tutorial::staticCtor",        Tutorial::staticCtor());
        MCLE_STATIC("ColourTable::staticCtor",     ColourTable::staticCtor());
        // Common for all platforms, the creative menu's item table
        MCLE_STATIC("IUIScene_CreativeMenu::staticCtor", mcle_creative_menu_static_ctor());

        #undef MCLE_STATIC
    });
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

    mcle_world_static_ctors();

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
    // subdirectory there is a candidate levelId. The one picked in
    // LoadMenu wins; the first folder is only a fallback.
    std::wstring levelId = g_pendingLevelId;
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

    if (g_newWorld.active) {
        // MinecraftServer::loadLevel with no save data: a new, empty save file
        g_saveFile = new ConsoleSaveFileOriginal(L"");
        g_saveFile->ConvertToLocalPlatform();
    } else if (!mcle_open_save_bundle(savesDir + L"/" + levelId)) {
        return;
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
    // A new level has no level.dat yet, the level makes its data from the settings
    if (!levelData && !g_newWorld.active) {
        MCLE_LOG("mcle_game_init: prepareLevel returned null");
        g_initState = kStateFailed;
        return;
    }
    // loadLevel's name is the world name for a new level
    g_levelName = levelData ? levelData->getLevelName() : levelId;
    MCLE_LOG("mcle_game_init: prepareLevel succeeded, name = %{public}s",
             narrow(g_levelName).c_str());

    // Step 5: build LevelSettings from the metadata.
    MCLE_LOG("mcle_game_init: building LevelSettings...");
    LevelSettings *settings = nullptr;
    try {
        if (g_newWorld.active) {
            // MinecraftServer::initServer picks the level type and finds a
            // seed if there wasn't one, then loadLevel makes the settings
            bool gameRuleUseFlatWorld = false;
            if (app.getLevelGenerationOptions() != nullptr) {
                gameRuleUseFlatWorld = app.getLevelGenerationOptions()->getuseFlatWorld();
            }
            LevelType *pLevelType = LevelType::getLevelType(
                gameRuleUseFlatWorld || app.GetGameHostOption(eGameHostOption_LevelType) > 0 ? L"flat" : L"default");
            if (pLevelType == nullptr) pLevelType = LevelType::lvl_normal;
            if (g_newWorld.findSeed) {
                g_loadFindingSeed = true;
                g_newWorld.seed = BiomeSource::findSeed(pLevelType);
                g_loadFindingSeed = false;
            }
            GameType *gameType = LevelSettings::validateGameType(app.GetGameHostOption(eGameHostOption_GameType));
            // isHardcore() is false
            settings = new LevelSettings(g_newWorld.seed, gameType,
                                         app.GetGameHostOption(eGameHostOption_Structures) > 0, false, true,
                                         pLevelType, g_newWorld.xzSize, g_newWorld.hellScale);
            if (app.GetGameHostOption(eGameHostOption_BonusChest)) settings->enableStartingBonusItems();
            MCLE_LOG("mcle_game_init: new level, seed %lld", (long long)g_newWorld.seed);
        } else {
            settings = new LevelSettings(levelData);
        }
        MCLE_LOG("mcle_game_init: LevelSettings at %p", (void*)settings);
    } catch (const std::exception &e) {
        MCLE_LOG("mcle_game_init: LevelSettings ctor threw: %{public}s", e.what());
        g_initState = kStateFailed;
        return;
    }
    // The settings have copied what they need. The level reads its own
    // level data when it's made.
    delete levelData;
    levelData = nullptr;

    // Step 5.5: construct a real MinecraftServer instance. Parity with
    // MinecraftServer::main:2251 (`server = new MinecraftServer()`).
    // The ctor sets up command dispatcher + dispenser bootstrap +
    // pause-event - all things ServerLevel ctor expects to call back
    // into.
    MCLE_LOG("mcle_game_init: constructing MinecraftServer...");
    try {
        g_server = new MinecraftServer();
        // MinecraftServer::main keeps it for getInstance
        MinecraftServer::server = g_server;
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

    // The rest of initServer's settings. server.properties only changes them
    // on a dedicated server. Unset, isAnimals() and isNpcsEnabled() were
    // false, and ServerLevel::tick(entity) removes every animal, squid and
    // villager when they're off.
    g_server->setAnimals(true);
    g_server->setNpcsEnabled(true);
    g_server->setPvpAllowed(app.GetGameHostOption(eGameHostOption_PvP) > 0);
    g_server->setFlightAllowed(true);

    // Step 5.6: attach a PlayerList. Parity with MinecraftServer.cpp:690
    // (`setPlayers(new PlayerList(this))`). Without this, ServerLevel ctor
    // crashes inside `server->getPlayers()->getViewDistance()`.
    MCLE_LOG("mcle_game_init: constructing PlayerList...");
    try {
        PlayerList *pl = new PlayerList(g_server);
        MCLE_LOG("mcle_game_init: PlayerList allocated at %p, attaching...", (void*)pl);
        // Default with _LARGE_WORLDS is 16 (33x33 = 1089 chunks). Way
        // too much on iOS without worker-thread meshing - the main thread
        // can't keep up. With extended-mem entitlement + unloaded-cache
        // LRU eviction in place we can hold 8 (17x17 = 289). Bump toward
        // 16 once worker threads land. (CPU-budget test at vd=6 didn't
        // extend session length, so the kill isn't CPU-related - keep
        // the better visual parity.)
        pl->setViewDistance(8);
        MCLE_LOG("mcle_game_init: PlayerList viewDistance=%d", pl->getViewDistance());
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

    // MinecraftServer::loadLevel: difficulty, spawning and the game type all
    // come from the host options LoadMenu set
    {
        GameType *gameType = LevelSettings::validateGameType(app.GetGameHostOption(eGameHostOption_GameType));
        LevelGenerationOptions *mapOptions = app.getLevelGenerationOptions();
        if (mapOptions != nullptr) {
            Pos *spawnPos = mapOptions->getSpawnPos();
            if (spawnPos != nullptr) g_levels[0]->setSpawnPos(spawnPos);
            g_levels[0]->getLevelData()->setHasBeenInCreative(mapOptions->isFromDLC());
        }
        for (int i = 0; i < 3; ++i) {
            g_levels[i]->difficulty = app.GetGameHostOption(eGameHostOption_Difficulty);
            g_levels[i]->setSpawnSettings(true, g_server->isAnimals());
            g_levels[i]->getLevelData()->setGameType(gameType);
            if (mapOptions != nullptr) {
                g_levels[i]->getLevelData()->setHasBeenInCreative(mapOptions->getLevelHasBeenInCreative());
            }
        }
        g_loadIsNew = g_levels[0]->isNew;
        app.SetGameHostOption(eGameHostOption_HasBeenInCreative,
                              gameType == GameType::CREATIVE || g_levels[0]->getHasBeenInCreative());
        app.SetGameHostOption(eGameHostOption_Structures, g_levels[0]->isGenerateMapFeatures());
    }

    // loadLevel: the game rules a save was made with, a saved tutorial's
    // or mash-up world's, go back in before its chunks are made
    {
        ConsoleSavePath filepath(GAME_RULE_SAVENAME);
        ConsoleSaveFile *csf = g_levels[0]->getLevelStorage()->getSaveFile();
        if (csf->doesFileExist(filepath)) {
            DWORD numberOfBytesRead;
            byteArray ba_gameRules;

            FileEntry *fe = csf->createFile(filepath);

            ba_gameRules.length = fe->getFileSize();
            ba_gameRules.data = new BYTE[ba_gameRules.length];

            csf->setFilePointer(fe, 0, nullptr, FILE_BEGIN);
            csf->readFile(fe, ba_gameRules.data, ba_gameRules.length, &numberOfBytesRead);

            app.m_gameRules.loadGameRules(ba_gameRules.data, ba_gameRules.length);
            csf->closeHandle(fe);
        }
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
        const int total = (2 * kPreloadRadiusChunks + 1) * (2 * kPreloadRadiusChunks + 1);
        int done = 0;
        g_loadStage = 1;
        g_loadPercent = 0;
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
                g_loadPercent = (++done * 100) / total;
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
            // PlayerList::getPlayerForLogin, 4J took this out of ServerPlayer's ctor
            g_player->gameMode->player = g_player;
            // PlayerList::placeNewPlayer, with the skin and cape the client's
            // LoginPacket carries
            g_player->setPlayerIndex(0);
            g_player->setCustomSkin(app.GetPlayerSkinId(0));
            g_player->setCustomCape(app.GetPlayerCapeId(0));
            // This player moves like the client's LocalPlayer, which keeps
            // LivingEntity's step, ServerPlayer's ctor zeroes it
            g_player->footSize = 0.5f;
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: ServerPlayer ctor threw: %{public}s", e.what());
        } catch (...) {
            MCLE_LOG("mcle_game_init: ServerPlayer ctor threw unknown");
        }
    }

    // Minecraft.cpp: on Windows64 the world host keeps the legacy embedded
    // host XUID, which its player file in the save is named by. Then
    // PlayerList::placeNewPlayer loads that player's saved data.
    bool newPlayer = false;
    if (g_player && g_levelStorage) {
        g_player->setXuid((PlayerUID)0xe000d45248242f2eULL); // Win64Xuid::GetLegacyEmbeddedHostXuid
        try {
            CompoundTag *playerTag = g_levelStorage->getPlayerIO()->load(g_player);
            newPlayer = playerTag == nullptr;
            MCLE_LOG("mcle_game_init: player data %s, pos %.1f %.1f %.1f dim %d",
                     playerTag ? "loaded" : "new", g_player->x, g_player->y, g_player->z, g_player->dimension);
            // Only the overworld is wired up to render, so a player saved in
            // another dimension starts at the overworld spawn for now
            if (playerTag && g_player->dimension != 0) {
                Pos *spawn = g_levels[0]->getSharedSpawnPos();
                g_player->dimension = 0;
                g_player->moveTo(spawn->x + 0.5, spawn->y, spawn->z + 0.5, 0, 0);
                delete spawn;
            }
            delete playerTag;
        } catch (...) {
            MCLE_LOG("mcle_game_init: player data load threw");
        }
    }

    if (g_player && g_player->gameMode) {
        // PlayerList::getPlayerForLogin: the player follows the level's game
        // rules. Their progress packets go to the local player.
        LevelRuleset *serverRuleDefs = app.getGameRuleDefinitions();
        if (serverRuleDefs != nullptr) {
            g_player->gameMode->setGameRules(GameRuleDefinition::generateNewGameRulesInstance(
                GameRulesInstance::eGameRulesInstanceType_ServerPlayer, serverRuleDefs, mcle_game_rule_connection()));
        }
    }

    if (g_player && newPlayer) {
        // PlayerList::placeNewPlayer for a player new to the level
        int mapScale = 3;
        int scale = MapItemSavedData::MAP_SIZE * 2 * (1 << mapScale);
        int centreXC = static_cast<int>(Math::round(g_player->x / scale) * scale);
        int centreZC = static_cast<int>(Math::round(g_player->z / scale) * scale);
        // 4J Added - Give every player a map the first time they join a server
        g_player->inventory->setItem(9, std::make_shared<ItemInstance>(static_cast<int>(Item::map_Id), 1,
            g_levels[0]->getAuxValueForMap(g_player->getXuid(), 0, centreXC, centreZC, mapScale)));
        if (app.getGameRuleDefinitions() != nullptr) {
            app.getGameRuleDefinitions()->postProcessPlayer(g_player);
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

    // PlayerList::updatePlayerGameMode: a new player takes the level's game type
    if (g_player && g_player->gameMode) {
        try {
            g_player->gameMode->updateGameMode(g_levels[0]->getLevelData()->getGameType());
        } catch (...) {
            MCLE_LOG("mcle_game_init: updateGameMode threw");
        }
    }

    // Minecraft::init starts the sound engine. The levels' sounds reach the
    // player the way LevelSoundPackets reach a client.
    if (g_player) {
        try {
            mcle_sound_init(StorageManager.GetSaveRootPath());
            for (int i = 0; i < 3; ++i) {
                if (g_levels[i]) mcle_sound_listen(g_player, g_levels[i]);
            }
        } catch (...) {
            MCLE_LOG("mcle_game_init: sound engine init threw");
        }
    }

    // _RunNetworkGame: the client doesn't start until the selected texture
    // pack's data is in and the main loop has reloaded with it
    mcle_texture_packs_wait_for_reload();

    // Flip to ticking state ONLY after the player is fully registered.
    // Otherwise the render thread tick races against the bootstrap and
    // first tick runs entities=0, second tick runs entities=1 mid-init.
    g_initState = kStateTicking;
    g_loadStage = 2;
    g_loadPercent = 0;
    MCLE_LOG("mcle_game_init: 3 levels constructed, ticking enabled");

    // The Minecraft made at launch (mcle_minecraft_main), its world fields
    // filled in from here
    Minecraft *g_minecraftShim = mcle_minecraft_shim();
    MCLE_LOG("mcle_game_init: Minecraft shim at %p (sizeof=%zu)",
             (void*)g_minecraftShim, sizeof(Minecraft));
    // Like Minecraft itself the shim lives for the whole process. Its
    // shared_ptr fields are constructed on the first world and assigned
    // on later ones.
    static bool s_shimFieldsBuilt = false;
    const bool shimFieldsBuilt = s_shimFieldsBuilt;
    s_shimFieldsBuilt = true;

    // Wire the player into the shim so renderChunks's cameraTargetPlayer
    // null check doesn't bail. ServerPlayer extends LivingEntity so the
    // shared_ptr conversion is implicit. Done with placement-new on the
    // already-zeroed buffer so the shared_ptr's default ctor runs.
    if (shimFieldsBuilt) {
        g_minecraftShim->cameraTargetPlayer = g_player;
    } else {
        new (&g_minecraftShim->cameraTargetPlayer) std::shared_ptr<LivingEntity>(g_player);
    }

    // G5: localplayers[0] is what LevelRenderer::updateDirtyChunks reads
    // to find the nearest dirty chunk for rebuild (uses player x/y/z).
    // ServerPlayer != MultiplayerLocalPlayer but they share Entity base
    // and only the position fields are touched in this loop, so a
    // reinterpret-cast placement-new is safe for the read-only path.
    {
        std::shared_ptr<MultiplayerLocalPlayer> *slot =
            reinterpret_cast<std::shared_ptr<MultiplayerLocalPlayer> *>(
                &g_minecraftShim->localplayers[0]);
        if (shimFieldsBuilt) {
            *slot = std::reinterpret_pointer_cast<MultiplayerLocalPlayer>(g_player);
        } else {
            new (slot) std::shared_ptr<MultiplayerLocalPlayer>(
                std::reinterpret_pointer_cast<MultiplayerLocalPlayer>(g_player));
        }
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
    if (!g_minecraftShim->options) try {
        g_minecraftShim->options = new Options();
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

    // ApplyGameSettingsChanged at the start of the game: the render
    // distance setting, before setLevel's allChanged sizes the area
    if (g_minecraftShim->options) g_minecraftShim->options->viewDistance = mcle_view_distance_setting();

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
    if (!g_levelRenderer) try {
        // Allocate the chunk-rebuild event arrays + spawn the 7 worker
        // threads before the renderer ctor so the very first dispatch
        // has them ready. Upstream Minecraft::init calls this; we
        // bypass that path so we have to do it here.
        MCLE_LOG("mcle_game_init: LevelRenderer::staticCtor()...");
        LevelRenderer::staticCtor();
        MCLE_LOG("mcle_game_init: LevelRenderer::staticCtor done");

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
        // The ctor allocates chunkLists (2 per chunk) right before starList
        const int chunkListCount = LevelRenderer::getGlobalChunkCount() * 2;
        mcle_glbridge_set_autoreplay_range(starList - chunkListCount, chunkListCount);
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

    // Minecraft::setLevel with the level of the player's dimension, which
    // the LoginPacket gives the client
    if (g_levelRenderer && g_player) {
        MultiPlayerLevel *lvl = reinterpret_cast<MultiPlayerLevel *>(g_player->getLevel());
        // Minecraft::init builds this before any level is set. LevelRenderer
        // routes block break / crack events into it as soon as it listens to
        // the level, so it has to exist first. It's made again with the
        // textures once they exist.
        if (!g_minecraftShim->particleEngine)
            g_minecraftShim->particleEngine = new ParticleEngine(lvl, nullptr);
        // Its calls go to Game/MCLESound.cpp
        if (!g_minecraftShim->soundEngine)
            g_minecraftShim->soundEngine = new SoundEngine();
        mcle_client_set_level(g_player->getLevel());
    }

    // Minecraft::init: the textures, fonts and hand renderers stay for the
    // whole process. They're made with the texture pack repository from
    // launch, so PreStitchedTextureMap::stitch reads the selected pack.
    if (!g_minecraftShim->textures) {
    static char s_optsBuf[sizeof(Options)] = {0};
    TexturePackRepository *skins = g_minecraftShim->skins;
    Options *options = reinterpret_cast<Options *>(s_optsBuf);

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
            // Minecraft::init makes the particle engine with the textures.
            // The one from before only had to catch events and has none.
            {
                ParticleEngine *early = g_minecraftShim->particleEngine;
                g_minecraftShim->particleEngine = new ParticleEngine(g_clientLevel, textures);
                delete early;
            }
            MCLE_LOG("mcle_game_init: Textures ctor done at %p", (void*)textures);
            // Minecraft::init makes the fonts right after Textures. Signs
            // and name tags draw with them. Same locations as
            // Minecraft::DEFAULT_FONT_LOCATION / ALT_FONT_LOCATION.
            static ResourceLocation s_defaultFontLocation(TN_DEFAULT_FONT);
            static ResourceLocation s_altFontLocation(TN_ALT_FONT);
            g_minecraftShim->font = new Font(options, L"font/Default.png", textures, false,
                                             &s_defaultFontLocation, 23, 20, 8, 8,
                                             SFontData::Codepoints);
            g_minecraftShim->altFont = new Font(options, L"font/alternate.png", textures, false,
                                                &s_altFontLocation, 16, 16, 8, 8);
            try {
                textures->stitch();
                MCLE_LOG("mcle_game_init: textures->stitch done");
                // GameRenderer's constructor makes the item in hand renderer
                g_itemInHand = new ItemInHandRenderer(g_minecraftShim);
                g_minecraftShim->localitemInHandRenderers[0] = g_itemInHand;
                // Minecraft::init: the one mobs and players hold items with
                if (EntityRenderDispatcher::instance) {
                    EntityRenderDispatcher::instance->itemInHandRenderer = new ItemInHandRenderer(g_minecraftShim, false);
                }
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
        if (shimFieldsBuilt) {
            *slot = std::reinterpret_pointer_cast<MultiplayerLocalPlayer>(g_player);
        } else {
            new (slot) std::shared_ptr<MultiplayerLocalPlayer>(
                std::reinterpret_pointer_cast<MultiplayerLocalPlayer>(g_player));
        }
        MCLE_LOG("mcle_game_init: minecraft->player aliased to g_player");
    }

    // Construct Gui (HUD renderer) - parity with upstream Minecraft::init.
    // Stores minecraft pointer + initialises bookkeeping. Actual render
    // happens in mcle_world_drive_renderer per frame.
    if (g_minecraftShim && !g_gui) {
        try {
            g_gui = new Gui(g_minecraftShim);
            MCLE_LOG("mcle_game_init: Gui ctor at %p", (void*)g_gui);
        } catch (const std::exception &e) {
            MCLE_LOG("mcle_game_init: Gui ctor threw: %{public}s", e.what());
        } catch (...) {
            MCLE_LOG("mcle_game_init: Gui ctor threw unknown");
        }
    }

    // ClientConnection::handleLogin's game mode for the primary player,
    // FullTutorialMode in the tutorial world, before the first tick
    try {
        mcle_tutorial_start();
    } catch (...) {
        MCLE_LOG("mcle_game_init: tutorial threw");
    }

    g_loadPercent = 100;
    g_renderReady = true;

    // MinecraftServer::loadLevel saves a newly created level straight away.
    // saveToDisc looks at Minecraft's texture packs, which only exist from
    // here, so the save goes now, the way an autosave does.
    if (g_loadIsNew) mcle_save_now();
    MCLE_LOG("mcle_game_init: initImpl returning");
}

} // anonymous namespace

// Gui's chat lines for UIScene_HUD's chat timer on the main thread. The Gui
// ticks on the sim thread.
static std::mutex s_guiMutex;

static void copyUtf8(const std::wstring &w, char *out, int cap) {
    int n = 0;
    for (wchar_t wc : w) {
        unsigned int c = static_cast<unsigned int>(wc);
        char b[4];
        int len;
        if (c < 0x80) {
            b[0] = static_cast<char>(c);
            len = 1;
        } else if (c < 0x800) {
            b[0] = static_cast<char>(0xC0 | (c >> 6));
            b[1] = static_cast<char>(0x80 | (c & 0x3F));
            len = 2;
        } else {
            b[0] = static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
            b[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            b[2] = static_cast<char>(0x80 | (c & 0x3F));
            len = 3;
        }
        if (n + len >= cap) break;
        for (int i = 0; i < len; i++) out[n++] = b[i];
    }
    out[n] = 0;
}

// Gui::getOpacity / getMessage for a chat line
extern "C" float mcle_hud_chat_line(int index, char *out, int cap) {
    if (cap > 0) out[0] = 0;
    std::lock_guard<std::mutex> lock(s_guiMutex);
    if (!g_gui || g_initState != kStateTicking) return 0.0f;
    float opacity = g_gui->getOpacity(0, static_cast<DWORD>(index));
    if (opacity > 0 && cap > 0) copyUtf8(g_gui->getMessage(0, static_cast<DWORD>(index)), out, cap);
    return opacity;
}

// Gui::getJukeboxOpacity / getJukeboxMessage
extern "C" float mcle_hud_jukebox(char *out, int cap) {
    if (cap > 0) out[0] = 0;
    std::lock_guard<std::mutex> lock(s_guiMutex);
    if (!g_gui || g_initState != kStateTicking) return 0.0f;
    float opacity = g_gui->getJukeboxOpacity(0);
    if (cap > 0) copyUtf8(g_gui->getJukeboxMessage(0), out, cap);
    return opacity;
}

// PlayerConnection::teleport's player->absMoveTo, see WorldProbe/link_stubs.cpp
void mcle_local_player_abs_move_to(double x, double y, double z, float yRot, float xRot) {
    if (g_player) g_player->absMoveTo(x, y, z, yRot, xRot);
}

// PlayerList::broadcastAll's send for the local player, who isn't in its list
void mcle_local_player_packet(std::shared_ptr<Packet> packet);   // WorldProbe/link_stubs.cpp
void mcle_broadcast_to_local_player(std::shared_ptr<Packet> packet, bool oneDimension, int dimension) {
    if (!g_player || (oneDimension && g_player->dimension != dimension)) return;
    mcle_local_player_packet(packet);
}

// ClientConnection::handleExplosion. The server's explosion already moved
// this player (it is the server's own), played the sound and drew the big
// particle, and the blown tiles are already air. What the client's
// finalizeExplosion(true) adds on top is the debris, so that loop is here;
// calling it whole played the sound and the big particle twice.
void mcle_local_player_explosion(std::shared_ptr<ExplodePacket> packet) {
    if (!g_player || !g_player->level || packet->m_bKnockbackOnly) return;
    Level *level = g_player->level;
    const double x = packet->x, y = packet->y, z = packet->z;
    const float r = packet->r;
    static const int MAX_EXPLODE_PARTICLES = 50;
    // 4J - try and make at most MAX_EXPLODE_PARTICLES pairs of particles
    int fraction = static_cast<int>(packet->toBlow.size()) / MAX_EXPLODE_PARTICLES;
    if (fraction == 0) fraction = 1;
    size_t j = packet->toBlow.size() - 1;
    for (auto it = packet->toBlow.rbegin(); it != packet->toBlow.rend(); ++it) {
        if ((j % fraction) == 0) {
            double xa = it->x + level->random->nextFloat();
            double ya = it->y + level->random->nextFloat();
            double za = it->z + level->random->nextFloat();

            double xd = xa - x;
            double yd = ya - y;
            double zd = za - z;

            double dd = sqrt(xd * xd + yd * yd + zd * zd);

            xd /= dd;
            yd /= dd;
            zd /= dd;

            double speed = 0.5 / (dd / r + 0.1);
            speed *= (level->random->nextFloat() * level->random->nextFloat() + 0.3f);
            xd *= speed;
            yd *= speed;
            zd *= speed;

            level->addParticle(eParticleType_explode, (xa + x * 1) / 2, (ya + y * 1) / 2, (za + z * 1) / 2, xd, yd, zd);
            level->addParticle(eParticleType_smoke, xa, ya, za, xd, yd, zd);
        }
        --j;
    }
}

// The chat handler's minecraft->gui calls
void mcle_gui_add_message(const std::wstring &message, bool bIsDeathMessage) {
    MCLE_LOG("chat: %s", narrow(message).c_str());
    std::lock_guard<std::mutex> lock(s_guiMutex);
    if (g_gui) g_gui->addMessage(message, 0, bIsDeathMessage);
}

void mcle_gui_set_action_bar_message(const std::wstring &message) {
    std::lock_guard<std::mutex> lock(s_guiMutex);
    if (g_gui) g_gui->setActionBarMessage(message);
}

// CMinecraftApp::SetPlayerSkin / SetPlayerCape call the local player's
// setAndBroadcastCustomSkin / Cape. The player the server's
// TextureAndGeometryChangePacket would update is this same one.
void mcle_local_player_set_custom_skin(unsigned int skinId) {
    std::lock_guard<std::mutex> lock(g_simTickMutex);
    if (g_player) g_player->setCustomSkin(skinId);
}

void mcle_local_player_set_custom_cape(unsigned int capeId) {
    std::lock_guard<std::mutex> lock(g_simTickMutex);
    if (g_player) g_player->setCustomCape(capeId);
}

// Held while anything outside this file touches the particle engine
std::recursive_mutex &mcle_particle_mutex() {
    return g_particleMutex;
}

// The container menus run on the main thread against the player the sim
// thread owns, holding the tick lock while they do
std::mutex &mcle_sim_tick_mutex() {
    return g_simTickMutex;
}

std::shared_ptr<ServerPlayer> mcle_local_player() {
    return g_player;
}

// While a menu is up the pad drives the menu, not the player
static std::atomic<bool> g_menuDisplayed{false};
// The pad's buttons as the game sees them this tick
static unsigned int g_gameButtons = 0;
// Buttons held down from a menu, which the game doesn't take until let go
static std::atomic<unsigned int> g_heldFromMenu{0};

void mcle_game_set_menu_displayed(bool displayed) {
    // The menu shuts on the press, often between two ticks, so the button
    // that shut it is marked held here rather than on the next tick
    if (!displayed) g_heldFromMenu.fetch_or((unsigned int)mcle_ios_input_poll_buttons(0));
    g_menuDisplayed.store(displayed);
}

// UIController::GetMenuDisplayed and IsPauseMenuDisplayed for the tutorial
bool mcle_game_menu_displayed() {
    return g_menuDisplayed.load();
}

bool mcle_game_paused() {
    return g_gamePaused.load(std::memory_order_relaxed);
}

// ClientConnection::handleAnimate's CRITICAL_HIT and MAGIC_CRITICAL_HIT, from
// scripts/patch-client-effects.py
void mcle_client_crit(shared_ptr<Entity> entity, bool magic) {
    Minecraft *mc = Minecraft::GetInstance();
    if (!mc || !mc->particleEngine || !entity) return;
    std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
    std::shared_ptr<CritParticle> critParticle =
        magic ? std::make_shared<CritParticle>(entity->level, entity, eParticleType_magicCrit)
              : std::make_shared<CritParticle>(entity->level, entity);
    critParticle->CritParticlePostConstructor();
    mc->particleEngine->add(critParticle);
}

// ClientConnection::handleTakeItemEntity: the client's own pickup sound,
// besides the one the server plays, and the item flies into whoever took it
void mcle_client_take(shared_ptr<Entity> item, shared_ptr<Entity> taker) {
    Minecraft *mc = Minecraft::GetInstance();
    if (!mc || !mc->particleEngine || !item || !taker) return;
    if (!g_player || item->level != g_player->level) return;
    // level->playSound(from, ...) is MultiPlayerLevel's, a local sound at the entity
    static Random s_takeRandom;
    const bool isLocalPlayer = taker->entityId == g_player->entityId;
    if (isLocalPlayer && item->GetType() == eTYPE_EXPERIENCEORB) {
        float fPitch = ((s_takeRandom.nextFloat() - s_takeRandom.nextFloat()) * 0.7f + 1.0f) * 2.0f;
        item->level->playLocalSound(item->x, item->y - item->heightOffset, item->z, eSoundType_RANDOM_ORB, 0.2f,
                                    fPitch, false);
    } else {
        item->level->playLocalSound(item->x, item->y - item->heightOffset, item->z, eSoundType_RANDOM_POP, 0.2f,
                                    ((s_takeRandom.nextFloat() - s_takeRandom.nextFloat()) * 0.7f + 1.0f) * 2.0f,
                                    false);
    }
    std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
    mc->particleEngine->add(std::make_shared<TakeAnimationParticle>(item->level, item, taker, -0.5f));
}

// ClientConnection::handleEntityEvent, for what the client's handleEntityEvent
// adds to the entity the server already changed: particles and the zombie
// cure sound. Hurt and death, eating, sheep eating grass, wolves shaking,
// golem arms and the wolf and ocelot taming particles are made by the
// server's own entity, so those aren't run again.
void mcle_client_entity_event(shared_ptr<Entity> e, byte event) {
    if (!e || !g_player || e->level != g_player->level) return;
    switch (event) {
    case EntityEvent::ZOMBIE_CONVERTING:
        if (e->instanceof(eTYPE_ZOMBIE)) e->handleEntityEvent(event);
        break;
    case EntityEvent::WITCH_HAT_MAGIC:
        if (e->instanceof(eTYPE_WITCH)) e->handleEntityEvent(event);
        break;
    case EntityEvent::LOVE_HEARTS:
    case EntityEvent::VILLAGER_ANGRY:
    case EntityEvent::VILLAGER_HAPPY:
        if (e->instanceof(eTYPE_VILLAGER)) e->handleEntityEvent(event);
        break;
    case EntityEvent::IN_LOVE_HEARTS:
        if (e->instanceof(eTYPE_ANIMAL)) e->handleEntityEvent(event);
        break;
    case EntityEvent::TAMING_SUCCEEDED:
    case EntityEvent::TAMING_FAILED:
        if (e->instanceof(eTYPE_HORSE)) e->handleEntityEvent(event);
        break;
    case EntityEvent::FIREWORKS_EXPLODE:
        // FireworksRocketEntity::handleEntityEvent only does it on a client
        // level, then MultiPlayerLevel::createFireworks
        if (e->instanceof(eTYPE_FIREWORKS_ROCKET)) {
            Minecraft *mc = Minecraft::GetInstance();
            if (!mc || !mc->particleEngine) break;
            const int DATA_ID_FIREWORKS_ITEM = 8;  // FireworksRocketEntity's
            shared_ptr<ItemInstance> sourceItem = e->getEntityData()->getItemInstance(DATA_ID_FIREWORKS_ITEM);
            CompoundTag *tag = nullptr;
            if (sourceItem != nullptr && sourceItem->hasTag()) {
                tag = sourceItem->getTag()->getCompound(FireworksItem::TAG_FIREWORKS);
            }
            std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
            mc->particleEngine->add(std::make_shared<FireworksParticles::FireworksStarter>(
                e->level, e->x, e->y, e->z, e->xd, e->yd, e->zd, mc->particleEngine, tag));
        }
        break;
    default:
        break;
    }
}

// minecraft->level->destroyTileProgress from MultiPlayerGameMode
void mcle_client_destroy_progress(int id, int x, int y, int z, int progress) {
    if (!g_levelRenderer) return;
    std::lock_guard<std::mutex> lock(g_destroyingBlocksMutex);
    g_levelRenderer->destroyTileProgress(id, x, y, z, progress);
}

// Pre-opened file descriptor for crash logging. Opened once at init
// so the signal handler can write() directly without going through
// fopen/malloc/NSLog (none of which are async-signal-safe). External
// linkage so MCLE_iOS_MachHandler.cpp can share the same fd.
extern "C" int g_crash_log_fd = -1;

static void mcle_open_crash_log_fd(void) {
    if (g_crash_log_fd >= 0) return;
    extern const char *ios_documents_dir(void);
    const char *root = ios_documents_dir();
    if (!root || !*root) {
        MCLE_LOG("mcle_open_crash_log_fd: ios_documents_dir returned null - "
                 "signal handler will NOT be able to write");
        return;
    }
    char path[1024];
    // Open mcle_log.txt - the dedicated mcle-only file. Ruffle does
    // not write here, so signal-handler writes can't be overwritten
    // by a racing non-O_APPEND writer (which was happening when we
    // shared crash_log.txt with ruffle).
    int n = snprintf(path, sizeof(path), "%s/mcle_log.txt", root);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        MCLE_LOG("mcle_open_crash_log_fd: snprintf failed for path");
        return;
    }
    g_crash_log_fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    MCLE_LOG("mcle_open_crash_log_fd: fd=%d path=%s",
             g_crash_log_fd, path);
}

// Async-signal-safe itoa using a writable buffer the caller supplies.
// Returns number of bytes written. Negative values are formatted as
// "-N". Used by mcle_crash_handler which can't call snprintf.
static size_t mcle_safe_itoa(char *buf, size_t cap, long v) {
    if (cap == 0) return 0;
    if (v < 0) {
        if (cap < 2) return 0;
        buf[0] = '-';
        return 1 + mcle_safe_itoa(buf + 1, cap - 1, -v);
    }
    char tmp[24]; size_t t = 0;
    if (v == 0) { tmp[t++] = '0'; }
    while (v > 0 && t < sizeof(tmp)) { tmp[t++] = '0' + (char)(v % 10); v /= 10; }
    size_t n = 0;
    while (t > 0 && n < cap) { buf[n++] = tmp[--t]; }
    return n;
}

// Async-signal-safe hex write for pointers.
static size_t mcle_safe_hex(char *buf, size_t cap, uintptr_t v) {
    if (cap < 3) return 0;
    buf[0] = '0'; buf[1] = 'x';
    size_t n = 2;
    char tmp[20]; size_t t = 0;
    if (v == 0) { tmp[t++] = '0'; }
    while (v > 0 && t < sizeof(tmp)) {
        int d = (int)(v & 0xF);
        tmp[t++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v >>= 4;
    }
    while (t > 0 && n < cap) { buf[n++] = tmp[--t]; }
    return n;
}

// Crash logger: handler for SIGSEGV / SIGBUS / SIGABRT etc. CRITICAL
// requirement - everything in here must be async-signal-safe (no
// malloc, no NSLog, no fprintf, no objc_msgSend). Otherwise the
// handler crashes inside itself and the process dies silently with
// no log. We use a pre-opened fd + write() + manual int/hex format.
static void mcle_crash_handler(int sig, siginfo_t *info, void *) {
    if (g_crash_log_fd >= 0) {
        char buf[160]; size_t n = 0;
        const char *prefix = "[mcle] mcle: caught signal ";
        size_t plen = 27;
        for (size_t i = 0; i < plen && n < sizeof(buf); ++i) buf[n++] = prefix[i];
        n += mcle_safe_itoa(buf + n, sizeof(buf) - n, sig);
        const char *mid = " at addr ";
        for (size_t i = 0; i < 9 && n < sizeof(buf); ++i) buf[n++] = mid[i];
        n += mcle_safe_hex(buf + n, sizeof(buf) - n,
                           (uintptr_t)(info ? info->si_addr : (void *)0));
        const char *code = " (code=";
        for (size_t i = 0; i < 7 && n < sizeof(buf); ++i) buf[n++] = code[i];
        n += mcle_safe_itoa(buf + n, sizeof(buf) - n, info ? info->si_code : 0);
        if (n < sizeof(buf)) buf[n++] = ')';
        if (n < sizeof(buf)) buf[n++] = '\n';
        ssize_t r = write(g_crash_log_fd, buf, n);
        (void)r;
        fsync(g_crash_log_fd);
    }
    // Give the kernel a moment to flush before re-raise. usleep IS
    // documented async-signal-safe on Darwin.
    usleep(200000);
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, nullptr);
    raise(sig);
}

static void mcle_terminate_handler() {
    // Uncaught C++ throw turns into std::terminate -> abort -> SIGABRT,
    // which the default handler kills the process on without printing
    // anything. Catch the in-flight exception here and log its type +
    // what() so we know which upstream call threw.
    auto ex = std::current_exception();
    if (ex) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception &e) {
            MCLE_LOG("mcle: std::terminate caught std::exception type=%{public}s what=%{public}s",
                     typeid(e).name(), e.what());
        } catch (...) {
            MCLE_LOG("mcle: std::terminate caught unknown C++ exception");
        }
    } else {
        MCLE_LOG("mcle: std::terminate with no current exception");
    }
    usleep(200000);
    std::abort();
}

// Install a per-thread alternate signal stack. sigaltstack is
// per-thread on iOS, so each thread that might fault (main, sim,
// workers) needs its own. Use thread_local so each thread gets its
// own memory automatically. Without this, a UAF that corrupts the
// faulting thread's stack causes the signal handler to crash on its
// way to logging, silently turning a SIGSEGV into an apparent
// SIGKILL with no trace.
extern "C" void mcle_install_sig_altstack(void) {
    static thread_local uint8_t altStack[SIGSTKSZ];
    static thread_local bool installed = false;
    if (installed) return;
    stack_t ss{};
    ss.ss_sp    = altStack;
    ss.ss_size  = sizeof(altStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
    installed = true;
}

// Defined in MCLE_iOS_MachHandler.cpp. Catches kernel-level EXC_*
// exceptions that bypass POSIX signals (EXC_RESOURCE, EXC_GUARD,
// EXC_CRASH on iOS). Order matters: fd must be open first so the
// Mach handler thread can write through it.
extern "C" void mcle_install_mach_exception_handler(void);

static void mcle_install_crash_handler() {
    mcle_install_sig_altstack();
    mcle_open_crash_log_fd();
    mcle_install_mach_exception_handler();

    struct sigaction sa{};
    sa.sa_sigaction = &mcle_crash_handler;
    sa.sa_flags     = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    std::set_terminate(&mcle_terminate_handler);
}

extern "C" void mcle_game_init(void) {
    if (g_initState != kStateUnstarted) return;
    // Build identifier so any log we look at shows immediately which
    // build is running. Plain %s here (not %{public}s) because the
    // file-write path uses fprintf which doesn't understand the
    // os_log "public" modifier - it leaks "{public}s" as literal text
    // and eats the corresponding argument.
    MCLE_LOG("BUILD_INFO build=%s %s", __DATE__, __TIME__);
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

extern "C" void mcle_game_tick(void); // forward decl for the sim loop
extern "C" void mcle_delete_reassigned_storage(void); // link_stubs.cpp

// LoadMenu and CreateWorldMenu start the game here. Same spot where they
// hand RunNetworkGameThreadProc to the FullscreenProgress scene. Init runs
// on its own thread so the loading screen keeps animating.
static int mcle_start_game_thread() {
    g_loadStage = 0;
    g_loadPercent = 0;
    g_loadIsNew = false;
    g_loadFindingSeed = false;
    g_initStarted = true;
    pthread_t t;
    if (pthread_create(&t, nullptr, [](void *) -> void * {
            mcle_game_init();
            // ServerThreadProc lets go of its pools once the level is done
            // with, so the generation caches don't outlive the load
            Chunk::ReleaseThreadStorage();
            mcle_world_thread_end();
            return nullptr;
        }, nullptr) != 0) {
        g_initStarted = false;
        return 0;
    }
    pthread_detach(t);
    return 1;
}

// UIScene_LoadMenu's constructor: the More Options params for the save,
// from its host options when its thumbnail had them
void mcle_load_menu_options(MCLELaunchMoreOptions &params, bool hostOptionsRead, unsigned int uiHostOptions,
                            bool &isSaveOwner) {
    params.bGenerateOptions = false;
    params.bPVP = true;
    params.bTrust = true;
    params.bFireSpreads = true;
    params.bHostPrivileges = false;
    params.bTNT = true;
    params.iPad = 0;
    isSaveOwner = true;
    if (!hostOptionsRead) return;

    // Setup all the text and checkboxes to match what the game was saved with on
    params.bPVP = app.GetGameHostOption(uiHostOptions, eGameHostOption_PvP) > 0;
    params.bTrust = app.GetGameHostOption(uiHostOptions, eGameHostOption_TrustPlayers) > 0;
    params.bFireSpreads = app.GetGameHostOption(uiHostOptions, eGameHostOption_FireSpreads) > 0;
    params.bTNT = app.GetGameHostOption(uiHostOptions, eGameHostOption_TNT) > 0;
    params.bHostPrivileges = app.GetGameHostOption(uiHostOptions, eGameHostOption_CheatsEnabled) > 0;
    params.bDisableSaving = app.GetGameHostOption(uiHostOptions, eGameHostOption_DisableSaving) > 0;
    params.currentWorldSize = static_cast<int>(app.GetGameHostOption(uiHostOptions, eGameHostOption_WorldSize));
    params.newWorldSize = params.currentWorldSize;

    params.bMobGriefing = app.GetGameHostOption(uiHostOptions, eGameHostOption_MobGriefing);
    params.bKeepInventory = app.GetGameHostOption(uiHostOptions, eGameHostOption_KeepInventory);
    params.bDoMobSpawning = app.GetGameHostOption(uiHostOptions, eGameHostOption_DoMobSpawning);
    params.bDoMobLoot = app.GetGameHostOption(uiHostOptions, eGameHostOption_DoMobLoot);
    params.bDoTileDrops = app.GetGameHostOption(uiHostOptions, eGameHostOption_DoTileDrops);
    params.bNaturalRegeneration = app.GetGameHostOption(uiHostOptions, eGameHostOption_NaturalRegeneration);
    params.bDoDaylightCycle = app.GetGameHostOption(uiHostOptions, eGameHostOption_DoDaylightCycle);

    if (!params.bHostPrivileges) {
        // Set defaults
        params.bMobGriefing = true;
        params.bKeepInventory = false;
        params.bDoMobSpawning = true;
        params.bDoDaylightCycle = true;
    }

    if (app.GetGameHostOption(uiHostOptions, eGameHostOption_WasntSaveOwner) > 0) isSaveOwner = false;
}

// UIScene_LoadMenu::LaunchGame and StartGameFromSave: the app's host options
// from the params, the difficulty setting and the game mode toggle
void mcle_game_set_load_options(const MCLELaunchMoreOptions &params, bool isSaveOwner, int difficulty,
                                int gameModeId) {
    app.SetGameHostOption(eGameHostOption_WasntSaveOwner, (!isSaveOwner));

    Minecraft *pMinecraft = Minecraft::GetInstance();
    pMinecraft->skins->selectTexturePackById(params.dwTexturePack);

    const int pad = 0;
    app.SetGameHostOption(eGameHostOption_Difficulty, difficulty);
    app.SetGameHostOption(eGameHostOption_FriendsOfFriends, app.GetGameSettings(pad, eGameSetting_FriendsOfFriends));
    app.SetGameHostOption(eGameHostOption_Gamertags, app.GetGameSettings(pad, eGameSetting_GamertagsVisible));
    app.SetGameHostOption(eGameHostOption_BedrockFog, app.GetGameSettings(pad, eGameSetting_BedrockFog) ? 1 : 0);

    app.SetGameHostOption(eGameHostOption_PvP, params.bPVP);
    app.SetGameHostOption(eGameHostOption_TrustPlayers, params.bTrust);
    app.SetGameHostOption(eGameHostOption_FireSpreads, params.bFireSpreads);
    app.SetGameHostOption(eGameHostOption_TNT, params.bTNT);
    app.SetGameHostOption(eGameHostOption_HostCanFly, params.bHostPrivileges);
    app.SetGameHostOption(eGameHostOption_HostCanChangeHunger, params.bHostPrivileges);
    app.SetGameHostOption(eGameHostOption_HostCanBeInvisible, params.bHostPrivileges);

    app.SetGameHostOption(eGameHostOption_MobGriefing, params.bMobGriefing);
    app.SetGameHostOption(eGameHostOption_KeepInventory, params.bKeepInventory);
    app.SetGameHostOption(eGameHostOption_DoMobSpawning, params.bDoMobSpawning);
    app.SetGameHostOption(eGameHostOption_DoMobLoot, params.bDoMobLoot);
    app.SetGameHostOption(eGameHostOption_DoTileDrops, params.bDoTileDrops);
    app.SetGameHostOption(eGameHostOption_NaturalRegeneration, params.bNaturalRegeneration);
    app.SetGameHostOption(eGameHostOption_DoDaylightCycle, params.bDoDaylightCycle);

    app.SetGameHostOption(eGameHostOption_WorldSize, params.worldSize + 1); // 0 is GAME_HOST_OPTION_WORLDSIZE_UNKNOWN

    // flag if the user wants to reset the Nether to force a Fortress with netherwart etc.
    app.SetResetNether(params.bResetNether);
    // clear out the app's terrain features list
    app.ClearTerrainFeaturePosition();

    app.SetGameHostOption(eGameHostOption_GameType, gameModeId);

    MCLE_LOG("load options: host=%08x difficulty=%d gameType=%d", app.GetGameHostOption(eGameHostOption_All),
             difficulty, gameModeId);
}

// UIScene_CreateWorldMenu::CreateGame. The game thread then starts on a
// level with no save data, like RunNetworkGameThreadProc with saveData null.
int mcle_game_create_world(const MCLELaunchMoreOptions &params, const std::wstring &worldName, int gameModeId,
                           int difficulty) {
    if (g_initStarted || g_initState != kStateUnstarted) return 0;
    app.SetTutorialMode(false);
    // StartNetworkGame: no level generator
    app.setLevelGenerationOptions(nullptr);

    // clear out the app's terrain features list
    app.ClearTerrainFeaturePosition();

    StorageManager.ResetSaveData();
    // Make our next save default to the name of the level
    StorageManager.SetSaveTitle(worldName.c_str());

    const std::wstring &wSeed = params.seed;
    NewWorldParams param;
    param.active = true;
    if (wSeed.length() != 0) {
        int64_t value = 0;
        unsigned int len = static_cast<unsigned int>(wSeed.length());

        // Check if the input string contains a numerical value
        bool isNumber = true;
        for (unsigned int i = 0; i < len; ++i) {
            if (wSeed.at(i) < L'0' || wSeed.at(i) > L'9') {
                if (!(i == 0 && wSeed.at(i) == L'-')) {
                    isNumber = false;
                    break;
                }
            }
        }

        // If the input string is a numerical value, convert it to a number
        if (isNumber) value = _fromString<int64_t>(wSeed);

        // If the value is not 0 use it, otherwise use the algorithm from the java String.hashCode() function to hash it
        if (value != 0) {
            param.seed = value;
        } else {
            // Wraps like the int arithmetic it's copied from
            unsigned int hashValue = 0;
            for (unsigned int i = 0; i < len; ++i) hashValue = 31u * hashValue + static_cast<unsigned int>(wSeed.at(i));
            param.seed = static_cast<int>(hashValue);
        }
    } else {
        // the seed is found in initServer
        param.findSeed = true;
    }

    Minecraft *pMinecraft = Minecraft::GetInstance();
    pMinecraft->skins->selectTexturePackById(params.dwTexturePack);

    const int pad = 0;
    // Minecraft's options->difficulty is the Difficulty setting
    app.SetGameHostOption(eGameHostOption_Difficulty, difficulty);
    app.SetGameHostOption(eGameHostOption_FriendsOfFriends, params.bAllowFriendsOfFriends);
    app.SetGameHostOption(eGameHostOption_Gamertags, app.GetGameSettings(pad, eGameSetting_GamertagsVisible) ? 1 : 0);

    app.SetGameHostOption(eGameHostOption_BedrockFog, app.GetGameSettings(pad, eGameSetting_BedrockFog) ? 1 : 0);

    app.SetGameHostOption(eGameHostOption_GameType, gameModeId);
    app.SetGameHostOption(eGameHostOption_LevelType, params.bFlatWorld);
    app.SetGameHostOption(eGameHostOption_Structures, params.bStructures);
    app.SetGameHostOption(eGameHostOption_BonusChest, params.bBonusChest);

    app.SetGameHostOption(eGameHostOption_PvP, params.bPVP);
    app.SetGameHostOption(eGameHostOption_TrustPlayers, params.bTrust);
    app.SetGameHostOption(eGameHostOption_FireSpreads, params.bFireSpreads);
    app.SetGameHostOption(eGameHostOption_TNT, params.bTNT);
    app.SetGameHostOption(eGameHostOption_HostCanFly, params.bHostPrivileges);
    app.SetGameHostOption(eGameHostOption_HostCanChangeHunger, params.bHostPrivileges);
    app.SetGameHostOption(eGameHostOption_HostCanBeInvisible, params.bHostPrivileges);

    app.SetGameHostOption(eGameHostOption_MobGriefing, params.bMobGriefing);
    app.SetGameHostOption(eGameHostOption_KeepInventory, params.bKeepInventory);
    app.SetGameHostOption(eGameHostOption_DoMobSpawning, params.bDoMobSpawning);
    app.SetGameHostOption(eGameHostOption_DoMobLoot, params.bDoMobLoot);
    app.SetGameHostOption(eGameHostOption_DoTileDrops, params.bDoTileDrops);
    app.SetGameHostOption(eGameHostOption_NaturalRegeneration, params.bNaturalRegeneration);
    app.SetGameHostOption(eGameHostOption_DoDaylightCycle, params.bDoDaylightCycle);

    app.SetGameHostOption(eGameHostOption_WasntSaveOwner, false);
    app.SetGameHostOption(eGameHostOption_WorldSize, params.worldSize + 1); // 0 is GAME_HOST_OPTION_WORLDSIZE_UNKNOWN

    switch (params.worldSize) {
    case 0:
        // Classic
        param.xzSize = LEVEL_WIDTH_CLASSIC;
        param.hellScale = HELL_LEVEL_SCALE_CLASSIC; // hellsize = 54/3 = 18
        break;
    case 1:
        // Small
        param.xzSize = LEVEL_WIDTH_SMALL;
        param.hellScale = HELL_LEVEL_SCALE_SMALL; // hellsize = ceil(64/3) = 22
        break;
    case 2:
        // Medium
        param.xzSize = LEVEL_WIDTH_MEDIUM;
        param.hellScale = HELL_LEVEL_SCALE_MEDIUM; // hellsize= ceil(3*64/6) = 32
        break;
    case 3:
        // Large
        param.xzSize = LEVEL_WIDTH_LARGE;
        param.hellScale = HELL_LEVEL_SCALE_LARGE; // hellsize = ceil(5*64/8) = 40
        break;
    }

    MCLE_LOG("create world: host=%08x seed=%lld findSeed=%d xz=%d", app.GetGameHostOption(eGameHostOption_All),
             (long long)param.seed, param.findSeed ? 1 : 0, param.xzSize);
    g_newWorld = param;
    g_pendingLevelId = worldName;
    return mcle_start_game_thread();
}

// UIComponent_Panorama::tick reads the loaded level's game time and the
// player's dimension. Returns 0 when no level is loaded.
extern "C" int mcle_world_game_time(long long *gameTime, int *dimension) {
    if (g_initState != kStateTicking) return 0;
    ServerLevel *level = g_levels[0];
    if (!level) return 0;
    *gameTime = level->getLevelData()->getGameTime();
    *dimension = g_player ? g_player->dimension : 0;
    return 1;
}

extern "C" int mcle_player_is_dead(void) {
    return g_playerDead.load(std::memory_order_relaxed) ? 1 : 0;
}

// UIScene_DeathMenu's Respawn
extern "C" void mcle_game_respawn(void) {
    g_respawnRequested.store(true, std::memory_order_relaxed);
}

// ClientConnection::handleRespawn's ConnectingProgress, for the view
// controller: the level changes started, the ones the client has finished
// (eAppAction_WaitForDimensionChangeComplete) and the last one's title
static std::atomic<int> g_levelChangesStarted{0};
static std::atomic<int> g_levelChangesFinished{0};
static std::atomic<int> g_levelChangeTitle{-1};
// ClientConnection::handleGameEvent's WIN_GAME, for the EndPoem
static std::atomic<int> g_winGames{0};
// eAppAction_WaitForRespawnComplete: respawns done
static std::atomic<int> g_respawnsDone{0};

extern "C" int mcle_game_level_changes(int *finished, int *stringId) {
    if (stringId) *stringId = g_levelChangeTitle.load();
    if (finished) *finished = g_levelChangesFinished.load();
    return g_levelChangesStarted.load();
}

extern "C" int mcle_game_wins(void) {
    return g_winGames.load();
}

extern "C" int mcle_game_respawns(void) {
    return g_respawnsDone.load();
}

// ClientConnection::handleRespawn: going to another dimension puts up
// ConnectingProgress with where the player's going. The client takes the
// new level once the server is done moving the player.
void mcle_local_player_respawn_packet(int dimension) {
    const int oldDimension = g_clientLevel ? g_clientLevel->dimension->id : 0;
    if (dimension == oldDimension) return;
    int stringId = -1;
    if (dimension == -1) {
        stringId = IDS_PROGRESS_ENTERING_NETHER;
    } else if (oldDimension == -1) {
        stringId = IDS_PROGRESS_LEAVING_NETHER;
    } else if (dimension == 1) {
        stringId = IDS_PROGRESS_ENTERING_END;
    } else if (oldDimension == 1) {
        stringId = IDS_PROGRESS_LEAVING_END;
    }
    g_levelChangeTitle.store(stringId);
    g_levelChangesStarted.fetch_add(1);
}

// ClientConnection::handleGameEvent WIN_GAME
void mcle_local_player_win_game() {
    g_winGames.fetch_add(1);
}

// handleRespawn's Minecraft::setLevel for the level the server put the
// local player in, then the teleport that starts the client again. With
// g_frameMutex held.
static void mcle_client_follow_player() {
    if (g_player && g_player->level != g_clientLevel) {
        Minecraft *mc = Minecraft::GetInstance();
        // 4J - stop update thread from processing this level, which blocks until it is safe to move on
        mc->gameRenderer->DisableUpdateThread();
        // Stop any streaming music (e.g. jukebox) when changing dimensions
        g_levelRenderer->playStreamingMusic(L"", 0, 0, 0);
        mcle_client_set_level(g_player->getLevel());
        // respawnPlayer makes the client a new player
        mcle_portal_client_reset();
        mc->gameRenderer->EnableUpdateThread();
    }
    g_levelChangesFinished.store(g_levelChangesStarted.load());
}

// PlayerConnection::handleClientCommand(PERFORM_RESPAWN): PlayerList::respawn
// makes a new player at the spawn, or back in the overworld after the End
// poem. The player has no network connection, and respawn stores the new
// player in it and sends packets through it, so a zeroed one stands in for
// the call (send / teleport are the link_stubs ones, which don't touch it).
static void mcle_do_respawn() {
    if (!g_player || !g_server || !g_server->getPlayers()) return;
    if (!g_player->wonGame && g_player->getHealth() > 0) return;
    struct alignas(PlayerConnection) ConnectionBuf { char bytes[sizeof(PlayerConnection)]; };
    static ConnectionBuf s_connection = {};
    PlayerConnection *standIn = reinterpret_cast<PlayerConnection *>(&s_connection);
    // connection is a shared_ptr; this one doesn't own the buffer
    g_player->connection = std::shared_ptr<PlayerConnection>(standIn, [](PlayerConnection *) {});
    std::shared_ptr<ServerPlayer> fresh;
    try {
        if (g_player->wonGame) {
            fresh = g_server->getPlayers()->respawn(g_player, g_player->m_enteredEndExitPortal ? 0 : g_player->dimension,
                                                    true);
        } else {
            fresh = g_server->getPlayers()->respawn(g_player, 0, false);
        }
    } catch (...) {
        MCLE_LOG("respawn threw");
    }
    g_player->connection = nullptr;
    // respawn gave the connection the new player, which this one keeps
    standIn->setPlayer(nullptr);
    if (!fresh) return;
    fresh->connection = nullptr;
    // LocalPlayer's step, like the first player's
    fresh->footSize = 0.5f;
    Minecraft *mc = Minecraft::GetInstance();
    // Minecraft::respawnPlayer: don't do updating whilst the player &
    // localplayer array are adjusted
    mc->gameRenderer->DisableUpdateThread();
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        std::shared_ptr<MultiplayerLocalPlayer> alias = std::reinterpret_pointer_cast<MultiplayerLocalPlayer>(fresh);
        mc->player = alias;
        mc->localplayers[0] = alias;
        mc->cameraTargetPlayer = fresh;
        g_playerGameMode = fresh->gameMode;
        g_player = fresh;
        mcle_client_follow_player();
    }
    mc->gameRenderer->EnableUpdateThread();
    mcle_player_new_local_player();
    mcle_sound_listen(fresh, nullptr);
    mcle_portal_client_reset();
    g_respawnsDone.fetch_add(1);
    MCLE_LOG("respawned at %.1f %.1f %.1f dim %d", fresh->x, fresh->y, fresh->z, fresh->dimension);
}

// PlayerConnection::handleCraftItem crafts on the server player, and
// awardStat and refreshContainer in there send their packets through its
// connection. As for respawn, a zeroed one stands in while it runs: the
// only thing called on it is the link_stubs send, which doesn't touch it.
void mcle_run_with_stand_in_connection(const std::shared_ptr<ServerPlayer> &player, void (*fn)(void *), void *arg) {
    if (!player) return;
    struct alignas(PlayerConnection) ConnectionBuf { char bytes[sizeof(PlayerConnection)]; };
    static ConnectionBuf s_connection = {};
    const bool standIn = player->connection == nullptr;
    if (standIn) {
        player->connection = std::shared_ptr<PlayerConnection>(reinterpret_cast<PlayerConnection *>(&s_connection),
                                                               [](PlayerConnection *) {});
    }
    struct Restore {
        const std::shared_ptr<ServerPlayer> &player;
        bool standIn;
        ~Restore() {
            if (standIn) player->connection = nullptr;
        }
    } restore{player, standIn};
    fn(arg);
}

// ServerPlayer::changeDimension, from scripts/patch-dimension-ios.py. The
// server changes the level in the middle of the player's tick, which here
// holds the frame, so the change waits for the end of the tick. That way
// ConnectingProgress keeps drawing while the arrival portal is found or
// built. The first portal of the tick is the one taken, like upstream,
// where the player is gone from the old level after it.
static bool g_changeDimensionPending = false;
static int g_changeDimensionTo = 0;
static bool g_changingDimension = false;

bool mcle_defer_change_dimension(ServerPlayer *player, int i) {
    if (g_changingDimension) return false;
    if (!g_changeDimensionPending) {
        g_changeDimensionPending = true;
        g_changeDimensionTo = i;
    }
    return true;
}

static void mcle_do_change_dimension() {
    if (!g_changeDimensionPending) return;
    g_changeDimensionPending = false;
    if (!g_player) return;
    // Nothing draws the old level while the player leaves it
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        g_levelChanging.store(true);
    }
    g_changingDimension = true;
    try {
        mcle_run_with_stand_in_connection(g_player, [](void *to) {
            g_player->changeDimension(*static_cast<int *>(to));
        }, &g_changeDimensionTo);
    } catch (...) {
        MCLE_LOG("changeDimension threw");
    }
    g_changingDimension = false;
    {
        std::lock_guard<std::mutex> lock(g_frameMutex);
        mcle_client_follow_player();
        g_levelChanging.store(false);
    }
    MCLE_LOG("changeDimension to %d: dim %d at %.1f %.1f %.1f", g_changeDimensionTo, g_player->dimension, g_player->x,
             g_player->y, g_player->z);
}

// ActionGameSettings for the host in game: In-Game Gamertags and Bedrock
// Fog go to the game host options, which the renderers read, and the
// players show on maps per the gamertags one
extern "C" void mcle_game_host_settings_changed(void) {
    if (g_initState != kStateTicking) return;
    app.SetGameHostOption(eGameHostOption_Gamertags, mcle_settings_get(13 /*GamertagsVisible*/) ? 1 : 0);
    app.SetGameHostOption(eGameHostOption_BedrockFog, mcle_settings_get(24 /*BedrockFog*/) ? 1 : 0);
    std::lock_guard<std::mutex> lock(g_simTickMutex);
    if (g_player) g_player->setShowOnMaps(app.GetGameHostOption(eGameHostOption_Gamertags) != 0);
}

extern "C" void mcle_game_set_paused(int paused) {
    g_gamePaused.store(paused != 0, std::memory_order_relaxed);
}

extern "C" int mcle_game_start(const char *levelFolder) {
    if (g_initStarted || g_initState != kStateUnstarted) return 0;
    app.SetTutorialMode(false);
    // StartNetworkGame: no level generator, loadLevel puts back any the save has
    app.setLevelGenerationOptions(nullptr);
    g_pendingLevelId = widen(levelFolder ? levelFolder : "");
    g_newWorld = NewWorldParams();
    return mcle_start_game_thread();
}

// Minecraft::main up to the menus, on the main thread like Windows64's main:
// its thread storage, the statics, then the default game rules, which read
// Tutorial.pck into the level generators.
extern "C" void mcle_minecraft_main(void) {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    Tesselator::CreateNewThreadStorage(16 * 1024);
    AABB::CreateNewThreadStorage();
    Vec3::CreateNewThreadStorage();
    IntCache::CreateNewThreadStorage();
    Compression::CreateNewThreadStorage();
    OldChunkStorage::CreateNewThreadStorage();
    Tile::CreateNewThreadStorage();

    mcle_world_static_ctors();
    app.loadDefaultGameRules();
    MCLE_LOG("minecraft main: %d level generators", (int)app.getLevelGenerators()->size());

    // Minecraft::start makes the Minecraft: its constructor the sound engine
    // and the instance, init the texture packs. The DLC mount from the menus
    // adds the DLC ones to it.
    Minecraft *minecraft = mcle_minecraft_shim();
    minecraft->soundEngine = new SoundEngine();
    Minecraft::m_instance = minecraft;
    mcle_texture_packs_init(minecraft);
}

// UIScene_LoadOrJoinMenu::AddDefaultButtons lists the level generators after
// Create New World. Only the tutorial for now: a mash-up world needs its
// pack's data mounted, which isn't done on iOS yet.
static std::vector<LevelGenerationOptions *> g_menuGenerators;
static std::string g_menuGeneratorName;

extern "C" int mcle_level_generators_refresh(void) {
    g_menuGenerators.clear();
    for (LevelGenerationOptions *levelGen : *app.getLevelGenerators()) {
        if (levelGen->requiresTexturePack()) continue;
        g_menuGenerators.push_back(levelGen);
    }
    return static_cast<int>(g_menuGenerators.size());
}

extern "C" const char *mcle_level_generator_name(int i) {
    if (i < 0 || i >= static_cast<int>(g_menuGenerators.size())) return "";
    LPCWSTR wstr = g_menuGenerators[i]->getWorldName();
    g_menuGeneratorName.clear();
    for (const wchar_t *p = wstr; p && *p; ++p) {
        unsigned int c = static_cast<unsigned int>(*p);
        if (c < 0x80) {
            g_menuGeneratorName += static_cast<char>(c);
        } else if (c < 0x800) {
            g_menuGeneratorName += static_cast<char>(0xC0 | (c >> 6));
            g_menuGeneratorName += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            g_menuGeneratorName += static_cast<char>(0xE0 | ((c >> 12) & 0x0F));
            g_menuGeneratorName += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            g_menuGeneratorName += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return g_menuGeneratorName.c_str();
}

// UIScene_LoadOrJoinMenu::handlePress on the tutorial: LoadLevelGen, then
// what StartNetworkGame and ServerThreadProc do with its init data
extern "C" int mcle_game_start_level_gen(int i) {
    if (g_initStarted || g_initState != kStateUnstarted) return 0;
    if (i < 0 || i >= static_cast<int>(g_menuGenerators.size())) return 0;
    LevelGenerationOptions *levelGen = g_menuGenerators[i];
    app.SetTutorialMode(levelGen->isTutorial());
    // The others go through LoadMenu
    if (!levelGen->isTutorial()) return 0;

    // clear out the app's terrain features list
    app.ClearTerrainFeaturePosition();

    StorageManager.ResetSaveData();
    // Make our next save default to the name of the level
    StorageManager.SetSaveTitle(levelGen->getDefaultSaveName().c_str());

    NewWorldParams param;
    param.active = true;
    param.xzSize = LEVEL_LEGACY_WIDTH;
    param.hellScale = HELL_LEVEL_LEGACY_SCALE;
    const unsigned int settings = app.GetGameHostOption(eGameHostOption_Tutorial);

    app.setLevelGenerationOptions(levelGen);
    param.seed = app.getLevelGenerationOptions()->getLevelSeed();

    // Load the tutorial save data here
    if (levelGen->requiresBaseSave() && !levelGen->getBaseSavePath().empty()) {
        std::string fileRoot = std::string(StorageManager.GetSaveRootPath()) + "/Windows64Media/Tutorial/" +
                               wstringtofilename(levelGen->getBaseSavePath());
        FILE *f = fopen(fileRoot.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long dwFileSize = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (dwFileSize > 0) {
                PBYTE pbData = new BYTE[dwFileSize];
                if (fread(pbData, 1, static_cast<size_t>(dwFileSize), f) == static_cast<size_t>(dwFileSize)) {
                    levelGen->setBaseSaveData(pbData, static_cast<DWORD>(dwFileSize));
                } else {
                    delete[] pbData;
                }
            }
            fclose(f);
        }
    }

    // ServerThreadProc
    app.SetGameHostOption(eGameHostOption_All, settings);

    MCLE_LOG("level gen: tutorial seed=%lld host=%08x", (long long)param.seed, settings);
    g_newWorld = param;
    // initServer names the level from server.properties' level-name
    g_pendingLevelId = L"world";
    return mcle_start_game_thread();
}

// FullscreenProgress says it's generating the spawn area for a new level
extern "C" int mcle_game_load_is_new(void) {
    return g_loadIsNew.load() ? 1 : 0;
}

// and that it's finding a seed while findSeed runs
extern "C" int mcle_game_load_finding_seed(void) {
    return g_loadFindingSeed.load() ? 1 : 0;
}

// -1 failed, 0-2 g_loadStage, 3 ready to draw.
extern "C" int mcle_game_load_status(int *percent) {
    if (percent) *percent = g_loadPercent.load();
    if (g_initState == kStateFailed || g_initState == kStateReadyIdle) return -1;
    if (g_renderReady) return 3;
    return g_loadStage.load();
}

// Phase 3a: sim thread runs the tick body in a loop at ~60Hz. The
// internal 50ms simulation throttle inside mcle_game_tick gates the
// actual sim step to 20Hz; we sleep 16ms between calls so unused
// frames don't burn CPU.
static void *mcle_sim_thread_loop(void *) {
    mcle_install_sig_altstack();
    MCLE_LOG("mcle_sim_thread_loop: started, beginning ticks");
    for (;;) {
        mcle_game_tick();
        // Sleep until the next tick is due, a few ms at a time so pausing
        // and loading still get looked at
        using clock = std::chrono::steady_clock;
        const uint64_t nowNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   clock::now().time_since_epoch()).count();
        const uint64_t nextNs = g_nextSimTickNs.load(std::memory_order_relaxed);
        // The tick schedule only moves while a world is ticking, so paused,
        // loading or back at the menus it just waits a frame
        const bool running = g_initState == kStateTicking && g_levels[0] &&
                             !g_gamePaused.load(std::memory_order_relaxed);
        uint64_t waitUs = 16 * 1000;
        if (running && nextNs > nowNs) {
            waitUs = std::min<uint64_t>((nextNs - nowNs) / 1000, 16 * 1000);
        } else if (running && nextNs != 0) {
            waitUs = 0;
        }
        if (waitUs > 0) usleep((useconds_t)waitUs);
    }
    return nullptr;
}

// GameRenderer's chunk update thread. DisableUpdateThread stops it and waits
// for the one running, so allChanged can remake the chunks;
// EnableUpdateThread lets it go again.
static std::atomic<bool> g_chunkUpdateRunning{true};
static std::mutex g_chunkUpdateMutex;
// Set on the update thread while it's updating, which has nothing to wait for
static thread_local bool t_inChunkUpdate = false;

void mcle_chunk_update_disable() {
    if (!g_chunkUpdateRunning.exchange(false)) return;
    if (t_inChunkUpdate) return;
    std::lock_guard<std::mutex> wait(g_chunkUpdateMutex);
}

void mcle_chunk_update_enable() {
    g_chunkUpdateRunning.store(true);
}

// GameRenderer::runUpdate
static void *mcle_chunk_update_thread_loop(void *) {
    mcle_install_sig_altstack();
    Vec3::CreateNewThreadStorage();
    AABB::CreateNewThreadStorage();
    IntCache::CreateNewThreadStorage();
    Tesselator::CreateNewThreadStorage(1024 * 1024);
    Compression::UseDefaultThreadStorage();
    Chunk::CreateNewThreadStorage();
    Tile::CreateNewThreadStorage();
    for (;;) {
        bool idle = true;
        {
            std::lock_guard<std::mutex> lock(g_chunkUpdateMutex);
            Minecraft *mc = Minecraft::GetInstance();
            if (g_chunkUpdateRunning.load() && g_initState == kStateTicking &&
                g_renderReady.load(std::memory_order_acquire) && g_levelRenderer && mc && mc->textures) {
                t_inChunkUpdate = true;
                // Update chunks atomically until there aren't any very near ones left
                static const int MAX_DEFERRED_UPDATES = 10;
                int count = 0;
                bool shouldContinue = false;
                try {
                    do {
                        shouldContinue = g_levelRenderer->updateDirtyChunks();
                        count++;
                    } while (shouldContinue && count < MAX_DEFERRED_UPDATES);
                } catch (...) {}
                try { g_levelRenderer->fullyFlagRenderableTileEntitiesToBeRemoved(); } catch (...) {}
                mcle_delete_reassigned_storage();
                AABB::resetPool();
                Vec3::resetPool();
                IntCache::Reset();
                idle = !g_levelRenderer->dirtyChunkPresent;
                t_inChunkUpdate = false;
            }
        }
        // Nothing was dirty, wait a bit like the PS3 build does
        if (idle) usleep(5 * 1000);
    }
    return nullptr;
}

// ServerPlayer::doChunkSendingTick for the local player. Its connection is
// local, so the chunk isn't sent, the client's chunks are the server's: the
// nearest finalised one comes off chunksToSend and its render chunks are made
// dirty, so ones built while it was still loading get built again.
static void mcle_local_chunk_sending_tick(const std::shared_ptr<ServerPlayer> &player) {
    if (player->chunksToSend.empty()) return;
    Level *level = player->level;
    if (level == nullptr) return;
    ChunkPos nearest = player->chunksToSend.front();
    bool nearestValid = false;
    double dist = DBL_MAX;
    for (ChunkPos chunk : player->chunksToSend) {
        if (level->isChunkFinalised(chunk.x, chunk.z)) {
            double newDist = chunk.distanceToSqr(player->x, player->z);
            if ((!nearestValid) || (newDist < dist)) {
                nearest = chunk;
                dist = chunk.distanceToSqr(player->x, player->z);
                nearestValid = true;
            }
        }
    }
    if (!nearestValid) return;
    player->chunksToSend.remove(nearest);
    level->setTilesDirty(nearest.x * 16 + 1, 1, nearest.z * 16 + 1, nearest.x * 16 + 14, Level::maxBuildHeight - 2,
                         nearest.z * 16 + 14);
}

extern "C" void mcle_game_tick(void) {
    // Install per-thread sigaltstack on whichever thread first calls
    // this. Idempotent. Covers main (UIKit) and sim threads; workers
    // install at the top of rebuildChunkThreadProc via patch.
    mcle_install_sig_altstack();

    // After the sim thread is up, the iOS main thread's call to
    // mcle_game_tick from MinecraftViewController is a no-op. The
    // sim thread itself bypasses this guard via pthread_equal.
    if (g_simThreadStarted.load(std::memory_order_acquire) &&
        !pthread_equal(pthread_self(), g_simThread))
    {
        return;
    }

    // Once init succeeds, spawn the dedicated sim thread (once) and
    // return immediately so main + sim don't race running the body in
    // parallel for the first tick. Subsequent main calls return at the
    // guard at the top.
    if (g_initState == kStateTicking &&
        !g_simThreadStarted.load(std::memory_order_acquire))
    {
        bool expected = false;
        if (g_simThreadStarted.compare_exchange_strong(expected, true)) {
            if (pthread_create(&g_simThread, nullptr,
                               mcle_sim_thread_loop, nullptr) == 0)
            {
                pthread_detach(g_simThread);
                pthread_t updateThread;
                if (pthread_create(&updateThread, nullptr, mcle_chunk_update_thread_loop, nullptr) == 0) {
                    pthread_detach(updateThread);
                }
                MCLE_LOG("mcle_game_tick: sim thread spawned, main goes "
                         "no-op from next call");
                return; // Don't race the sim thread on its first iteration.
            }
            // Spawn failed - reset flag and fall through to tick on main
            // this frame and every subsequent frame.
            g_simThreadStarted.store(false);
            MCLE_LOG("mcle_game_tick: sim thread spawn FAILED, "
                     "fallback to main-thread tick");
        }
    }

    // Per-thread TLS init - mirrors the bootstrap thread setup.
    // Tile::setShape and Tesselator are both TLS-singletons; without
    // these on the current thread the first tick null-derefs.
    // thread_local so the sim thread re-runs init for itself rather
    // than skipping because main already set the flag.
    thread_local bool s_tickThreadTlsReady = false;
    if (!s_tickThreadTlsReady) {
        Tile::CreateNewThreadStorage();
        Tesselator::CreateNewThreadStorage(16 * 1024);
        Vec3::CreateNewThreadStorage();
        IntCache::CreateNewThreadStorage();
        AABB::CreateNewThreadStorage();
        OldChunkStorage::CreateNewThreadStorage();
        Level::enableLightingCache();
        Chunk::CreateNewThreadStorage();
        // Compression is a TLS-singleton: getCompression() returns the
        // current thread's slot. McRegionChunkStorage::load -> RegionFile
        // -> Compression::getCompression() crashes on null TLS during
        // chunk streaming on this thread without this init.
        Compression::CreateNewThreadStorage();
        s_tickThreadTlsReady = true;
        MCLE_LOG("mcle_game_tick: tick-thread full TLS init done");
    }

    // Leaving the world waits on this for the tick in flight
    std::unique_lock<std::mutex> tickLock(g_simTickMutex);

    g_tickCount++;

    if (g_initState != kStateTicking || !g_levels[0]) {
        if ((g_tickCount % kLogEveryN) == 0) {
            MCLE_LOG("tick %llu (probe lib running, simulation idle, state=%d)",
                     static_cast<unsigned long long>(g_tickCount), g_initState.load());
        }
        return;
    }

    // Minecraft::init has the LevelRenderer long before a level ticks. Here
    // ticking is switched on while init is still setting rendering up, and
    // level code (Level::getCubes) reaches into the renderer.
    if (!g_renderReady.load(std::memory_order_acquire)) return;

    // UIScene_SettingsGraphicsMenu sets options->viewDistance when the render
    // distance slider moves, and LevelRenderer::render's allChanged resizes
    // the area with the chunk update thread stopped. Here it's done with the
    // frame held, and allChanged stops the update thread itself.
    {
        Minecraft *mc = Minecraft::GetInstance();
        const int vd = mcle_view_distance_setting();
        if (mc && mc->options && g_levelRenderer && mc->options->viewDistance != vd) {
            std::lock_guard<std::mutex> frameLock(g_frameMutex);
            mc->options->viewDistance = vd;
            try { g_levelRenderer->allChanged(); } catch (...) { MCLE_LOG("allChanged threw"); }
        }
    }

    // Throttle simulation to 20Hz (50ms per tick) to match upstream parity.
    // CADisplayLink calls us at 60Hz; without this, aiStep + tickEntities run
    // 3x too fast and walking / gravity / friction all feel sped up.
    // Publish tick start time so the render thread can compute partial-tick
    // alpha for smooth camera interp between 20Hz simulation steps.
    // Like Minecraft's Timer: ticks fall on a fixed 50ms grid, and the
    // render's partial tick is measured from the grid, not from when the
    // thread happened to wake up.
    {
        using clock = std::chrono::steady_clock;
        const uint64_t tickNs = 50000000ull;
        const uint64_t nowNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   clock::now().time_since_epoch()).count();
        static uint64_t s_nextTickNs = 0;
        if (s_nextTickNs == 0) s_nextTickNs = nowNs;
        if (nowNs < s_nextTickNs) return;
        uint64_t thisTickNs = s_nextTickNs;
        // Timer gives up on catching up after 10 ticks
        if (nowNs - thisTickNs > 10 * tickNs) thisTickNs = nowNs;
        s_nextTickNs = thisTickNs + tickNs;
        // Published once the player has moved, until then the render stays
        // at the end of the last tick, which is where this one starts
        g_pendingSimTickNs = thisTickNs;
        g_nextSimTickNs.store(s_nextTickNs, std::memory_order_relaxed);
    }

    if (g_gamePaused.load(std::memory_order_relaxed)) return;

    if (g_respawnRequested.exchange(false)) mcle_do_respawn();

    // Minecraft::tick: gui->tick() while not paused
    {
        std::lock_guard<std::mutex> lock(s_guiMutex);
        if (g_gui) g_gui->tick();
    }

    // GameRenderer::tick: torch flicker for the light texture
    // (tickLightTexture) and the wither boss darkening the world
    {
        static float s_blrt = 0.0f;
        s_blrt += static_cast<float>((Math::random() - Math::random()) * Math::random() * Math::random());
        s_blrt *= 0.9;
        float blr = g_lightBlr.load(std::memory_order_relaxed);
        blr += (s_blrt - blr) * 1;
        g_lightBlr.store(blr, std::memory_order_relaxed);

        float darken = g_darkenWorldAmount.load(std::memory_order_relaxed);
        g_darkenWorldAmountO.store(darken, std::memory_order_relaxed);
        if (BossMobGuiInfo::darkenWorld) {
            darken += 1.0f / (static_cast<float>(SharedConstants::TICKS_PER_SECOND) * 1);
            if (darken > 1) darken = 1;
            BossMobGuiInfo::darkenWorld = false;
        } else if (darken > 0) {
            darken -= 1.0f / (static_cast<float>(SharedConstants::TICKS_PER_SECOND) * 4);
        }
        g_darkenWorldAmount.store(darken, std::memory_order_relaxed);

        if (g_player && g_player->level) {
            float fogBr = g_fogBr.load(std::memory_order_relaxed);
            g_fogBrO.store(fogBr, std::memory_order_relaxed);
            float brr = g_player->level->getBrightness(Mth::floor(g_player->x), Mth::floor(g_player->y),
                                                       Mth::floor(g_player->z));
            float whiteness = (3 - Minecraft::GetInstance()->options->viewDistance) / 3.0f;
            float fogBrT = brr * (1 - whiteness) + whiteness;
            fogBr += (fogBrT - fogBr) * 0.1f;
            g_fogBr.store(fogBr, std::memory_order_relaxed);
        }
    }

    // Minecraft::tick: if (!pause) particleEngine->tick();
    if (Minecraft *mcPE = Minecraft::GetInstance()) {
        if (mcPE->particleEngine) {
            std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
            try { mcPE->particleEngine->tick(); } catch (...) {}
        }
    }

    // Minecraft::tick runs these once per frame so storage queued for
    // delete actually gets freed
    SparseLightStorage::tick();
    CompressedTileStorage::tick();
    SparseDataStorage::tick();

    // While a menu is up the game gets no buttons, and a button pressed then
    // only counts once it's been let go, so the press that shut the menu
    // isn't a press in the game as well
    {
        const unsigned int raw = (unsigned int)mcle_ios_input_poll_buttons(0);
        if (g_menuDisplayed.load()) {
            g_heldFromMenu.fetch_or(raw);
            g_gameButtons = 0;
        } else {
            const unsigned int held = g_heldFromMenu.fetch_and(raw) & raw;
            g_gameButtons = raw & ~held;
        }
    }

    // Minecraft::tick's player input: mining, placing, attacking, hotbar.
    // Before the levels tick, like the client's packets being handled at
    // the start of the server tick.
    if (g_player && g_player->level) {
        // gameMode->tick(), which for the TutorialMode ticks the tutorial
        try {
            mcle_tutorial_tick();
        } catch (...) {
            MCLE_LOG("mcle_game_tick: tutorial threw");
        }
        try {
            mcle_player_input_tick(g_player, g_player->getLevel(), g_gameButtons);
        } catch (...) {
            MCLE_LOG("mcle_game_tick: player input threw");
        }
        try {
            mcle_tooltips_update(g_player, mcle_player_hit_result());
        } catch (...) {
            MCLE_LOG("mcle_game_tick: tooltips threw");
        }
    }

    // MinecraftServer::tick: difficulty and spawning follow the host options,
    // animals the server's spawn-animals.
    for (int i = 0; i < 3; ++i) {
        if (!g_levels[i]) continue;
        g_levels[i]->difficulty = app.GetGameHostOption(eGameHostOption_Difficulty);
        g_levels[i]->setSpawnSettings(g_levels[i]->difficulty > 0 && !Minecraft::GetInstance()->isTutorial(), g_server->isAnimals());
    }

    // Tick all three dimensions in order. Parity with how the upstream
    // server's runUpdate iterates levels[] each frame.
    using hrclock = std::chrono::steady_clock;
    const auto tickStart = hrclock::now();
    // Mobs are drawn between their old and new place by the time since the
    // last tick, which is set after the player's move below. From the first
    // entity moving until then the frame waits, or it draws the mobs' new
    // places with the old time and they jump.
    std::unique_lock<std::mutex> entityFrameLock(g_frameMutex, std::defer_lock);
    try {
        for (int i = 0; i < 3; ++i) {
            if (!g_levels[i]) continue;
            const auto lvlStart = hrclock::now();
            g_levels[i]->tick();
            if (!entityFrameLock.owns_lock()) entityFrameLock.lock();
            g_levels[i]->tickEntities();
            const auto lvlMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   hrclock::now() - lvlStart).count();
            if (lvlMs > 50) {
                MCLE_LOG("SLOW_TICK level=%d took=%lldms", i, (long long)lvlMs);
            }
        }
        // LevelRenderer::tick increments the renderer-side `ticks` member
        // which renderClouds reads for the cloud drift offset
        // (time * 0.03f at LevelRenderer.cpp:1330). Without this the cloud
        // layer is frozen in place. Also drives destroying-block animations
        // and the per-second tick maintenance branch.
        if (g_levelRenderer) {
            static int s_lrTickLog = 0;
            try {
                std::lock_guard<std::mutex> lock(g_destroyingBlocksMutex);
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

    // PlayerConnection::handleMovePlayer runs doTick(false) on the server
    // player every tick. Player::tick in there does the aiStep movement
    // (xxa/yya/jumping are set per frame from the controller in
    // mcle_world_drive_renderer) plus food and the rest.
    // tickEntities above already saved xOld via Level::tick, so the camera
    // interp anchor is still right.
    bool published = false;
    if (g_player) {
        try {
            const float xRotBefore = g_player->xRot, yRotBefore = g_player->yRot;
            const bool menu = g_menuDisplayed.load(std::memory_order_relaxed);
            // handleMovePlayer takes no moves from a player who has won
            if (!g_player->wonGame)
            {
                // The camera is drawn between the player's old and new place by
                // the time since the last tick. Moving the player and setting
                // that time under the frame lock keeps a frame from seeing the
                // new place with the old time.
                if (!entityFrameLock.owns_lock()) entityFrameLock.lock();
                // ServerConnection::tick, before the player's packets: the
                // item in use counting down, sleeping and the xp pickup delay
                g_player->updateFrameTick();
                mcle_player_move_tick(g_player, menu ? 0.0f : mcle_ios_input_poll_lx(0) / 1000.0f,
                                      menu ? 0.0f : mcle_ios_input_poll_ly(0) / 1000.0f,
                                      menu ? 0.0f : mcle_ios_input_poll_rx(0) / 1000.0f,
                                      menu ? 0.0f : mcle_ios_input_poll_ry(0) / 1000.0f,
                                      g_gameButtons,
                                      mcle_settings_get(6 /*SensitivityInGame*/) / 100.0f,
                                      mcle_settings_get(10 /*ControlInvertLook*/) != 0);
                const double xBefore = g_player->x, yBefore = g_player->y, zBefore = g_player->z;
                const bool onGroundBefore = g_player->onGround;
                // Level::tick saves where the world is drawn from, but skips a
                // player whose chunks aren't all in yet, like after a respawn.
                // The client player doesn't move until then, this one does.
                const int pxc = Mth::floor(xBefore), pzc = Mth::floor(zBefore);
                if (g_player->level && !g_player->level->hasChunksAt(pxc - 32, 0, pzc - 32, pxc + 32, 0, pzc + 32)) {
                    g_player->xOld = xBefore;
                    g_player->yOld = yBefore;
                    g_player->zOld = zBefore;
                }
                g_player->doTick(false);
                // Entity::rideTick: the client's player ticks inside its ride
                // tick, then sits back on its mount. It's drawn from where the
                // mount had it last tick, which Level::tick kept in xOld.
                const bool riding = g_player->riding != nullptr;
                if (riding) {
                    g_player->riding->positionRider();
                    g_player->xo = g_player->xOld;
                    g_player->yo = g_player->yOld;
                    g_player->zo = g_player->zOld;
                }
                // MultiplayerLocalPlayer::tick: a move the tutorial doesn't allow
                // goes back
                if (!mcle_tutorial_can_move_to_position(xBefore, yBefore, zBefore, g_player->x, g_player->y,
                                                        g_player->z)) {
                    g_player->setPos(xBefore, g_player->y, zBefore);
                }
                // PlayerConnection::handleMovePlayer, which a riding player's
                // move returns before
                if (!riding) {
                    // assume the player made a jump (the client's own jump
                    // exhaustion does nothing, scripts/patch-local-player-jump-exhaustion.py)
                    if (onGroundBefore && !g_player->onGround && g_player->y - yBefore > 0) {
                        g_player->causeFoodExhaustion(FoodConstants::EXHAUSTION_JUMP);
                    }
                    // ServerPlayer's own checkFallDamage does nothing, falls
                    // count from the move
                    g_player->doCheckFallDamage(g_player->y - yBefore, g_player->onGround);
                }
                // LocalPlayer turns inside aiStep, after baseTick saved the old
                // rotation, so that's what the camera interpolates from
                g_player->xRotO = xRotBefore;
                g_player->yRotO = yRotBefore;
                // The new place shows from now on, so that's where the
                // in-between starts
                g_lastSimTickNs.store((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()).count(),
                                      std::memory_order_relaxed);
                published = true;
                entityFrameLock.unlock();
            }
            mcle_player_fov_tick(g_player);
            mcle_portal_client_tick(g_player);
            if (g_itemInHand) {
                std::lock_guard<std::mutex> lock(g_itemInHandMutex);
                g_itemInHand->tick();
            }
            // Minecraft::run's soundEngine->tick and the main loop's
            // playMusicTick, volumes from the settings like ActionGameSettings
            mcle_sound_tick(g_player, 1.0f, mcle_settings_get(0 /*MusicVolume*/) / 100.0f,
                            mcle_settings_get(1 /*SoundFXVolume*/) / 100.0f);
            mcle_hud_update(g_player);
            animateTick(g_player->level, Mth::floor(g_player->x), Mth::floor(g_player->y), Mth::floor(g_player->z));
            mcle_weather_tick(g_player, g_player->level);
            g_playerDead.store(g_player->getHealth() <= 0, std::memory_order_relaxed);
        } catch (...) {
            MCLE_LOG("mcle_game_tick: doTick threw");
        }
    }
    if (!published) g_lastSimTickNs.store(g_pendingSimTickNs, std::memory_order_relaxed);
    if (entityFrameLock.owns_lock()) entityFrameLock.unlock();

    // A portal the player went through in its tick, with the frame let go
    mcle_do_change_dimension();

    // On-demand chunk streaming. Walk clamp is gone; instead drive
    // PlayerChunkMap each tick so new chunks load as the player moves.
    if (g_player && g_player->level) {
        try {
            PlayerChunkMap *cm = g_player->getLevel()->getChunkMap();
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
                // Wide MEMSTATS line: one snapshot of every leak suspect
                // in a single log message so we can correlate growth
                // across resident memory, the display-list cache, and
                // the live chunk + entity counts. 5-second wall-clock
                // cadence so the rhythm isn't tied to sim tick rate.
                {
                    using mclock = std::chrono::steady_clock;
                    static auto s_lastMem = mclock::now() - std::chrono::seconds(10);
                    if (std::chrono::duration_cast<std::chrono::seconds>(
                            mclock::now() - s_lastMem).count() >= 5)
                    {
                        s_lastMem = mclock::now();
                        double residentMB = 0.0, virtualMB = 0.0;
                        mach_task_basic_info info{};
                        mach_msg_type_number_t taskCount = MACH_TASK_BASIC_INFO_COUNT;
                        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                                      (task_info_t)&info, &taskCount) == KERN_SUCCESS) {
                            residentMB = info.resident_size / (1024.0 * 1024.0);
                            virtualMB  = info.virtual_size  / (1024.0 * 1024.0);
                        }
                        // phys_footprint is what jetsam compares against the limit
                        double footprintMB = 0.0;
                        task_vm_info_data_t vmInfo{};
                        mach_msg_type_number_t vmCount = TASK_VM_INFO_COUNT;
                        if (task_info(mach_task_self(), TASK_VM_INFO,
                                      (task_info_t)&vmInfo, &vmCount) == KERN_SUCCESS) {
                            footprintMB = vmInfo.phys_footprint / (1024.0 * 1024.0);
                        }
                        const double availMB = os_proc_available_memory() / (1024.0 * 1024.0);
                        const unsigned long long listCount = mcle_glbridge_list_count();
                        const unsigned long long listBytes = mcle_glbridge_list_bytes();
                        const unsigned long long drawCount = mcle_glbridge_total_draws();
                        size_t loadedChunks = 0;
                        size_t entityCount  = 0;
                        try {
                            if (g_levels[0]) {
                                ChunkSource *src = g_levels[0]->getChunkSource();
                                auto *sc = dynamic_cast<ServerChunkCache *>(src);
                                if (sc) {
                                    if (auto *l = sc->getLoadedChunkList()) {
                                        loadedChunks = l->size();
                                    }
                                }
                                entityCount = g_levels[0]->entities.size();
                            }
                        } catch (...) {}
                        // Heap totals across all malloc zones. mallocBytes
                        // is the total live bytes the allocator has handed
                        // out and not freed. If this tracks resident, the
                        // leak is C++/upstream heap allocations. If
                        // mallocBytes stays flat but resident grows, the
                        // leak is non-malloc (Metal buffers, Mach VM, etc).
                        double mallocMB = 0.0;
                        size_t mallocCount = 0;
                        try {
                            vm_address_t *zones = nullptr;
                            unsigned int zoneCount = 0;
                            if (malloc_get_all_zones(mach_task_self(), nullptr,
                                                      &zones, &zoneCount) == KERN_SUCCESS) {
                                for (unsigned int z = 0; z < zoneCount; ++z) {
                                    malloc_zone_t *zone = (malloc_zone_t *)zones[z];
                                    if (!zone) continue;
                                    malloc_statistics_t st{};
                                    malloc_zone_statistics(zone, &st);
                                    mallocMB += st.size_in_use / (1024.0 * 1024.0);
                                    mallocCount += st.blocks_in_use;
                                }
                            }
                        } catch (...) {}
                        const long regionLive  = g_leak_count[0].load(std::memory_order_relaxed);
                        const long regionTotal = g_leak_total[0].load(std::memory_order_relaxed);
                        const long trLive      = g_leak_count[1].load(std::memory_order_relaxed);
                        const long trTotal     = g_leak_total[1].load(std::memory_order_relaxed);
                        const long lcLive      = g_leak_count[2].load(std::memory_order_relaxed);
                        const long lcTotal     = g_leak_total[2].load(std::memory_order_relaxed);
                        MCLE_LOG("MEMSTATS footprint=%.1fMB avail=%.1fMB resident=%.1fMB virtual=%.1fMB "
                                 "mallocMB=%.1f mallocBlocks=%zu "
                                 "lists=%llu listMB=%.2f draws=%llu "
                                 "loadedChunks=%zu entities=%zu saveInFlight=%d "
                                 "Region(L=%ld T=%ld) TR(L=%ld T=%ld) LC(L=%ld T=%ld)",
                                 footprintMB, availMB, residentMB, virtualMB,
                                 mallocMB, mallocCount,
                                 (unsigned long long)listCount,
                                 listBytes / (1024.0 * 1024.0),
                                 (unsigned long long)drawCount,
                                 loadedChunks, entityCount,
                                 (int)g_autoSaveInFlight.load(std::memory_order_acquire),
                                 regionLive, regionTotal,
                                 trLive, trTotal,
                                 lcLive, lcTotal);
                    }
                }
                const auto cmStart = hrclock::now();
                cm->move(g_player);
                cm->tick();
                const auto cmMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      hrclock::now() - cmStart).count();
                if (cmMs > 50) {
                    MCLE_LOG("SLOW_PCM cm->move+tick took=%lldms", (long long)cmMs);
                }
            }
        } catch (...) {}
        // ServerConnection::tick's doChunkSendingTick
        try {
            mcle_local_chunk_sending_tick(g_player);
        } catch (...) {}
    }

    // MinecraftServer::tick's. Chunk rebuilds are on the update thread
    // (mcle_chunk_update_thread_loop), not in the tick.
    AABB::resetPool();
    Vec3::resetPool();
    IntCache::Reset();

    const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             hrclock::now() - tickStart).count();
    if (totalMs > 100) {
        MCLE_LOG("SLOW_GAME_TICK total=%lldms", (long long)totalMs);
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
extern "C" void mcle_glbridge_metal_ortho(float left, float right, float bottom, float top, float zNear,
                                          float zFar);
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
extern "C" void mcle_glbridge_set_fog_mode(int exp_mode);
extern "C" void mcle_glbridge_set_fog_density(float v);
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

// GameRenderer's fr / fg / fb from setupClearColor. Also what the next
// frame clears to, like its glClearColor.
static float g_fr = 0.0f, g_fg = 0.0f, g_fb = 0.0f;
static std::atomic<bool> g_haveClearColor{false};
// GameRenderer::isInClouds, set by moveCameraToPlayer
static bool g_isInClouds = false;
// GameRenderer::setupCamera's renderDistance from Options::viewDistance
static float mcle_render_distance() {
    const int vd = Minecraft::GetInstance()->options->viewDistance;
    return vd >= 0 ? static_cast<float>(16 * 16 >> vd) : static_cast<float>((16 * 16) << (-vd));
}

// GameRenderer::getNightVisionScale
static float mcle_night_vision_scale(float a) {
    int duration = g_player->getEffect(MobEffect::nightVision)->getDuration();
    if (duration > (SharedConstants::TICKS_PER_SECOND * 10)) {
        return 1.0f;
    }
    float flash = std::max(0.0f, (float)duration - a);
    return .7f + Mth::sin(flash * PI * .05f) * .3f;
}

// GameRenderer::setupClearColor
static void mcle_setup_clear_color(float a) {
    Level *level = g_player->level;
    std::shared_ptr<ServerPlayer> player = g_player;
    Minecraft *mc = Minecraft::GetInstance();

    float whiteness = 1.0f / (4 - mc->options->viewDistance);
    whiteness = 1 - static_cast<float>(pow(static_cast<double>(whiteness), 0.25));

    Vec3 *skyColor = level->getSkyColor(player, a);
    float sr = static_cast<float>(skyColor->x);
    float sg = static_cast<float>(skyColor->y);
    float sb = static_cast<float>(skyColor->z);

    Vec3 *fogColor = level->getFogColor(a);
    float fr = static_cast<float>(fogColor->x);
    float fg = static_cast<float>(fogColor->y);
    float fb = static_cast<float>(fogColor->z);

    if (mc->options->viewDistance < 2) {
        Vec3 *sunAngle = Mth::sin(level->getSunAngle(a)) > 0 ? Vec3::newTemp(-1, 0, 0) : Vec3::newTemp(1, 0, 0);
        float d = static_cast<float>(player->getViewVector(a)->dot(sunAngle));
        if (d < 0) d = 0;
        if (d > 0) {
            float *c = level->dimension->getSunriseColor(level->getTimeOfDay(a), a);
            if (c != nullptr) {
                d *= c[3];
                fr = fr * (1 - d) + c[0] * d;
                fg = fg * (1 - d) + c[1] * d;
                fb = fb * (1 - d) + c[2] * d;
            }
        }
    }

    fr += (sr - fr) * whiteness;
    fg += (sg - fg) * whiteness;
    fb += (sb - fb) * whiteness;

    float rainLevel = level->getRainLevel(a);
    if (rainLevel > 0) {
        float ba = 1 - rainLevel * 0.5f;
        float bb = 1 - rainLevel * 0.4f;
        fr *= ba;
        fg *= ba;
        fb *= bb;
    }
    float thunderLevel = level->getThunderLevel(a);
    if (thunderLevel > 0) {
        float ba = 1 - thunderLevel * 0.5f;
        fr *= ba;
        fg *= ba;
        fb *= ba;
    }

    int t = Camera::getBlockAt(level, player, a);
    if (g_isInClouds) {
        Vec3 *cc = level->getCloudColor(a);
        fr = static_cast<float>(cc->x);
        fg = static_cast<float>(cc->y);
        fb = static_cast<float>(cc->z);
    } else if (t != 0 && Tile::tiles[t]->material == Material::water) {
        float clearness = EnchantmentHelper::getOxygenBonus(player) * 0.2f;

        unsigned int colour = mc->getColourTable()->getColor(eMinecraftColour_Under_Water_Clear_Colour);
        byte redComponent = ((colour >> 16) & 0xFF);
        byte greenComponent = ((colour >> 8) & 0xFF);
        byte blueComponent = ((colour) & 0xFF);

        fr = static_cast<float>(redComponent) / 256 + clearness;
        fg = static_cast<float>(greenComponent) / 256 + clearness;
        fb = static_cast<float>(blueComponent) / 256 + clearness;
    } else if (t != 0 && Tile::tiles[t]->material == Material::lava) {
        unsigned int colour = mc->getColourTable()->getColor(eMinecraftColour_Under_Lava_Clear_Colour);
        byte redComponent = ((colour >> 16) & 0xFF);
        byte greenComponent = ((colour >> 8) & 0xFF);
        byte blueComponent = ((colour) & 0xFF);

        fr = static_cast<float>(redComponent) / 256;
        fg = static_cast<float>(greenComponent) / 256;
        fb = static_cast<float>(blueComponent) / 256;
    }

    float fogBrO = g_fogBrO.load(std::memory_order_relaxed);
    float brr = fogBrO + (g_fogBr.load(std::memory_order_relaxed) - fogBrO) * a;
    fr *= brr;
    fg *= brr;
    fb *= brr;

    double yy = (player->yOld + (player->y - player->yOld) * a) * level->dimension->getClearColorScale();

    if (player->hasEffect(MobEffect::blindness)) {
        int duration = player->getEffect(MobEffect::blindness)->getDuration();
        if (duration < 20) {
            yy = yy * (1.0f - static_cast<float>(duration) / 20.0f);
        } else {
            yy = 0;
        }
    }

    if (yy < 1) {
        if (yy < 0) yy = 0;
        yy = yy * yy;
        fr *= yy;
        fg *= yy;
        fb *= yy;
    }

    float darkenWorldAmount = g_darkenWorldAmount.load(std::memory_order_relaxed);
    if (darkenWorldAmount > 0) {
        float darkenWorldAmountO = g_darkenWorldAmountO.load(std::memory_order_relaxed);
        float amount = darkenWorldAmountO + (darkenWorldAmount - darkenWorldAmountO) * a;
        fr = fr * (1.0f - amount) + (fr * .7f) * amount;
        fg = fg * (1.0f - amount) + (fg * .6f) * amount;
        fb = fb * (1.0f - amount) + (fb * .6f) * amount;
    }

    if (player->hasEffect(MobEffect::nightVision)) {
        float scale = mcle_night_vision_scale(a);
        float dist = FLT_MAX; // MGH - changed this to avoid divide by zero
        if ((fr > 0) && (dist > (1.0f / fr))) dist = (1.0f / fr);
        if ((fg > 0) && (dist > (1.0f / fg))) dist = (1.0f / fg);
        if ((fb > 0) && (dist > (1.0f / fb))) dist = (1.0f / fb);
        fr = fr * (1.0f - scale) + (fr * dist) * scale;
        fg = fg * (1.0f - scale) + (fg * dist) * scale;
        fb = fb * (1.0f - scale) + (fb * dist) * scale;
    }

    g_fr = fr;
    g_fg = fg;
    g_fb = fb;
    g_haveClearColor.store(true, std::memory_order_release);
}

// GameRenderer::setupFog. i is -1 for the sky, 0 for the terrain.
static void mcle_setup_fog(int i, float alpha) {
    std::shared_ptr<ServerPlayer> player = g_player;
    Level *level = player->level;
    bool creative = player->abilities.instabuild;

    mcle_glbridge_set_fog_color(g_fr, g_fg, g_fb, 1);

    int t = Camera::getBlockAt(level, player, alpha);

    if (player->hasEffect(MobEffect::blindness)) {
        float distance = 5.0f;
        int duration = player->getEffect(MobEffect::blindness)->getDuration();
        if (duration < 20) {
            distance = 5.0f + (mcle_render_distance() - 5.0f) * (1.0f - static_cast<float>(duration) / 20.0f);
        }

        mcle_glbridge_set_fog_mode(0);
        if (i < 0) {
            mcle_glbridge_set_fog_start(0);
            mcle_glbridge_set_fog_end(distance * 0.8f);
        } else {
            mcle_glbridge_set_fog_start(distance * 0.25f);
            mcle_glbridge_set_fog_end(distance);
        }
    } else if (g_isInClouds) {
        mcle_glbridge_set_fog_mode(1);
        mcle_glbridge_set_fog_density(0.1f);
    } else if (t > 0 && Tile::tiles[t]->material == Material::water) {
        mcle_glbridge_set_fog_mode(1);
        if (player->hasEffect(MobEffect::waterBreathing)) {
            mcle_glbridge_set_fog_density(0.05f);
        } else {
            mcle_glbridge_set_fog_density(0.1f - (EnchantmentHelper::getOxygenBonus(player) * 0.03f));
        }
    } else if (t > 0 && Tile::tiles[t]->material == Material::lava) {
        mcle_glbridge_set_fog_mode(1);
        mcle_glbridge_set_fog_density(2.0f);
    } else {
        float distance = mcle_render_distance();
        if (!level->dimension->hasCeiling) {
            // 4J - test for doing bedrockfog brought forward from 1.2.3
            if (level->dimension->hasBedrockFog() && !creative) {
                double yy = ((player->getLightColor(alpha) & 0xf00000) >> 20) / 16.0 +
                            (player->yOld + (player->y - player->yOld) * alpha + 4) / 32;
                if (yy < 1) {
                    if (yy < 0) yy = 0;
                    yy = yy * yy;
                    float dist = 100 * static_cast<float>(yy);
                    if (dist < 5) dist = 5;
                    if (distance > dist) distance = dist;
                }
            }
        }

        mcle_glbridge_set_fog_mode(0);
        if (i < 0) {
            mcle_glbridge_set_fog_start(0);
            mcle_glbridge_set_fog_end(distance * 0.8f);
        } else {
            mcle_glbridge_set_fog_start(distance * 0.25f);
            mcle_glbridge_set_fog_end(distance);
        }

        if (level->dimension->isFoggyAt(static_cast<int>(player->x), static_cast<int>(player->z))) {
            mcle_glbridge_set_fog_start(distance * 0.05f);
            mcle_glbridge_set_fog_end(std::min(distance, 16 * 16 * .75f) * .5f);
        }
    }
}

// GuiComponent::fill, a flat colour over the given gui rectangle
static void mcle_gui_fill(int x0, int y0, int x1, int y1, int col) {
    if (x0 < x1) {
        int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    if (y0 < y1) {
        int tmp = y0;
        y0 = y1;
        y1 = tmp;
    }
    float a = ((col >> 24) & 0xff) / 255.0f;
    float r = ((col >> 16) & 0xff) / 255.0f;
    float g = ((col >> 8) & 0xff) / 255.0f;
    float b = ((col) & 0xff) / 255.0f;
    Tesselator *t = Tesselator::getInstance();
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    t->begin();
    t->vertex(static_cast<float>(x0), static_cast<float>(y1), static_cast<float>(0));
    t->vertex(static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(0));
    t->vertex(static_cast<float>(x1), static_cast<float>(y0), static_cast<float>(0));
    t->vertex(static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(0));
    t->end();
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

// The parts of Gui::render still drawn natively next to the SWF HUD: the
// pumpkin blur, the portal, falling asleep and the red death fade. Gui works
// in gui units over the whole screen, here that's 0..w and 0..h of the
// drawable.
static void mcle_gui_overlays(float a) {
    std::shared_ptr<ItemInstance> headGear = g_player->inventory->getArmor(3);
    const bool pumpkin = mcle_third_person_view() == 0 && headGear != nullptr && headGear->id == Tile::pumpkin_Id;
    const float pt = g_player->hasEffect(MobEffect::confusion) ? 0.0f : mcle_portal_client_time(a);
    if (!pumpkin && pt <= 0 && g_player->getSleepTimer() <= 0 && g_player->isAlive()) return;

    int w = 0, h = 0;
    mcle_metal_current_size(&w, &h);
    if (w <= 0 || h <= 0) return;

    // GameRenderer::setupGuiScreen. Nothing here depth tests, so the depth
    // clear it does isn't needed.
    mcle_glbridge_matrix_mode(0x1701 /* GL_PROJECTION */);
    mcle_glbridge_load_identity();
    mcle_glbridge_metal_ortho(0, static_cast<float>(w), static_cast<float>(h), 0, 1000, 3000);
    mcle_glbridge_matrix_mode(0x1700 /* GL_MODELVIEW */);
    mcle_glbridge_load_identity();
    mcle_glbridge_translate(0, 0, -2000);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Gui::renderPumpkin
    if (pumpkin) {
        static ResourceLocation s_pumpkinBlur(TN__BLUR__MISC_PUMPKINBLUR);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(false);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, 1);
        glDisable(GL_ALPHA_TEST);
        Minecraft::GetInstance()->textures->bindTexture(&s_pumpkinBlur);
        Tesselator *t = Tesselator::getInstance();
        t->begin();
        t->vertexUV(static_cast<float>(0), static_cast<float>(h), static_cast<float>(-90), static_cast<float>(0), static_cast<float>(1));
        t->vertexUV(static_cast<float>(w), static_cast<float>(h), static_cast<float>(-90), static_cast<float>(1), static_cast<float>(1));
        t->vertexUV(static_cast<float>(w), static_cast<float>(0), static_cast<float>(-90), static_cast<float>(1), static_cast<float>(0));
        t->vertexUV(static_cast<float>(0), static_cast<float>(0), static_cast<float>(-90), static_cast<float>(0), static_cast<float>(0));
        t->end();
        glDepthMask(true);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_ALPHA_TEST);
        glColor4f(1, 1, 1, 1);
    }

    // Gui::renderTp, the portal's own texture over the screen
    if (pt > 0) {
        float br = pt;
        if (br < 1) {
            br = br * br;
            br = br * br;
            br = br * 0.8f + 0.2f;
        }

        glDisable(GL_ALPHA_TEST);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(false);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, br);
        Minecraft::GetInstance()->textures->bindTexture(&TextureAtlas::LOCATION_BLOCKS);

        Icon *slot = Tile::portalTile->getTexture(Facing::UP);
        float u0 = slot->getU0();
        float v0 = slot->getV0();
        float u1 = slot->getU1();
        float v1 = slot->getV1();
        Tesselator *t = Tesselator::getInstance();
        t->begin();
        t->vertexUV(static_cast<float>(0), static_cast<float>(h), static_cast<float>(-90), (float)(u0), (float)(v1));
        t->vertexUV(static_cast<float>(w), static_cast<float>(h), static_cast<float>(-90), (float)(u1), (float)(v1));
        t->vertexUV(static_cast<float>(w), static_cast<float>(0), static_cast<float>(-90), (float)(u1), (float)(v0));
        t->vertexUV(static_cast<float>(0), static_cast<float>(0), static_cast<float>(-90), (float)(u0), (float)(v0));
        t->end();
        glDepthMask(true);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_ALPHA_TEST);
        glColor4f(1, 1, 1, 1);
    }

    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // falling asleep darkens the screen
    if (g_player->getSleepTimer() > 0) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_ALPHA_TEST);
        int timer = g_player->getSleepTimer();
        float amount = static_cast<float>(timer) / static_cast<float>(Player::SLEEP_DURATION);
        if (amount > 1) {
            // waking up
            amount = 1.0f - (static_cast<float>(timer - Player::SLEEP_DURATION) /
                             static_cast<float>(Player::WAKE_UP_DURATION));
        }
        int color = static_cast<int>(220.0f * amount) << 24 | (0x101020);
        mcle_gui_fill(0, 0, w, h, color);
        glEnable(GL_ALPHA_TEST);
        glEnable(GL_DEPTH_TEST);
    }

    // 4J-PB - Request from Mojang to have a red death screen
    if (!g_player->isAlive()) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_ALPHA_TEST);
        int timer = g_player->getDeathFadeTimer();
        float amount = static_cast<float>(timer) / static_cast<float>(Player::DEATHFADE_DURATION);
        int color = static_cast<int>(220.0f * amount) << 24 | (0x200000);
        mcle_gui_fill(0, 0, w, h, color);
        glEnable(GL_ALPHA_TEST);
        glEnable(GL_DEPTH_TEST);
    }
}

// Gui::render's animated character: the player at the top of the screen
// while sneaking, sprinting or flying, kept up a moment after. Gui lays it
// out over the whole screen, not the 16:9 movie, in gui units of
// Minecraft's 720 line screen at the safe zone plus 10.
static void mcle_gui_animated_character(float a) {
    static int s_characterDisplayTimer = 0;
    if (!mcle_settings_get(31 /*AnimatedCharacter*/)) return;
    const bool bDisplayGui = !g_menuDisplayed.load(std::memory_order_relaxed) &&
                             mcle_settings_get(25 /*DisplayHUD*/) != 0;
    if (!bDisplayGui) {
        s_characterDisplayTimer = 0;
    } else if (g_player->isSneaking()) {
        s_characterDisplayTimer = 30;
    } else if (g_player->isSprinting()) {
        s_characterDisplayTimer = 30;
    } else if (g_player->abilities.flying) {
        s_characterDisplayTimer = 5; // quickly get rid of the player display if they stop flying
    } else if (s_characterDisplayTimer > 0) {
        --s_characterDisplayTimer;
    }
    const bool displayCrouch = g_player->isSneaking() || s_characterDisplayTimer > 0;
    const bool displaySprint = g_player->isSprinting() || s_characterDisplayTimer > 0;
    const bool displayFlying = g_player->abilities.flying || s_characterDisplayTimer > 0;
    if (!bDisplayGui || !(displayCrouch || displaySprint || displayFlying)) return;

    EntityRenderDispatcher *dispatcher = EntityRenderDispatcher::instance;
    EntityRenderer *renderer = dispatcher ? dispatcher->getRenderer(g_player) : nullptr;
    int w = 0, h = 0;
    mcle_metal_current_size(&w, &h);
    if (!renderer || w <= 0 || h <= 0) return;

    // Minecraft is 1280x720 on a 16:9 screen, here it's as wide as the
    // screen so a gui unit stays square
    const int guiScale = mcle_settings_get(29 /*UISize*/) + 2;
    ScreenSizeCalculator ssc(nullptr, 720 * w / h, 720, guiScale);
    const int screenWidth = ssc.getWidth();
    const int screenHeight = ssc.getHeight();

    // GameRenderer::setupGuiScreen(guiScale)
    mcle_metal_clear_depth();
    mcle_glbridge_matrix_mode(0x1701 /* GL_PROJECTION */);
    mcle_glbridge_load_identity();
    mcle_glbridge_metal_ortho(0, static_cast<float>(ssc.rawWidth), static_cast<float>(ssc.rawHeight), 0, 1000, 3000);
    mcle_glbridge_matrix_mode(0x1700 /* GL_MODELVIEW */);
    mcle_glbridge_load_identity();
    mcle_glbridge_translate(0, 0, -2000);
    // The state the Gui's earlier draws leave
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);

    // The full screen safe zone, 5%
    const int iSafezoneXHalf = screenWidth / 20;
    const int iSafezoneTopYHalf = screenHeight / 20;
    const int xo = iSafezoneXHalf + 10;
    const int yo = iSafezoneTopYHalf + 10;

    glEnable(GL_RESCALE_NORMAL);
    glEnable(GL_COLOR_MATERIAL);
    glPushMatrix();
    glTranslatef(static_cast<float>(xo), static_cast<float>(yo), 50);
    float ss = 12;
    glScalef(-ss, ss, ss);
    glRotatef(180, 0, 0, 1);

    float oyr = g_player->yRot;
    float oyrO = g_player->yRotO;
    float oxr = g_player->xRot;

    float xd = -40;
    float yd = 10;

    glRotatef(45 + 90, 0, 1, 0);
    Lighting::turnOn();
    glRotatef(-45 - 90, 0, 1, 0);

    const float xRotAngle = -static_cast<float>(atan(yd / 40.0f)) * 20;
    glRotatef(xRotAngle, 1, 0, 0);
    float bodyRot = (g_player->yBodyRotO + (g_player->yBodyRot - g_player->yBodyRotO));
    glRotatef(bodyRot - (static_cast<float>(atan(xd / 40.0f)) * 20), 0, 1, 0);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    // The Gui draws with the light layer off
    Minecraft::GetInstance()->gameRenderer->turnOffLightLayer(a);

    // Set head rotation to body rotation to make head static
    g_player->yRot = bodyRot;
    g_player->yRotO = g_player->yRot;
    g_player->xRot = xRotAngle;

    dispatcher->playerRotY = 180;
    dispatcher->isGuiRender = true;
    // The renderer without postRender's flame, as Gui puts the fire out for
    // the draw
    try {
        renderer->SetItemFrame(false);
        renderer->render(g_player, 0, 0, 0, 0, 1);
    } catch (...) {
    }
    dispatcher->isGuiRender = false;

    g_player->yRot = oyr;
    g_player->yRotO = oyrO;
    g_player->xRot = oxr;
    glPopMatrix();
    Lighting::turnOff();
    glDisable(GL_RESCALE_NORMAL);
}

// GameRenderer::bobHurt: tilt when hit, roll over when dead
static void mcle_bob_hurt(float a) {
    float hurt = g_player->hurtTime - a;
    if (g_player->getHealth() <= 0) {
        float duration = g_player->deathTime + a;
        mcle_glbridge_rotate(40 - (40 * 200) / (duration + 200), 0, 0, 1);
    }
    if (hurt < 0 || g_player->hurtDuration <= 0) return;
    hurt /= g_player->hurtDuration;
    hurt = (float)Mth::sin(hurt * hurt * hurt * hurt * PI);
    // The client player's hurtDir, which handleEntityEvent's HURT zeroes.
    // Only the server's own hurt aims it.
    float rr = 0;
    mcle_glbridge_rotate(-rr, 0, 1, 0);
    mcle_glbridge_rotate(-hurt * 14, 0, 0, 1);
    mcle_glbridge_rotate(+rr, 0, 1, 0);
}

// GameRenderer::bobView, the walking bob
static void mcle_bob_view(float a) {
    float wda = g_player->walkDist - g_player->walkDistO;
    float b = -(g_player->walkDist + wda * a);
    float bob = g_player->oBob + (g_player->bob - g_player->oBob) * a;
    float tilt = g_player->oTilt + (g_player->tilt - g_player->oTilt) * a;
    mcle_glbridge_translate((float)Mth::sin(b * PI) * bob * 0.5f, -fabsf((float)Mth::cos(b * PI) * bob), 0);
    mcle_glbridge_rotate((float)Mth::sin(b * PI) * bob * 3, 0, 0, 1);
    mcle_glbridge_rotate(fabsf((float)Mth::cos(b * PI - 0.2f) * bob) * 5, 1, 0, 0);
    mcle_glbridge_rotate(tilt, 1, 0, 0);
}

// The view bob runs with the setting on, not flying, and not on a skin
// whose animation turns the legs or the bobbing off
static bool mcle_view_bob_on() {
    const unsigned int noBob = (1u << HumanoidModel::eAnim_NoLegAnim) | (1u << HumanoidModel::eAnim_NoBobbing);
    return mcle_settings_get(8 /*ViewBob*/) && !g_player->abilities.flying &&
           (g_player->getAnimOverrideBitmask() & noBob) == 0;
}

// Where the world frame's time goes, logged with frame_ms every 60 frames:
// setup, sky+cull, solid chunks, entities, opaque particles, water,
// translucent particles to clouds, the hand to the end
static double s_worldPhaseMs[8];
static int s_worldPhaseFrames = 0;
static std::chrono::steady_clock::time_point s_worldPhaseMark;
static void mcle_world_phase(int phase) {
    const auto now = std::chrono::steady_clock::now();
    if (phase > 0) s_worldPhaseMs[phase - 1] += std::chrono::duration<double, std::milli>(now - s_worldPhaseMark).count();
    s_worldPhaseMark = now;
}

extern "C" void mcle_world_drive_renderer(void) {
    std::lock_guard<std::mutex> frameLock(g_frameMutex);
    mcle_world_phase(0);
    if (g_initState != kStateTicking || !g_levelRenderer || !g_player) return;
    if (g_levelChanging.load()) return;

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
        MCLE_LOG("RTLS_CKPT step=9 Compression");
        Compression::CreateNewThreadStorage();
        s_renderThreadTlsReady = true;
        MCLE_LOG("mcle_world_drive_renderer: render-thread full TLS init done");
    }

    // Minecraft::tick's `if (!pause) textures->tick(bUpdateTextures)`, once
    // for the game ticks since the last frame: the animated textures cycle
    // into the atlases, which upload when they're next bound. A paused game
    // doesn't tick, so neither do these.
    {
        static uint64_t s_texturesTickNs = 0;
        const uint64_t tickNs = g_lastSimTickNs.load(std::memory_order_relaxed);
        Minecraft *mcTex = Minecraft::GetInstance();
        if (tickNs != s_texturesTickNs && mcTex && mcTex->textures) {
            s_texturesTickNs = tickNs;
            try {
                mcTex->textures->tick(true);
            } catch (...) {
                MCLE_LOG("mcle_world_drive_renderer: textures tick threw");
            }
        }
    }

    try {
        // G3d-step3: set up projection + view via the GL matrix stack
        // each frame. Once upstream renderSky/renderClouds drive replays
        // naturally (G3e), they'll layer their per-pass glPush/Translate
        // /Rotate calls on top of this base camera.
        int vw = 0, vh = 0;
        mcle_metal_current_size(&vw, &vh);
        const float aspect = (vh > 0) ? ((float)vw / (float)vh) : 1.0f;
        // Minecraft's backbuffer size. ItemRenderer::blit scales item icons
        // by width / width_phys, which were never set, so every 2D item
        // icon came out NaN.
        if (Minecraft *mcSize = Minecraft::GetInstance()) {
            mcSize->width = mcSize->width_phys = vw;
            mcSize->height = mcSize->height_phys = vh;
        }

        // Partial tick. The sim runs at 20Hz and rendering interpolates
        // between its last two ticks, alpha = (now - lastTick) / 50ms.
        float frame_partial_tick = 1.0f;
        {
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
        const float a = frame_partial_tick;

        // GameRenderer::getFov: the FOV setting, the sprint / fly / bow
        // factor from tickFov, the death zoom and narrower under water
        float fov = 70.0f + mcle_settings_get(4 /*FOV*/) * 40.0f / 100.0f;
        fov *= mcle_player_fov_factor(a);
        if (g_player->getHealth() <= 0) {
            float duration = g_player->deathTime + a;
            fov /= ((1 - 500 / (duration + 500)) * 2.0f + 1);
        }
        {
            double cx = g_player->xo + (g_player->x - g_player->xo) * a;
            double cy = g_player->yo + (g_player->y - g_player->yo) * a + 1.62 - g_player->heightOffset;
            double cz = g_player->zo + (g_player->z - g_player->zo) * a;
            int t = g_player->level->getTile(Mth::floor(cx), Mth::floor(cy), Mth::floor(cz));
            if (t != 0 && Tile::tiles[t] && Tile::tiles[t]->material == Material::water) fov = fov * 60 / 70;
        }

        // GameRenderer::setupCamera, first person
        mcle_glbridge_matrix_mode(0x1701 /* GL_PROJECTION */);
        mcle_glbridge_load_identity();
        mcle_glbridge_metal_perspective(fov, aspect, 0.05f, mcle_render_distance() * 2);

        mcle_glbridge_matrix_mode(0x1700 /* GL_MODELVIEW */);
        mcle_glbridge_load_identity();

        mcle_bob_hurt(a);
        if (mcle_view_bob_on()) mcle_bob_view(a);

        // The portal skews the view as its overlay fades in
        {
            float pt = mcle_portal_client_time(a);
            if (pt > 0) {
                int multiplier = 20;
                if (g_player->hasEffect(MobEffect::confusion)) {
                    multiplier = 7;
                }

                float skew = 5 / (pt * pt + 5) - pt * 0.04f;
                skew *= skew;
                // GameRenderer's _tick
                const int _tick = mcle_renderer_tick();
                glRotatef((_tick + a) * multiplier, 0, 1, 1);
                glScalef(1 / skew, 1, 1);
                glRotatef(-(_tick + a) * multiplier, 0, 1, 1);
            }
        }

        // GameRenderer::moveCameraToPlayer. Rotation interpolates between
        // ticks like everything else, the look turns once a tick in
        // mcle_player_move_tick. No camera roll outside cut scenes.
        {
            const int thirdPersonView = mcle_third_person_view();
            // ServerPlayer's heightOffset is 0 (4J, ServerPlayer.cpp:119), so
            // this lifts the camera to eye level
            float heightOffset = g_player->heightOffset - 1.62f;
            double x = g_player->xo + (g_player->x - g_player->xo) * a;
            double y = g_player->yo + (g_player->y - g_player->yo) * a - heightOffset;
            double z = g_player->zo + (g_player->z - g_player->zo) * a;

            if (g_player->isSleeping()) {
                heightOffset += 1.0;
                mcle_glbridge_translate(0.0f, 0.3f, 0);
                int t = g_player->level->getTile(Mth::floor(g_player->x), Mth::floor(g_player->y), Mth::floor(g_player->z));
                if (t == Tile::bed_Id) {
                    int data = g_player->level->getData(Mth::floor(g_player->x), Mth::floor(g_player->y),
                                                        Mth::floor(g_player->z));
                    int direction = data & 3;
                    mcle_glbridge_rotate(static_cast<float>(direction) * 90, 0.0f, 1.0f, 0.0f);
                }
                mcle_glbridge_rotate(g_player->yRotO + (g_player->yRot - g_player->yRotO) * a + 180, 0, -1, 0);
                mcle_glbridge_rotate(g_player->xRotO + (g_player->xRot - g_player->xRotO) * a, -1, 0, 0);
            } else if (thirdPersonView) {
                // thirdDistance and thirdDistanceO stay at 4
                double cameraDist = 4;
                float yRot = g_player->yRotO + (g_player->yRot - g_player->yRotO) * a;
                float xRot = g_player->xRotO + (g_player->xRot - g_player->xRotO) * a;

                // Reverse y rotation - only used for the collision that works
                // out the view distance, the rotation itself is below
                if (thirdPersonView == 2) yRot += 180.0f;

                double xd = -Mth::sin(yRot / 180 * PI) * Mth::cos(xRot / 180 * PI) * cameraDist;
                double zd = Mth::cos(yRot / 180 * PI) * Mth::cos(xRot / 180 * PI) * cameraDist;
                double yd = -Mth::sin(xRot / 180 * PI) * cameraDist;
                if (thirdPersonView == 2) yd = Mth::sin(xRot / 180 * PI) * cameraDist;

                for (int i = 0; i < 8; i++) {
                    float xo = static_cast<float>((i & 1) * 2 - 1);
                    float yo = static_cast<float>(((i >> 1) & 1) * 2 - 1);
                    float zo = static_cast<float>(((i >> 2) & 1) * 2 - 1);
                    xo *= 0.1f;
                    yo *= 0.1f;
                    zo *= 0.1f;
                    HitResult *hr = g_player->level->clip(Vec3::newTemp(x + xo, y + yo, z + zo),
                                                          Vec3::newTemp(x - xd + xo, y - yd + yo, z - zd + zo));
                    if (hr != nullptr) {
                        double dist = hr->pos->distanceTo(Vec3::newTemp(x, y, z));
                        if (dist < cameraDist) cameraDist = dist;
                        delete hr;
                    }
                }
                mcle_glbridge_translate(0, 0, static_cast<float>(-cameraDist));
            } else {
                mcle_glbridge_translate(0.0f, 0.0f, -0.1f);
            }

            // yaw + 180 because Minecraft yaw 0 faces south and the camera
            // looks down -Z, the front view looks back at the player
            float pitch = g_player->xRotO + (g_player->xRot - g_player->xRotO) * a;
            if (thirdPersonView == 2) pitch = -pitch;
            mcle_glbridge_rotate(pitch, 1.0f, 0.0f, 0.0f);
            if (thirdPersonView == 2) {
                mcle_glbridge_rotate(g_player->yRotO + (g_player->yRot - g_player->yRotO) * a, 0.0f, 1.0f, 0.0f);
            } else {
                mcle_glbridge_rotate(g_player->yRotO + (g_player->yRot - g_player->yRotO) * a + 180.0f, 0.0f, 1.0f, 0.0f);
            }
            mcle_glbridge_translate(0.0f, heightOffset, 0.0f);

            x = g_player->xo + (g_player->x - g_player->xo) * a;
            y = g_player->yo + (g_player->y - g_player->yo) * a - heightOffset;
            z = g_player->zo + (g_player->z - g_player->zo) * a;
            g_isInClouds = g_levelRenderer->isInCloud(x, y, z, a);
        }
        // Player translation is applied INSIDE upstream LevelRenderer::renderChunks
        // via glPushMatrix; glTranslatef(-xOff, -yOff, -zOff). Adding it here too
        // would double-translate chunks. Sky/clouds/sun apply their own translations
        // per-pass so they're unaffected.

        // Chunk-rebuild dispatch was here on the render thread; it
        // internally blocks at WaitForAll(INFINITE) waiting for the 7
        // worker threads which made the render frame stall ~200-500ms.
        // Moved to the sim thread (mcle_game_tick body) so the render
        // thread never blocks on chunk work.

        // GameRenderer::setupClearColor, then setupFog(0, a) and GL_FOG on
        // for the terrain
        try {
            mcle_setup_clear_color(frame_partial_tick);
            mcle_setup_fog(0, frame_partial_tick);
        } catch (...) {
            static int s_fogErr = 0;
            if (s_fogErr++ < 3) MCLE_LOG("setupFog threw");
        }
        mcle_glbridge_set_fog_enabled(1);

        // GameRenderer::updateLightTexture. 256 entries indexed by
        // (skyLevel << 4) | blockLevel, built from the player's dimension.
        if (g_initState == kStateTicking && g_player && g_player->level) {
            try {
                Level *level = g_player->level;
                const float skyDarken1 = level->getSkyDarken(1.0f);
                const float blr = g_lightBlr.load(std::memory_order_relaxed);
                const float darkenWorldAmount = g_darkenWorldAmount.load(std::memory_order_relaxed);
                const float darkenWorldAmountO = g_darkenWorldAmountO.load(std::memory_order_relaxed);
                for (int i = 0; i < 256; i++) {
                    const float darken = skyDarken1 * 0.95f + 0.05f;
                    float sky = level->dimension->brightnessRamp[i / 16] * darken;
                    const float block = level->dimension->brightnessRamp[i % 16] * (blr * 0.1f + 1.5f);

                    if (level->skyFlashTime < 0) {
                        sky = level->dimension->brightnessRamp[i / 16];
                    }

                    const float rs = sky * (skyDarken1 * 0.65f + 0.35f);
                    const float gs = sky * (skyDarken1 * 0.65f + 0.35f);
                    const float bs = sky;

                    const float rb = block;
                    const float gb = block * ((block * 0.6f + 0.4f) * 0.6f + 0.4f);
                    const float bb = block * ((block * block) * 0.6f + 0.4f);

                    float _r = (rs + rb);
                    float _g = (gs + gb);
                    float _b = (bs + bb);

                    _r = _r * 0.96f + 0.03f;
                    _g = _g * 0.96f + 0.03f;
                    _b = _b * 0.96f + 0.03f;

                    if (darkenWorldAmount > 0) {
                        const float amount = darkenWorldAmountO + (darkenWorldAmount - darkenWorldAmountO) * frame_partial_tick;
                        _r = _r * (1.0f - amount) + (_r * 0.7f) * amount;
                        _g = _g * (1.0f - amount) + (_g * 0.6f) * amount;
                        _b = _b * (1.0f - amount) + (_b * 0.6f) * amount;
                    }

                    if (level->dimension->id == 1) {
                        _r = (0.22f + rb * 0.75f);
                        _g = (0.28f + gb * 0.75f);
                        _b = (0.25f + bb * 0.75f);
                    }

                    if (g_player->hasEffect(MobEffect::nightVision)) {
                        // GameRenderer::getNightVisionScale
                        int duration = g_player->getEffect(MobEffect::nightVision)->getDuration();
                        float scale = 1.0f;
                        if (duration <= (SharedConstants::TICKS_PER_SECOND * 10)) {
                            float flash = std::max(0.0f, (float)duration - frame_partial_tick);
                            scale = .7f + Mth::sin(flash * PI * .05f) * .3f;
                        }
                        float dist = 1.0f / _r;
                        if (dist > (1.0f / _g)) dist = (1.0f / _g);
                        if (dist > (1.0f / _b)) dist = (1.0f / _b);
                        _r = _r * (1.0f - scale) + (_r * dist) * scale;
                        _g = _g * (1.0f - scale) + (_g * dist) * scale;
                        _b = _b * (1.0f - scale) + (_b * dist) * scale;
                    }

                    if (_r > 1.0f) _r = 1.0f;
                    if (_r < 0.0f) _r = 0.0f;
                    if (_g > 1.0f) _g = 1.0f;
                    if (_g < 0.0f) _g = 0.0f;
                    if (_b > 1.0f) _b = 1.0f;
                    if (_b < 0.0f) _b = 0.0f;
                    mcle_lightmap_set_entry(i, _r, _g, _b);
                }
                mcle_lightmap_upload();
            } catch (...) {
                // Leave previous frame's lightmap in place on any throw,
                // safer than blanking the world.
            }
        }

        // GameRenderer::renderLevel from setupCamera on. The chunk lists go
        // out through LevelRenderer::render (CBuffCall), opaque layer first,
        // the water layer after the entities and particles.
        Minecraft *mc = Minecraft::GetInstance();
        std::shared_ptr<LivingEntity> cameraEntity = g_player;
        const double xOff = cameraEntity->xOld + (cameraEntity->x - cameraEntity->xOld) * a;
        const double yOff = cameraEntity->yOld + (cameraEntity->y - cameraEntity->yOld) * a;
        const double zOff = cameraEntity->zOld + (cameraEntity->z - cameraEntity->zOld) * a;
        const bool underWater = g_player->isUnderLiquid(Material::water);
        int hx = 0, hy = 0, hz = 0, hf = 0;
        const bool haveHit = mcle_player_hit_tile(&hx, &hy, &hz, &hf) != 0;

        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        mcle_world_phase(1);
        Frustum::getFrustum();
        if (mc->options->viewDistance < 2) {
            try { mcle_setup_fog(-1, a); } catch (...) {}
            try { g_levelRenderer->renderSky(a); } catch (...) {}
        }
        glEnable(GL_FOG);
        try { mcle_setup_fog(1, a); } catch (...) {}

        FrustumCuller frustObj;
        Culler *frustum = &frustObj;
        frustum->prepare(xOff, yOff, zOff);
        try { g_levelRenderer->cull(frustum, a); } catch (...) {}

        mcle_world_phase(2);
        // prepareAndRenderClouds, below the clouds' height
        auto renderClouds = [&]() {
            if (!mc->options->isCloudsOn()) return;
            glPushMatrix();
            try { mcle_setup_fog(0, a); } catch (...) {}
            glEnable(GL_FOG);
            try { g_levelRenderer->renderClouds(a); } catch (...) {}
            glDisable(GL_FOG);
            try { mcle_setup_fog(1, a); } catch (...) {}
            glPopMatrix();
        };
        if (cameraEntity->y < Level::genDepth) renderClouds();
        Frustum::getFrustum();

        try { mcle_setup_fog(0, a); } catch (...) {}
        glEnable(GL_FOG);
        if (mc->textures) {
            try { mc->textures->bindTexture(&TextureAtlas::LOCATION_BLOCKS); } catch (...) {}
        }
        Lighting::turnOff();
        {
            using hrclock2 = std::chrono::steady_clock;
            const auto renderStart = hrclock2::now();
            try {
                g_levelRenderer->render(cameraEntity, 0, (double)a, /*updateChunks*/false);
            } catch (...) {}
            const auto renderMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      hrclock2::now() - renderStart).count();
            if (renderMs > 100) {
                MCLE_LOG("SLOW_RENDER LevelRenderer::render took=%lldms", (long long)renderMs);
            }
        }

        mcle_world_phase(3);
        // Entities don't use the "a" factor for culling, their AABBs are
        // already at the new position
        if (EntityRenderDispatcher::instance && TileEntityRenderDispatcher::instance) {
            static Vec3 *s_cameraPos = Vec3::newPermanent(0, 0, 0);
            try {
                Lighting::turnOn();
                frustum->prepare(cameraEntity->x, cameraEntity->y, cameraEntity->z);
                Vec3 *cameraPosTemp = cameraEntity->getPos(a);
                s_cameraPos->x = cameraPosTemp->x;
                s_cameraPos->y = cameraPosTemp->y;
                s_cameraPos->z = cameraPosTemp->z;
                g_levelRenderer->renderEntities(s_cameraPos, frustum, a);
            } catch (...) {
                static int s_entErr = 0;
                if (s_entErr++ < 3) MCLE_LOG("renderEntities threw");
            }
        }

        mcle_world_phase(4);
        ParticleEngine *particleEngine = mc->particleEngine;
        if (particleEngine) {
            try {
                std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
                Camera::prepare(g_player, mcle_third_person_view() == 2);
                mc->gameRenderer->turnOnLightLayer(a);
                particleEngine->renderLit(cameraEntity, a, ParticleEngine::OPAQUE_LIST);
                Lighting::turnOff();
                try { mcle_setup_fog(0, a); } catch (...) {}
                particleEngine->render(cameraEntity, a, ParticleEngine::OPAQUE_LIST);
            } catch (...) {
                static int s_particleErr = 0;
                if (s_particleErr++ < 3) MCLE_LOG("particle render threw");
            }
            mc->gameRenderer->turnOffLightLayer(a);
        }
        Lighting::turnOff();

        if (haveHit && underWater) {
            try {
                HitResult hit(hx, hy, hz, hf, Vec3::newTemp(hx, hy, hz));
                glDisable(GL_ALPHA_TEST);
                g_levelRenderer->renderHit(g_player, &hit, 0, g_player->inventory->getSelected(), a);
                glEnable(GL_ALPHA_TEST);
            } catch (...) {}
        }

        mcle_world_phase(5);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(true);
        try { mcle_setup_fog(0, a); } catch (...) {}
        glEnable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        if (mc->textures) {
            try { mc->textures->bindTexture(&TextureAtlas::LOCATION_BLOCKS); } catch (...) {}
        }
        // The water: depth only first, then drawn over it, so only the
        // nearest water surface shows
        try {
            glBlendFunc(GL_ZERO, GL_ONE);
            int visibleWaterChunks = g_levelRenderer->render(cameraEntity, 1, (double)a, /*updateChunks*/false);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (visibleWaterChunks > 0) {
                g_levelRenderer->render(cameraEntity, 1, (double)a, /*updateChunks*/false);
            }
        } catch (...) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        mcle_world_phase(6);
        // Translucent particles go after the water
        if (particleEngine) {
            try {
                std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
                Lighting::turnOn();
                mc->gameRenderer->turnOnLightLayer(a);
                particleEngine->renderLit(cameraEntity, a, ParticleEngine::TRANSLUCENT_LIST);
                Lighting::turnOff();
                try { mcle_setup_fog(0, a); } catch (...) {}
                particleEngine->render(cameraEntity, a, ParticleEngine::TRANSLUCENT_LIST);
            } catch (...) {
                static int s_particleErr2 = 0;
                if (s_particleErr2++ < 3) MCLE_LOG("translucent particle render threw");
            }
            mc->gameRenderer->turnOffLightLayer(a);
        }
        Lighting::turnOff();

        glDepthMask(true);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        // The selection box round the targeted block, picked on the sim
        // thread this tick
        if (haveHit && !underWater) {
            try {
                HitResult hit(hx, hy, hz, hf, Vec3::newTemp(hx, hy, hz));
                glDisable(GL_ALPHA_TEST);
                g_levelRenderer->renderHitOutline(g_player, &hit, 0, a);
                glEnable(GL_ALPHA_TEST);
            } catch (...) {
                static int s_hitErr = 0;
                if (s_hitErr++ < 3) MCLE_LOG("renderHitOutline threw");
            }
        }

        // The cracks on blocks being broken
        try {
            std::lock_guard<std::mutex> lock(g_destroyingBlocksMutex);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            g_levelRenderer->renderDestroyAnimation(Tesselator::getInstance(), g_player, a);
            glDisable(GL_BLEND);
        } catch (...) {
            glDisable(GL_BLEND);
            static int s_crackErr = 0;
            if (s_crackErr++ < 3) MCLE_LOG("renderDestroyAnimation threw");
        }

        if (cameraEntity->y >= Level::genDepth) renderClouds();
        mcle_world_phase(7);

        // Rain and snow after the clouds so they blend onto them
        try {
            mcle_setup_fog(0, a);
            glEnable(GL_FOG);
            mcle_weather_render(g_player, g_player->level, a);
            glDisable(GL_FOG);
        } catch (...) {
            glDisable(GL_FOG);
            static int s_rainErr = 0;
            if (s_rainErr++ < 3) MCLE_LOG("renderSnowAndRain threw");
        }

        // Player position for the logs
        {
            static int s_posLog = 0;
            if ((s_posLog++ % 60) == 0) {
                MCLE_LOG("MV_CLOUD log=%d player_x=%.2f y=%.2f z=%.2f xRot=%.1f yRot=%.1f", s_posLog,
                         (float)g_player->x, (float)g_player->y, (float)g_player->z, g_player->xRot, g_player->yRot);
            }
        }

        // renderLevel ends with glClear(GL_DEPTH_BUFFER_BIT) and
        // renderItemInHand, so the hand is never inside a wall
        if (g_itemInHand) {
            mcle_metal_clear_depth();
            bool renderHand = true;
            std::shared_ptr<ItemInstance> item = g_player->inventory->getSelected();
            if (!(item && item->getItem()->id == Item::map_Id) && mcle_settings_get(26 /*DisplayHand*/) == 0) {
                renderHand = false;
            }

            // getFov(a, false): no FOV setting or sprint factor for the hand
            float handFov = 70.0f;
            if (g_player->getHealth() <= 0) {
                float duration = g_player->deathTime + a;
                handFov /= ((1 - 500 / (duration + 500)) * 2.0f + 1);
            }
            mcle_glbridge_matrix_mode(0x1701 /* GL_PROJECTION */);
            mcle_glbridge_load_identity();
            mcle_glbridge_metal_perspective(handFov, aspect, 0.05f, mcle_render_distance() * 2);
            mcle_glbridge_matrix_mode(0x1700 /* GL_MODELVIEW */);
            mcle_glbridge_load_identity();

            try {
                std::lock_guard<std::mutex> lock(g_itemInHandMutex);
                glPushMatrix();
                mcle_bob_hurt(a);
                if (mcle_view_bob_on()) mcle_bob_view(a);
                if (renderHand && mcle_third_person_view() == 0 && !g_player->isSleeping()) {
                    mc->gameRenderer->turnOnLightLayer(a);
                    try {
                        g_itemInHand->render(a);
                    } catch (...) {
                        mc->gameRenderer->turnOffLightLayer(a);
                        throw;
                    }
                    mc->gameRenderer->turnOffLightLayer(a);
                }
                glPopMatrix();
                if (mcle_third_person_view() == 0 && !g_player->isSleeping()) {
                    g_itemInHand->renderScreenEffect(a);
                    mcle_bob_hurt(a);
                }
                if (mcle_view_bob_on()) mcle_bob_view(a);
            } catch (...) {
                static int s_handErr = 0;
                if (s_handErr++ < 3) MCLE_LOG("renderItemInHand threw");
            }
        }

        mcle_world_phase(8);
        if (++s_worldPhaseFrames >= 60) {
            MCLE_LOG("world_ms setup=%.2f skycull=%.2f solid=%.2f ent=%.2f part=%.2f water=%.2f trans=%.2f hand=%.2f",
                     s_worldPhaseMs[0] / 60, s_worldPhaseMs[1] / 60, s_worldPhaseMs[2] / 60, s_worldPhaseMs[3] / 60,
                     s_worldPhaseMs[4] / 60, s_worldPhaseMs[5] / 60, s_worldPhaseMs[6] / 60, s_worldPhaseMs[7] / 60);
            for (double &ms : s_worldPhaseMs) ms = 0;
            s_worldPhaseFrames = 0;
        }
        // GameRenderer::render: then the Gui over the world
        try {
            mcle_gui_overlays(a);
            mcle_gui_animated_character(a);
        } catch (...) {
            static int s_guiErr = 0;
            if (s_guiErr++ < 3) MCLE_LOG("gui overlays threw");
        }

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
    return (g_initState == kStateTicking && g_levels[0] != nullptr && g_renderReady) ? 1 : 0;
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

    // setupClearColor's colour, from the last frame
    if (g_haveClearColor.load(std::memory_order_acquire) && g_initState == kStateTicking) {
        if (r) *r = g_fr;
        if (g) *g = g_fg;
        if (b) *b = g_fb;
        return;
    }
    if (g_initState == kStateTicking && g_player && g_player->level) {
        try {
            Vec3 *sky = g_player->level->getSkyColor(g_player, /*alpha*/1.0f);
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

// Leaving the world, what IUIScene_PauseMenu::_ExitWorld comes to for a
// single player host. The player leaves like PlayerList::remove does on
// disconnect, MinecraftServer::HaltServer -> stopServer saves if asked and
// deletes the server levels, players and settings, and
// Minecraft::setLevel(nullptr) lets go of the levels on the client side.
// What Minecraft keeps for the whole process (textures, LevelRenderer, Gui,
// the particle and sound engines, the renderers) stays for the next world.
static void mcle_exit_world(bool save) {
    MCLE_LOG("EXIT_WORLD: start save=%d", save ? 1 : 0);

    // Ticks and frames check this and stay out from here on. Leaving from
    // the menus marks it on the main thread before this thread starts, so
    // only a call from elsewhere waits for the main thread's current pass.
    if (g_initState != kStateExiting) {
        g_initState = kStateExiting;
        if (!pthread_main_np()) dispatch_sync_f(dispatch_get_main_queue(), nullptr, [](void *) {});
    }
    // The tick, the chunk update and the frame in flight
    { std::lock_guard<std::mutex> lock(g_simTickMutex); }
    { std::lock_guard<std::mutex> lock(g_chunkUpdateMutex); }
    { std::lock_guard<std::mutex> lock(g_frameMutex); }

    // A background save in progress finishes first
    std::lock_guard<std::mutex> life(g_worldLifeMutex);
    MCLE_LOG("EXIT_WORLD: world stopped");

    // stopServer: saveAll, saveAllChunks, the game rules and saveToDisc
    // when saving on exit
    if (save) {
        try { mcle_save_world(true); } catch (...) { MCLE_LOG("EXIT_WORLD: save threw"); }
    }
    // _ExitWorld unloads the game rules once the server has stopped
    app.m_gameRules.unloadCurrentGameRules();

    // eAppAction_ExitWorld: gui->clearMessages()
    {
        std::lock_guard<std::mutex> lock(s_guiMutex);
        if (g_gui) g_gui->clearMessages();
    }

    // Minecraft::setLevel(nullptr)
    Minecraft *mc = Minecraft::GetInstance();
    if (mc) {
        {
            std::lock_guard<std::mutex> lock(g_itemInHandMutex);
            for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
                if (mc->localitemInHandRenderers[i] != nullptr) mc->localitemInHandRenderers[i]->reset();
            }
        }
        if (g_levelRenderer) {
            if (g_clientLevel && g_clientLevelListener) g_clientLevel->removeListener(g_clientLevelListener);
            try {
                for (int p = 0; p < XUSER_MAX_COUNT; ++p) g_levelRenderer->setLevel(p, nullptr);
            } catch (...) {
                MCLE_LOG("EXIT_WORLD: LevelRenderer::setLevel threw");
            }
        }
        {
            std::lock_guard<std::recursive_mutex> lock(g_particleMutex);
            if (mc->particleEngine) mc->particleEngine->setLevel(nullptr);
        }
        mc->level = nullptr;
        mc->localplayers[0] = nullptr;
        mc->player = nullptr;
        mc->cameraTargetPlayer = nullptr;
        if (EntityRenderDispatcher::instance) EntityRenderDispatcher::instance->cameraEntity = nullptr;
        if (TileEntityRenderDispatcher::instance) TileEntityRenderDispatcher::instance->cameraEntity = nullptr;
    }
    delete g_clientLevelListener;
    g_clientLevelListener = nullptr;
    g_clientLevel = nullptr;
    mcle_portal_client_reset();
    mcle_sound_unlisten();
    mcle_sound_stop_music();
    mcle_player_input_reset();
    mcle_hud_reset();
    // removeLocalPlayer: out of the tutorial, and its game mode deleted
    try {
        mcle_tutorial_end();
    } catch (...) {
        MCLE_LOG("EXIT_WORLD: tutorial threw");
    }

    // PlayerList::remove, as the player's connection closing does. It was
    // already saved above when saving.
    std::shared_ptr<ServerPlayer> player = std::move(g_player);
    if (player) {
        try {
            ServerLevel *level = player->getLevel();
            if (level) {
                if (player->riding != nullptr) level->removeEntityImmediately(player->riding);
                level->getTracker()->removeEntity(player);
                level->removeEntity(player);
                level->getChunkMap()->remove(player);
            }
        } catch (...) {
            MCLE_LOG("EXIT_WORLD: removing the player threw");
        }
        player->connection = nullptr;
        // The game mode references back to the player
        delete player->gameMode;
        player->gameMode = nullptr;
        player.reset();
    }
    g_playerGameMode = nullptr;

    // stopServer: remove the server levels
    for (int i = 0; i < 3; ++i) {
        ServerLevel *level = g_levels[i];
        g_levels[i] = nullptr;
        if (g_server && i < (int)g_server->levels.length) g_server->levels[i] = nullptr;
        if (level) {
            try { delete level; } catch (...) { MCLE_LOG("EXIT_WORLD: level[%d] dtor threw", i); }
        }
    }
    if (g_server) {
        // No ServerConnection is made on iOS
        delete g_server->getPlayers();
        g_server->setPlayers(nullptr);
        delete g_server->settings;
        g_server->settings = nullptr;
        MinecraftServer::server = nullptr;
        try { delete g_server; } catch (...) {}
        g_server = nullptr;
    }
    // _ExitWorld: MinecraftServer::resetFlags() once the level is gone
    MinecraftServer::resetFlags();

    // DirectoryLevelStorage's destructor deletes the save file it was given,
    // and ~ConsoleSaveFileOriginal lets go of a thumbnail no save took
    g_levelStorage.reset();
    g_saveFile = nullptr;
    app.GetSaveThumbnail(nullptr, nullptr);
    if (g_levelSource) {
        try { delete g_levelSource; } catch (...) {}
        g_levelSource = nullptr;
    }

    // Ready for the next world
    g_renderReady = false;
    g_haveClearColor.store(false, std::memory_order_release);
    g_loadStage = 0;
    g_loadPercent = 0;
    g_gamePaused.store(false, std::memory_order_relaxed);
    g_playerDead.store(false, std::memory_order_relaxed);
    g_respawnRequested.store(false, std::memory_order_relaxed);
    // Leaving from the death menu leaves no menu up for the next world
    g_menuDisplayed.store(false);
    g_pendingLevelId.clear();
    g_newWorld = NewWorldParams();
    g_loadIsNew = false;
    g_initStarted = false;
    g_initState = kStateUnstarted;
    MCLE_LOG("EXIT_WORLD: done");
}

// 0 nothing going on, 1 leaving the world, 2 left it
static std::atomic<int> g_exitWorldState{0};

// IUIScene_PauseMenu::ExitWorldThreadProc: the exit runs off the main
// thread while FullscreenProgress shows "Exiting game"
extern "C" int mcle_game_exit_world(int save) {
    if (g_initState != kStateTicking) return 0;
    int expected = 0;
    if (!g_exitWorldState.compare_exchange_strong(expected, 1)) return 0;
    // Called on the main thread, which stays out of the world from here
    g_initState = kStateExiting;
    pthread_t t;
    if (pthread_create(&t, nullptr, [](void *arg) -> void * {
            const bool saving = arg != nullptr;
            // The world's thread local pools, like any thread that touches
            // it, given back when the thread ends
            mcle_world_thread_begin();
            Tesselator::CreateNewThreadStorage(16 * 1024);
            Chunk::CreateNewThreadStorage();
            mcle_exit_world(saving);
            Chunk::ReleaseThreadStorage();
            mcle_world_thread_end();
            g_exitWorldState.store(2, std::memory_order_release);
            return nullptr;
        }, save ? reinterpret_cast<void *>(1) : nullptr) != 0) {
        g_initState = kStateTicking;
        g_exitWorldState.store(0);
        return 0;
    }
    pthread_detach(t);
    return 1;
}

// 1 once the world is gone, then back to 0 for the next exit
extern "C" int mcle_game_exit_world_done(void) {
    int expected = 2;
    return g_exitWorldState.compare_exchange_strong(expected, 0) ? 1 : 0;
}

extern "C" void mcle_game_shutdown(void) {
    MCLE_LOG("mcle_game_shutdown: tearing down");
    if (g_initState == kStateTicking) mcle_exit_world(false);
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
