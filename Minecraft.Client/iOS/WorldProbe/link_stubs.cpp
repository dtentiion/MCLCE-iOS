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
#include "TexturePackRepository.h"

#include <mutex>

#include "AddEntityPacket.h"
#include "AddPlayerPacket.h"
#include "AnimatePacket.h"
#include "ContainerOpenPacket.h"
#include "CustomPayloadPacket.h"
#include "ChatPacket.h"
#include "ExplodePacket.h"
#include "RespawnPacket.h"
#include "EntityEvent.h"
#include "TileEditorOpenPacket.h"
#include "Biome.h"
#include "GameEventPacket.h"
#include "PlayerInfoPacket.h"
#include "ServerSettingsChangedPacket.h"
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
// ColourTable shim. ColourTable is an unsigned int array indexed by
// eMinecraftColour. Real upstream loads values from a DLC .col binary;
// the authoring source is Common/res/TitleUpdate/res/colours.xml. We
// parse the XML directly on first access so sunrise gradient, fog
// color, cloud color, biome tints, etc. all get their proper LCE values
// instead of a flat plains-blue fallback. The fallback (0x78A7FF) is
// still used for any enum entries not present in the XML.
#include "../../../upstream/Minecraft.Client/Common/Colours/ColourTable.h"
#include <fstream>
#include <sstream>
#include <cwchar>
#include <cstring>
extern "C" const char *ios_documents_dir(void);
extern "C" int mcle_log_msg(const char *msg);

static void load_colours_xml(unsigned int *vals, size_t n) {
    const char *root = ios_documents_dir();
    if (!root || !*root) return;
    // Search order mirrors bindTexture / buffered_image_load: try the
    // canonical Win64 path first (Common/res/TitleUpdate/res/), then
    // the sibling layout users typically have on iOS
    // (Common/TitleUpdate/res/), then 1_2_2 source, then base.
    const std::string base = std::string(root) + "/Common/";
    const std::string p1 = base + "res/TitleUpdate/res/colours.xml";
    const std::string p2 = base + "TitleUpdate/res/colours.xml";
    const std::string p3 = base + "res/1_2_2/TitleUpdate/res/colours.xml";
    const std::string p4 = base + "res/colours.xml";
    std::ifstream f(p1);
    if (!f.is_open()) f.open(p2);
    if (!f.is_open()) f.open(p3);
    if (!f.is_open()) f.open(p4);
    if (!f.is_open()) {
        mcle_log_msg("CT_XML colours.xml not found, using plains-blue fallback");
        return;
    }
    std::stringstream ss; ss << f.rdbuf();
    const std::string xml = ss.str();
    int hits = 0;
    // Scan for <colour name="X" value="HEX"/> entries. The XML is small
    // (~350 entries) so an O(N*M) name-lookup is fine.
    size_t pos = 0;
    while ((pos = xml.find("<colour ", pos)) != std::string::npos) {
        size_t name_start = xml.find("name=\"", pos);
        if (name_start == std::string::npos) break;
        name_start += 6;
        size_t name_end = xml.find('"', name_start);
        if (name_end == std::string::npos) break;
        size_t val_start = xml.find("value=\"", name_end);
        if (val_start == std::string::npos) break;
        val_start += 7;
        size_t val_end = xml.find('"', val_start);
        if (val_end == std::string::npos) break;
        const std::string name = xml.substr(name_start, name_end - name_start);
        const std::string val  = xml.substr(val_start,  val_end  - val_start);
        unsigned int hex = 0;
        for (char c : val) {
            unsigned int d = 0;
            if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') d = 10 + (unsigned)(c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10 + (unsigned)(c - 'A');
            else { hex = 0xFFFFFFFFu; break; }
            hex = (hex << 4) | d;
        }
        if (hex != 0xFFFFFFFFu) {
            // Linear search the ColourTableElements wchar_t array for a
            // matching name. Convert ASCII C string to wstring for compare.
            std::wstring wname(name.begin(), name.end());
            for (size_t i = 0; i < n; i++) {
                const wchar_t *elem = ColourTable::ColourTableElements[i];
                if (elem && wcscmp(elem, wname.c_str()) == 0) {
                    vals[i] = hex;
                    hits++;
                    break;
                }
            }
        }
        pos = val_end;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "CT_XML parsed colours.xml: %d entries applied (of %zu)", hits, n);
    mcle_log_msg(buf);
}

static ColourTable *get_colour_table_shim() {
    static char           s_buf[sizeof(ColourTable)];
    static bool           s_init = false;
    if (!s_init) {
        unsigned int *vals = reinterpret_cast<unsigned int *>(s_buf);
        const size_t  n    = sizeof(ColourTable) / sizeof(unsigned int);
        for (size_t i = 0; i < n; i++) vals[i] = 0x78A7FF;
        load_colours_xml(vals, n);
        s_init = true;
    }
    return reinterpret_cast<ColourTable *>(s_buf);
}
extern "C" int mcle_log_msg(const char *msg);
ColourTable *Minecraft::getColourTable() {
    // Minecraft.cpp's: the selected pack's table, else the default pack's.
    // The shim only covers colours.col not being there.
    ColourTable *ct = nullptr;
    if (skins) {
        TexturePack *selected = skins->getSelected();
        ct = selected->getColourTable();
        if (ct == nullptr) ct = skins->getDefault()->getColourTable();
    }
    if (!ct) ct = get_colour_table_shim();
    static int s_logged = 0;
    if (s_logged < 3) {
        char buf[256];
        snprintf(buf, sizeof(buf), "MC_CT_CKPT getColourTable called this=%p ct=%p", (void*)this, (void*)ct);
        mcle_log_msg(buf);
        s_logged++;
    }
    return ct;
}
// Minecraft::isTutorial and the tutorial bits are in Game/MCLETutorial.cpp
MultiPlayerLevel *Minecraft::getLevel(int /*dimension*/) { return nullptr; }

// ---------------------------------------------------------------------------
// PlayerConnection / ServerPlayer.
// ---------------------------------------------------------------------------
// What the server sends the local player's client goes nowhere, but a
// ContainerOpenPacket is where the client opens the menu
// (ClientConnection::handleContainerOpen).
extern void mcle_container_open_packet(int type, int containerId, const std::wstring &title, bool customName,
                                       int size, int entityId);
// What the local player's client would do with a packet sent to it
extern "C" int mcle_log_msg(const char *msg);
void mcle_handle_chat_packet(std::shared_ptr<ChatPacket> packet);   // Game/MCLEChat.cpp
void mcle_local_player_explosion(std::shared_ptr<ExplodePacket> packet);   // Game/MCLEGameLoop.cpp
void mcle_tutorial_local_player_packet(std::shared_ptr<Packet> packet);   // Game/MCLETutorial.cpp
void mcle_player_entity_link(int type, int sourceId, int destId);   // Game/MCLEPlayerInput.cpp
void mcle_container_custom_payload(const std::wstring &identifier, byteArray data);   // Game/MCLETradingMenu.cpp
void mcle_local_player_respawn_packet(int dimension);   // Game/MCLEGameLoop.cpp
void mcle_local_player_win_game();   // Game/MCLEGameLoop.cpp
void mcle_sign_entry_tile_editor_open(int editorType, int x, int y, int z);   // Game/MCLESignEntryMenu.cpp
void mcle_client_player_info(std::shared_ptr<PlayerInfoPacket> packet);   // Game/MCLEPlayerInfo.cpp
void mcle_client_server_settings_changed(std::shared_ptr<ServerSettingsChangedPacket> packet);   // Game/MCLEPlayerInfo.cpp
void mcle_local_player_packet(std::shared_ptr<Packet> packet) {
    // what ClientConnection's handlers pass on to the tutorial
    mcle_tutorial_local_player_packet(packet);
    if (std::shared_ptr<SetEntityLinkPacket> link = std::dynamic_pointer_cast<SetEntityLinkPacket>(packet)) {
        mcle_player_entity_link(link->type, link->sourceId, link->destId);
    } else if (std::shared_ptr<ContainerOpenPacket> open = std::dynamic_pointer_cast<ContainerOpenPacket>(packet)) {
        mcle_container_open_packet(open->type, open->containerId, open->title, open->customName, open->size,
                                   open->entityId);
    } else if (std::shared_ptr<ChatPacket> chat = std::dynamic_pointer_cast<ChatPacket>(packet)) {
        char line[48];
        snprintf(line, sizeof(line), "chat packet type %d", static_cast<int>(chat->m_messageType));
        mcle_log_msg(line);
        mcle_handle_chat_packet(chat);
    } else if (std::shared_ptr<ExplodePacket> explode = std::dynamic_pointer_cast<ExplodePacket>(packet)) {
        mcle_local_player_explosion(explode);
    } else if (std::shared_ptr<RespawnPacket> respawn = std::dynamic_pointer_cast<RespawnPacket>(packet)) {
        mcle_local_player_respawn_packet(respawn->dimension);
    } else if (std::shared_ptr<GameEventPacket> event = std::dynamic_pointer_cast<GameEventPacket>(packet)) {
        if (event->_event == GameEventPacket::WIN_GAME) mcle_local_player_win_game();
    } else if (std::shared_ptr<CustomPayloadPacket> payload = std::dynamic_pointer_cast<CustomPayloadPacket>(packet)) {
        mcle_container_custom_payload(payload->identifier, payload->data);
    } else if (std::shared_ptr<TileEditorOpenPacket> editor = std::dynamic_pointer_cast<TileEditorOpenPacket>(packet)) {
        // ClientConnection::handleTileEditorOpen, on the main thread
        mcle_sign_entry_tile_editor_open(editor->editorType, editor->x, editor->y, editor->z);
    } else if (std::shared_ptr<PlayerInfoPacket> info = std::dynamic_pointer_cast<PlayerInfoPacket>(packet)) {
        mcle_client_player_info(info);
    } else if (std::shared_ptr<ServerSettingsChangedPacket> settings =
                   std::dynamic_pointer_cast<ServerSettingsChangedPacket>(packet)) {
        mcle_client_server_settings_changed(settings);
    }
}
void PlayerConnection::send(std::shared_ptr<Packet> packet) {
    mcle_local_player_packet(packet);
}
bool PlayerConnection::isLocal() { return true; }
// PlayerConnection::teleport moves the server's player and tells its client
// where it went. The one ServerPlayer here is the local player and has no
// connection, so the move is left, with what the client does when it's told
// (beds, portals, respawn, ender pearls).
extern void mcle_local_player_abs_move_to(double x, double y, double z, float yRot, float xRot, bool sendPacket);
void PlayerConnection::teleport(double x, double y, double z, float yRot, float xRot, bool sendPacket) {
    mcle_local_player_abs_move_to(x, y, z, yRot, xRot, sendPacket);
}
void PlayerConnection::queueSend(std::shared_ptr<Packet> /*packet*/) {}
int  PlayerConnection::countDelayedPackets() { return 0; }

// ServerPlayer: real upstream now compiles, all methods come from the .cpp.

// ---------------------------------------------------------------------------
// GameRenderer delete stacks. GameRenderer.cpp isn't built for iOS, so these
// mirror its m_deleteStack* vectors: storage swapped out of a LevelChunk is
// parked here and freed after updateDirtyChunks, the same point
// GameRenderer::runUpdate frees it. AddForDelete takes the lock and
// FinishedReassigning drops it, like m_csDeleteStack.
// ---------------------------------------------------------------------------
static std::recursive_mutex s_deleteStackMu;
static std::vector<SparseLightStorage *>    s_deleteSparseLight;
static std::vector<CompressedTileStorage *> s_deleteCompressedTile;
static std::vector<SparseDataStorage *>     s_deleteSparseData;

void GameRenderer::AddForDelete(SparseDataStorage *p)     { s_deleteStackMu.lock(); s_deleteSparseData.push_back(p); }
void GameRenderer::AddForDelete(SparseLightStorage *p)    { s_deleteStackMu.lock(); s_deleteSparseLight.push_back(p); }
void GameRenderer::AddForDelete(CompressedTileStorage *p) { s_deleteStackMu.lock(); s_deleteCompressedTile.push_back(p); }
void GameRenderer::FinishedReassigning() { s_deleteStackMu.unlock(); }

extern "C" void mcle_delete_reassigned_storage(void) {
    std::lock_guard<std::recursive_mutex> lk(s_deleteStackMu);
    for (auto *p : s_deleteSparseLight) delete p;
    s_deleteSparseLight.clear();
    for (auto *p : s_deleteCompressedTile) delete p;
    s_deleteCompressedTile.clear();
    for (auto *p : s_deleteSparseData) delete p;
    s_deleteSparseData.clear();
}

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
        case TN_TERRAIN:             return "terrain";
        case TN_GUI_ITEMS:           return "items";
        case TN_TERRAIN_SUN:         return "terrain/sun";
        // moon_phases.png is the 4x2 atlas (8 phases). moon.png is a
        // single full-moon tile. renderSky slices the bound texture into
        // 4x2 cells per phase index, so MOON_PHASES must point at the
        // atlas - otherwise UV slicing carves up a single moon and you
        // see only 1/8 of it (cropped corner).
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

// G5-step32: TextureManager::createStitcher reads this for the atlas size.
// 2048 matches Metal's safe minimum on every iOS device.
#include "Minecraft.h"
int Minecraft::maxSupportedTextureSize() { return 2048; }

// Minecraft.cpp's, TexturePackRepository::resetWebSkin asks for it
void Minecraft::delayTextureReload() {
    reloadTextures = true;
}

#include "GameRenderer.h"
// G5: TileRenderer reads this for anaglyph color filtering; we keep it
// false so the standard non-stereo path runs.
bool GameRenderer::anaglyph3d = false;
// The chunk update thread is the sim thread's updateDirtyChunks (MCLEGameLoop.cpp)
void mcle_chunk_update_enable();
void mcle_chunk_update_disable();
void GameRenderer::EnableUpdateThread() { mcle_chunk_update_enable(); }
void GameRenderer::DisableUpdateThread() { mcle_chunk_update_disable(); }
// RenderManager.TextureBindVertex with the light texture, and -1 for none
extern "C" void mcle_glbridge_set_light_layer(int on);
void GameRenderer::turnOnLightLayer(double /*alpha*/) { mcle_glbridge_set_light_layer(1); }
void GameRenderer::turnOffLightLayer(double /*alpha*/) { mcle_glbridge_set_light_layer(0); }

#include "LocalPlayer.h"
void LocalPlayer::updateRichPresence() {}

#include "Gui.h"
void Gui::setNowPlaying(const std::wstring & /*s*/) {}

#include "Common/UI/UIScene_SettingsGraphicsMenu.h"
// The graphics menu's render distance table, which LevelRenderer::allChanged
// sizes the render area from
int UIScene_SettingsGraphicsMenu::LevelToDistance(int level) {
    static const int table[6] = {2, 4, 8, 16, 32, 64};
    if (level < 0) level = 0;
    if (level > 5) level = 5;
    return table[level];
}

// MobSkinMemTextureProcessor: real .cpp doesn't compile (uses
// BufferedImage::Graphics which needs the full BufferedImage class
// we haven't pulled in). Stub the vtable anchor.
#include "MobSkinMemTextureProcessor.h"
BufferedImage *MobSkinMemTextureProcessor::process(BufferedImage *in) { return in; }

// ClientConstants: real .cpp uses Win32-style VER_FILEVERSION_STR_W /
// VER_BRANCHVERSION_STR_W macros from a generated version.h we don't have.
// Gui.cpp's debug overlay reads these strings; iOS shows them as empty.
#include "ClientConstants.h"
const std::wstring ClientConstants::VERSION_STRING = std::wstring();
const std::wstring ClientConstants::BRANCH_STRING  = std::wstring();

// Bridge from probe_stub.cpp's glColor4f shim into Tesselator's color
// state. Without this sync, vertex emits use stale Tesselator _col
// (e.g. sunrise glow leaving alpha=0), making downstream sun/moon
// vertices render as black-on-black.
#include "Tesselator.h"
extern "C" void Tesselator_setColorBridge(float r, float g, float b, float a) {
    Tesselator *t = Tesselator::getInstance();
    if (t) t->color(r, g, b, a);
}

// Minecraft::currentTimeMillis - declared static in Minecraft.h:303 but no
// definition in Minecraft.cpp. ItemRenderer::renderItemBillboard uses it for
// the bobbing item-on-ground animation. Route through chrono steady_clock.
#include <chrono>
int64_t Minecraft::currentTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

#include "Options.h"
// Minecraft.cpp's, LivingEntityRenderer asks them before drawing name tags
bool Minecraft::renderNames() {
    if (m_instance == nullptr || !m_instance->options->hideGui) {
        return true;
    }
    return false;
}

bool Minecraft::renderDebug() {
    return (m_instance != nullptr && m_instance->options->renderDebug);
}

// Minecraft.cpp's, ConsoleSaveFileOriginal::Flush puts it in the save's metadata
unsigned int Minecraft::getCurrentTexturePackId() {
    return skins->getSelected()->getId();
}

// HUD-pulled symbols. Once Gui.cpp / GuiComponent.cpp / Lighting.cpp went
// into the build their references became live; the owning .cpps for these
// don't compile yet. Stubs return safe defaults / no-ops.

class FloatBuffer;
void glLight(unsigned int light, unsigned int mode, FloatBuffer *values); // probe_stub.cpp
void glLight(int light, int mode, FloatBuffer *values) {
    glLight(static_cast<unsigned int>(light), static_cast<unsigned int>(mode), values);
}

// Screen size accessed by Gui::render via extern int g_rScreen{Width,Height}.
// Gui only reads these in a couple of branches we don't hit; default 0.
int g_rScreenWidth  = 0;
int g_rScreenHeight = 0;

// setupGuiScreen sets up the orthographic projection for HUD rendering.
// Parity with upstream GameRenderer.cpp:1923-1934 - clear depth, ortho
// projection in SCALED gui coords (rawWidth/rawHeight), modelview at
// z=-2000 so HUD vertices end up at the near plane. Critical: ortho
// uses scaled dims, NOT physical, because Gui.cpp emits all vertices in
// scaled-coord space (computed via ScreenSizeCalculator with the same
// forceScale). Mismatch -> hotbar renders as a tiny corner blob.
extern "C" void mcle_glbridge_matrix_mode(int mode);
extern "C" void mcle_glbridge_load_identity(void);
extern "C" void mcle_glbridge_translate(float, float, float);
extern "C" void mcle_glbridge_metal_ortho(float, float, float, float, float, float);
#include "GameRenderer.h"
#include "ScreenSizeCalculator.h"
void GameRenderer::setupGuiScreen(int forceScale) {
    Minecraft *mc = Minecraft::GetInstance();
    if (!mc || mc->width <= 0 || mc->height <= 0) return;
    ScreenSizeCalculator ssc(mc->options, mc->width, mc->height, forceScale);
    int rw = ssc.getWidth();
    int rh = ssc.getHeight();
    if (rw <= 0 || rh <= 0) { rw = mc->width; rh = mc->height; }
    mcle_glbridge_matrix_mode(0x1701 /* GL_PROJECTION */);
    mcle_glbridge_load_identity();
    mcle_glbridge_metal_ortho(0.0f, (float)rw, (float)rh, 0.0f,
                               1000.0f, 3000.0f);
    mcle_glbridge_matrix_mode(0x1700 /* GL_MODELVIEW */);
    mcle_glbridge_load_identity();
    mcle_glbridge_translate(0.0f, 0.0f, -2000.0f);
}

#include "MultiPlayerGameMode.h"
bool MultiPlayerGameMode::isCutScene() { return false; }


// Gui::render's debug overlay reads these stat strings - empty is fine.
std::wstring Minecraft::gatherStats1() { return std::wstring(); }
std::wstring Minecraft::gatherStats2() { return std::wstring(); }
std::wstring Minecraft::gatherStats3() { return std::wstring(); }
std::wstring Minecraft::gatherStats4() { return std::wstring(); }
// Returning false skips Gui::render's renderVignette call - that's a
// full-screen overlay that without the proper texture binding paints
// the screen blue. Once vignette texture loading is wired we can flip
// this back on.
bool         Minecraft::useFancyGraphics() { return false; }
