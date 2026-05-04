// Phase E link-stub TU.
//
// When mcle_ios_game's MCLEGameLoop.cpp calls `new ServerLevel(...)` the
// linker walks the live transitive set out of the probe archive: ServerLevel
// constructs PlayerChunkMap, ServerChunkCache, TrackedEntity; ServerChunkCache
// stores a ChunkSource; LevelStorage paths reach ConsoleSaveFileOriginal,
// File, Compression; LevelData is read from disk; etc.
//
// Most of those owning .cpp files do not yet pass auto-probe (Win32 APIs,
// missing headers, redefinition cascades). Until they're unblocked, this TU
// gives the linker a body for every symbol the live set demands so the .ipa
// finishes linking and can boot. Bodies are no-ops returning sensible
// defaults; runtime save loading idles when prepareLevel() returns null,
// which the bootstrap already handles.
//
// Each block is grouped by the class it belongs to. When the owning .cpp
// becomes compilable (Phase F+), drop the corresponding block and add the
// real source to mcle_world_probe's source list.

#include "iOS_stdafx.h"

#include "AddEntityPacket.h"
#include "AddPlayerPacket.h"
#include "AnimatePacket.h"
#include "ContainerOpenPacket.h"
#include "EntityEvent.h"
#include "TileEditorOpenPacket.h"
#include "Biome.h"
#include "GameEventPacket.h"
#include "PotionBrewing.h"
#include "WeighedTreasure.h"
#include "ChunkPos.h"
#include "Compression.h"
#include "ConsoleSaveFile.h"
#include "ConsoleSaveFileOriginal.h"
#include "ConsoleSavePath.h"
#include "DirectoryLevelStorage.h"
#include "EnchantedBookItem.h"
#include "File.h"
#include "FileHeader.h"
#include "FlatGeneratorInfo.h"
#include "Item.h"
#include "LevelData.h"
#include "LevelSettings.h"
#include "RandomLevelSource.h"
#include "SavedDataStorage.h"
#include "SetEntityLinkPacket.h"
#include "C4JThread.h"
#include "../Minecraft.Client/PS3/PS3Extras/ShutdownManager.h"
#include "../Minecraft.Client/StatsCounter.h"
#include "EntitySelector.h"
#include "QuartzBlockTile.h"
#include "Sapling.h"
#include "SkullTileEntity.h"
#include "TallGrass.h"
#include "Tile.h"
#include "TileEntityDataPacket.h"
#include "../Minecraft.Client/Minecraft.h"
#include "../Minecraft.Client/PlayerConnection.h"
#include "../Minecraft.Client/ServerPlayer.h"
#include "../Minecraft.Client/ServerChunkCache.h"
#include "../Minecraft.Client/Chunk.h"
#include "../Minecraft.Client/GameRenderer.h"

#include <memory>
#include <vector>
#include <typeinfo>

// ---------------------------------------------------------------------------
// Item statics. In-class initialisers in Item.h give the values; ODR-use
// (e.g. taking the address) needs an out-of-line definition.
// ---------------------------------------------------------------------------
const int Item::flint_Id;
const int Item::emerald_Id;
const int Item::apple_Id;
const int Item::arrow_Id;
const int Item::beef_cooked_Id;
const int Item::beef_raw_Id;
const int Item::book_Id;
const int Item::boots_chain_Id;
const int Item::boots_diamond_Id;
const int Item::boots_iron_Id;
const int Item::boots_leather_Id;
const int Item::bread_Id;
const int Item::bucket_lava_Id;
const int Item::bucket_water_Id;
const int Item::chestplate_chain_Id;
const int Item::chestplate_diamond_Id;
const int Item::chestplate_iron_Id;
const int Item::chestplate_leather_Id;
const int Item::chicken_cooked_Id;
const int Item::chicken_raw_Id;
const int Item::clock_Id;
const int Item::coal_Id;
const int Item::compass_Id;
const int Item::cookie_Id;
const int Item::diamond_Id;
const int Item::enderPearl_Id;
const int Item::expBottle_Id;
const int Item::eyeOfEnder_Id;
const int Item::fish_cooked_Id;
const int Item::flintAndSteel_Id;
const int Item::goldIngot_Id;
const int Item::hatchet_diamond_Id;
const int Item::hatchet_iron_Id;
const int Item::helmet_chain_Id;
const int Item::helmet_diamond_Id;
const int Item::helmet_iron_Id;
const int Item::helmet_leather_Id;
const int Item::hoe_diamond_Id;
const int Item::hoe_iron_Id;
const int Item::ironIngot_Id;
const int Item::leggings_chain_Id;
const int Item::leggings_diamond_Id;
const int Item::leggings_iron_Id;
const int Item::leggings_leather_Id;
const int Item::melon_Id;
const int Item::paper_Id;
const int Item::pickAxe_diamond_Id;
const int Item::pickAxe_iron_Id;
const int Item::porkChop_cooked_Id;
const int Item::porkChop_raw_Id;
const int Item::potion_Id;
const int Item::redStone_Id;
const int Item::rotten_flesh_Id;
const int Item::saddle_Id;
const int Item::seeds_melon_Id;
const int Item::seeds_pumpkin_Id;
const int Item::seeds_wheat_Id;
const int Item::shears_Id;
const int Item::shovel_diamond_Id;
const int Item::shovel_iron_Id;
const int Item::skull_Id;
const int Item::sword_diamond_Id;
const int Item::sword_iron_Id;
const int Item::wheat_Id;

// ---------------------------------------------------------------------------
// Tile statics.
// ---------------------------------------------------------------------------
const int Tile::bookshelf_Id;
const int Tile::glass_Id;
const int Tile::glowstone_Id;
const int Tile::leaves_Id;
const int Tile::stoneSlabHalf_Id;
const int Tile::wool_Id;

// ---------------------------------------------------------------------------
// Misc tile/entity statics.
// ---------------------------------------------------------------------------
const int QuartzBlockTile::TYPE_LINES_Y;
const int Sapling::TYPE_DEFAULT;
const int Sapling::TYPE_EVERGREEN;
const int Sapling::TYPE_BIRCH;
const int Sapling::TYPE_JUNGLE;
const int TallGrass::FERN;
const int SkullTileEntity::TYPE_WITHER;
const int TileEntityDataPacket::TYPE_MOB_SPAWNER;
const int TileEntityDataPacket::TYPE_ADV_COMMAND;
const int TileEntityDataPacket::TYPE_BEACON;
const int TileEntityDataPacket::TYPE_SKULL;

// EntitySelector pointer-to-singleton statics. Real upstream constructs
// these in a registration block we don't pull in; nullptr keeps the
// container/hopper code paths inert (returns "no eligible entity").
const EntitySelector *EntitySelector::ENTITY_STILL_ALIVE = nullptr;
const EntitySelector *EntitySelector::CONTAINER_ENTITY_SELECTOR = nullptr;

// MobCanWearArmourEntitySelector: real bodies live in
// EntitySelector.cpp which doesn't compile yet. Dispenser-armor path
// short-circuits to "no match" until that source is in.
MobCanWearArmourEntitySelector::MobCanWearArmourEntitySelector(std::shared_ptr<ItemInstance> i) : item(i) {}
bool MobCanWearArmourEntitySelector::matches(std::shared_ptr<Entity>) const { return false; }

// StatsCounter::setupStatBoards: real body lives in StatsCounter.cpp;
// noop is fine because no boards means leaderboard panels render empty.
void StatsCounter::setupStatBoards() {}

// ShutdownManager: PS3-style co-op shutdown gate. iOS shell never tears
// down threads cleanly so HasStarted is recorded as a noop and
// ShouldRun always says yes (matches Windows64 shim behaviour).
namespace { struct C4J_EvArrFwd; }
void ShutdownManager::HasStarted(EThreadId) {}
void ShutdownManager::HasStarted(EThreadId, C4JThread::EventArray *) {}
bool ShutdownManager::ShouldRun(EThreadId)  { return true; }
void ShutdownManager::HasFinished(EThreadId) {}

// ---------------------------------------------------------------------------
// Packet statics.
// ---------------------------------------------------------------------------
const int AnimatePacket::SWING;
const int AnimatePacket::WAKE_UP;
const int AnimatePacket::EAT;
const int AnimatePacket::CRITICAL_HIT;
const int AnimatePacket::MAGIC_CRITICAL_HIT;
const int SetEntityLinkPacket::LEASH;
const int SetEntityLinkPacket::RIDING;
const int Level::maxBuildHeight;
const BYTE EntityEvent::USE_ITEM_COMPLETE;
const int TileEditorOpenPacket::SIGN;
const int ContainerOpenPacket::WORKBENCH;
const int ContainerOpenPacket::FURNACE;
const int ContainerOpenPacket::TRAP;
const int ContainerOpenPacket::ENCHANTMENT;
const int ContainerOpenPacket::BREWING_STAND;
const int ContainerOpenPacket::TRADER_NPC;
const int ContainerOpenPacket::BEACON;
const int ContainerOpenPacket::REPAIR_TABLE;
const int ContainerOpenPacket::HOPPER;
const int ContainerOpenPacket::DROPPER;
const int ContainerOpenPacket::HORSE;
const int ContainerOpenPacket::FIREWORKS;
const int GameEventPacket::SUCCESSFUL_BOW_HIT;
const int PotionBrewing::POTION_ID_SPLASH_DAMAGE;
const int AddEntityPacket::BOAT;
const int AddEntityPacket::ITEM;
const int AddEntityPacket::MINECART;
const int AddEntityPacket::PRIMED_TNT;
const int AddEntityPacket::ENDER_CRYSTAL;
const int AddEntityPacket::ARROW;
const int AddEntityPacket::SNOWBALL;
const int AddEntityPacket::EGG;
const int AddEntityPacket::THROWN_ENDERPEARL;
const int AddEntityPacket::FALLING;
const int AddEntityPacket::ITEM_FRAME;
const int AddEntityPacket::EYEOFENDERSIGNAL;
const int AddEntityPacket::THROWN_POTION;
const int AddEntityPacket::THROWN_EXPBOTTLE;
const int AddEntityPacket::FIREWORKS;
const int AddEntityPacket::LEASH_KNOT;
const int AddEntityPacket::FISH_HOOK;

// AddPlayerPacket: real upstream now compiles. Stubs removed.

// C4JThread + Event + EventArray: real upstream C4JThread.cpp now compiles
// via the Win32 thread/event API shims (ResumeThread, CreateEvent,
// WaitForSingleObject, GetCurrentThreadId, GetExitCodeThread, etc).

// ---------------------------------------------------------------------------
// Minecraft (singleton client app). Real app uses these to gate UI; the
// iOS shell never reaches into Minecraft, so safe defaults are fine.
// ---------------------------------------------------------------------------
Minecraft *Minecraft::m_instance = nullptr;
// G3e-step3 / G3f: flood-filled ColourTable shim. ColourTable has no
// virtuals, just an unsigned int array indexed by eMinecraftColour.
// All entries are the plains sky tint (0x78A7FF) so any biome's
// getSkyColor lookup returns a sane blue. Real DLC-loaded values land
// when the texture-pack pipeline is wired (G4); this is a parity-safe
// placeholder until then.
#include "../../../upstream/Minecraft.Client/Common/Colours/ColourTable.h"
static ColourTable *get_colour_table_shim() {
    static char           s_buf[sizeof(ColourTable)];
    static bool           s_init = false;
    if (!s_init) {
        unsigned int *vals = reinterpret_cast<unsigned int *>(s_buf);
        const size_t  n    = sizeof(ColourTable) / sizeof(unsigned int);
        for (size_t i = 0; i < n; i++) vals[i] = 0x78A7FF;
        s_init = true;
    }
    return reinterpret_cast<ColourTable *>(s_buf);
}
ColourTable *Minecraft::getColourTable() { return get_colour_table_shim(); }
bool         Minecraft::isTutorial()      { return false; }
MultiPlayerLevel *Minecraft::getLevel(int /*dimension*/) { return nullptr; }

// ---------------------------------------------------------------------------
// PlayerConnection / ServerPlayer.
// ---------------------------------------------------------------------------
void PlayerConnection::send(std::shared_ptr<Packet> /*packet*/) {}
bool PlayerConnection::isLocal() { return true; }
void PlayerConnection::teleport(double, double, double, float, float, bool) {}
void PlayerConnection::queueSend(std::shared_ptr<Packet> /*packet*/) {}
int  PlayerConnection::countDelayedPackets() { return 0; }

// ServerPlayer: real upstream now compiles, all methods come from the .cpp.

// ---------------------------------------------------------------------------
// GameRenderer overload set. The renderer is an iOS-shimmed shell, so the
// "delete this storage object on the next render frame" calls become no-ops
// (and the storage objects leak harmlessly until process exit).
// ---------------------------------------------------------------------------
void GameRenderer::AddForDelete(SparseDataStorage     * /*p*/) {}
void GameRenderer::AddForDelete(SparseLightStorage    * /*p*/) {}
void GameRenderer::AddForDelete(CompressedTileStorage * /*p*/) {}
void GameRenderer::FinishedReassigning() {}

// File: real upstream File.cpp now compiles in via the POSIX file API
// shims in iOS_WinFileShim.h plus the StringHelpers iOS path-separator
// patch. Stubs removed; bodies live in upstream/Minecraft.World/File.cpp.

// Compression: real upstream compression.cpp now compiles via zlib (iOS
// added to the Win64/Orbis/Vita/Durango branches in patch-upstream-stdafx).

// ConsoleSaveFileOriginal: real upstream now compiles via the
// LevelGenerationOptions.h iOS guard.

// DirectoryLevelStorage: real upstream now compiles via PlayerUID::toString
// being added to the iOS PlayerUID stub. Stubs removed.

// SavedDataStorage: real upstream now compiles via the typeid include
// patches (MapItemSavedData / Villages / StructureFeatureSavedData).

// ServerChunkCache: real upstream now compiles (LONG64 typedef +
// processSchematicsLighting stub). All methods come from the .cpp.

// ---------------------------------------------------------------------------
// RandomLevelSource (chunk source used by ServerLevel::createChunkSource).
// ---------------------------------------------------------------------------
// RandomLevelSource: real upstream RandomLevelSource.cpp now compiles via
// LevelGenerationOptions stub additions (getBiomeOverride). Stubs removed.

// Chunk: real upstream Chunk.cpp now compiles via the C4JRender stub
// additions (CBuffClear, MatrixMode, etc).

// FlatGeneratorInfo: real upstream now compiles via Biome.h include patch.
// EnchantedBookItem: real upstream now compiles via WeighedTreasure.h
// forward-decl patch.

// LevelData: real upstream LevelData.cpp now compiles (ChunkSource.h
// include patched in). Stubs removed.

// MemoryTracker: real upstream MemoryTracker.cpp now compiles via the
// upstream-wrapper glGenTextures()/glDeleteTextures(int) variants added
// in iOS_WinCompat.h and probe_stub.cpp. createFloatBuffer body comes
// from the .cpp; old stub removed.

// ---------------------------------------------------------------------------
// G2b: leaf-symbol stubs so LevelRenderer.cpp links. Each upstream class
// owning these would normally define them in its .cpp, but the matching
// .cpp doesn't compile cleanly on iOS yet (missing GL wrapper variants,
// transitive deps). No-op stubs let the rest of the renderer link; G3 +
// real C4JRender_iOS impls replace them with working bodies.
// ---------------------------------------------------------------------------

// Tesselator: real upstream Tesselator.cpp now compiles. Stubs removed.

// MemoryTracker: real upstream MemoryTracker.cpp now compiles via the
// upstream-wrapper glGenTextures()/glDeleteTextures(int) variants we
// added. Stubs removed.

// OffsettedRenderList: real upstream OffsettedRenderList.cpp now compiles
// via the glCallLists(IntBuffer*) overload added in iOS_WinCompat.h.

// Lighting: real upstream Lighting.cpp now compiles via the
// glLight(int,int,FloatBuffer*) variant added in iOS_WinCompat.h.

#include "Textures.h"
#include "ResourceLocation.h"
class MemTextureProcessor;
class MemTexture;

// G4-step3: PNG decoder + Metal texture upload bridge.
extern "C" unsigned int mcle_glbridge_load_or_get_png_path(const char* path);
extern "C" void         mcle_glbridge_bind_texture(unsigned int id);

// Mirrors upstream's Textures::preLoaded[] for the texture-name enum
// values our shipped renderer actually requests. Each entry is the
// relative path under Common/res/TitleUpdate/res (no leading slash, no
// .png extension - matches upstream exactly).
// External linkage so the patched upstream Textures::bindTexture can
// call it (G5-step29).
const char *texture_name_relpath(_TEXTURE_NAME tn) {
    switch (tn) {
        case TN_TERRAIN_SUN:         return "terrain/sun";
        case TN_TERRAIN_MOON_PHASES: return "terrain/moon_phases";
        case TN_TERRAIN_MOON:        return "terrain/moon";
        case TN_ENVIRONMENT_CLOUDS:  return "environment/clouds";
        case TN_ENVIRONMENT_RAIN:    return "environment/rain";
        case TN_ENVIRONMENT_SNOW:    return "environment/snow";
        case TN_MISC_TUNNEL:         return "misc/tunnel";
        case TN_MISC_WATER:          return "misc/water";
        case TN_PARTICLES:           return "particles";
        case TN_GUI_GUI:             return "gui/gui";
        case TN_GUI_ICONS:           return "gui/icons";
        default:                      return nullptr;
    }
}

namespace {

std::string wstr_to_utf8(const std::wstring &w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        unsigned int c = (unsigned int)wc;
        if (c < 0x80) {
            out.push_back((char)c);
        } else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (c >> 18)));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

} // namespace

// G5-step29: Textures methods (bindTexture, addMemTexture,
// removeMemTexture, getMissingIcon) removed from here. Upstream
// Textures.cpp is now compiled in WorldProbe and provides those.
// The G4 iOS CGImageSource PNG path now goes through upstream's
// loadTexture / TextureManager chain. If iOS-specific behavior
// is needed for any of those, patch upstream rather than re-stub.

// G5-step29: TexturePackRepository.cpp can't compile (UIScene_LanguageSelector
// pulls XC_LANGUAGE/XC_LOCALE enums we don't have, and ConsoleUIController
// methods we don't stub). Provide just the two symbols Textures.cpp needs.
#include "TexturePackRepository.h"
TexturePack *TexturePackRepository::DEFAULT_TEXTURE_PACK = nullptr;
TexturePack *TexturePackRepository::getSelected() { return DEFAULT_TEXTURE_PACK; }

// G5-step32: TextureManager::createStitcher reads this for the atlas size.
// 2048 matches Metal's safe minimum on every iOS device.
#include "Minecraft.h"
int Minecraft::maxSupportedTextureSize() { return 2048; }

// AbstractTexturePack.cpp can't compile (UI/locale deps), but FolderTexturePack
// inherits from it and its vtable references these. Provide minimal symbol
// bodies so the linker has something to point at. These match the no-op
// behavior of the IosFolderTexturePack subclass overrides.
#include "AbstractTexturePack.h"
void AbstractTexturePack::loadIcon() {}
void AbstractTexturePack::loadComparison() {}
void AbstractTexturePack::loadDescription() {}
void AbstractTexturePack::loadName() {}
InputStream *AbstractTexturePack::getResource(const std::wstring &, bool) { return nullptr; }
void AbstractTexturePack::unload(Textures *) {}
void AbstractTexturePack::load(Textures *) {}
bool AbstractTexturePack::hasFile(const std::wstring &, bool) { return true; }
DWORD AbstractTexturePack::getId() { return 0; }
std::wstring AbstractTexturePack::getName() { return L""; }
std::wstring AbstractTexturePack::getDesc1() { return L""; }
std::wstring AbstractTexturePack::getDesc2() { return L""; }
std::wstring AbstractTexturePack::getWorldName() { return L""; }
std::wstring AbstractTexturePack::getAnimationString(const std::wstring &, const std::wstring &) { return L""; }
std::wstring AbstractTexturePack::getAnimationString(const std::wstring &, const std::wstring &, bool) { return L""; }
BufferedImage *AbstractTexturePack::getImageResource(const std::wstring &filename, bool, bool, const std::wstring &) {
    return new BufferedImage(filename, false, false, L"");
}
void AbstractTexturePack::loadColourTable() {}
void AbstractTexturePack::loadUI() {}
void AbstractTexturePack::unloadUI() {}
std::wstring AbstractTexturePack::getXuiRootPath() { return L""; }
PBYTE AbstractTexturePack::getPackIcon(DWORD &) { return nullptr; }
PBYTE AbstractTexturePack::getPackComparison(DWORD &) { return nullptr; }
unsigned int AbstractTexturePack::getDLCParentPackId() { return 0; }
unsigned char AbstractTexturePack::getDLCSubPackId() { return 0; }
// Ctor body too - normally lives in AbstractTexturePack.cpp.
AbstractTexturePack::AbstractTexturePack(DWORD id_, File *file_, const std::wstring &name_, TexturePack *fallback_)
    : id(id_), name(name_), file(file_), m_iconData(nullptr), m_iconSize(0),
      m_comparisonData(nullptr), m_comparisonSize(0), fallback(fallback_),
      m_colourTable(nullptr), iconImage(nullptr), textureId(0) {}

#include "GameRenderer.h"
// G5: TileRenderer reads this for anaglyph color filtering; we keep it
// false so the standard non-stereo path runs.
bool GameRenderer::anaglyph3d = false;
void GameRenderer::EnableUpdateThread() {}
void GameRenderer::DisableUpdateThread() {}
// G2c: LevelRenderer::renderChunks calls these.
void GameRenderer::turnOnLightLayer(double /*alpha*/) {}
void GameRenderer::turnOffLightLayer(double /*alpha*/) {}

#include "LocalPlayer.h"
void LocalPlayer::updateRichPresence() {}

#include "Gui.h"
void Gui::setNowPlaying(const std::wstring & /*s*/) {}

#include "Common/UI/UIScene_SettingsGraphicsMenu.h"
// G5: starts small. Real upstream returns 16 / 8 / 6 / 4 for the four
// distance presets (Far / Normal / Short / Tiny). We return 4 across
// the board so allChanged's first allocation lands a manageable
// xChunks*yChunks*zChunks ~= 6*16*6 = 576 chunk grid. Real preset
// support comes once chunks render correctly.
int UIScene_SettingsGraphicsMenu::LevelToDistance(int /*dist*/) { return 4; }

// MobSkinMemTextureProcessor: real .cpp doesn't compile (uses
// BufferedImage::Graphics which needs the full BufferedImage class
// we haven't pulled in). Stub the vtable anchor.
#include "MobSkinMemTextureProcessor.h"
BufferedImage *MobSkinMemTextureProcessor::process(BufferedImage *in) { return in; }
