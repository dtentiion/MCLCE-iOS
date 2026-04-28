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

// ---------------------------------------------------------------------------
// Packet statics.
// ---------------------------------------------------------------------------
const int AnimatePacket::SWING;
const int SetEntityLinkPacket::LEASH;
const int SetEntityLinkPacket::RIDING;
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

AddPlayerPacket::AddPlayerPacket(std::shared_ptr<Player>, PlayerUID, PlayerUID, int, int, int, int, int, int) {}
AddPlayerPacket::~AddPlayerPacket() {}
void AddPlayerPacket::read(DataInputStream *)   {}
void AddPlayerPacket::write(DataOutputStream *) {}
void AddPlayerPacket::handle(PacketListener *)  {}
int  AddPlayerPacket::getEstimatedSize()        { return 0; }

// C4JThread + Event + EventArray: real upstream C4JThread.cpp now compiles
// via the Win32 thread/event API shims (ResumeThread, CreateEvent,
// WaitForSingleObject, GetCurrentThreadId, GetExitCodeThread, etc).

// ---------------------------------------------------------------------------
// Minecraft (singleton client app). Real app uses these to gate UI; the
// iOS shell never reaches into Minecraft, so safe defaults are fine.
// ---------------------------------------------------------------------------
ColourTable *Minecraft::getColourTable() { return nullptr; }
bool         Minecraft::isTutorial()      { return false; }

// ---------------------------------------------------------------------------
// PlayerConnection / ServerPlayer.
// ---------------------------------------------------------------------------
void PlayerConnection::send(std::shared_ptr<Packet> /*packet*/) {}
bool PlayerConnection::isLocal() { return true; }
void PlayerConnection::teleport(double, double, double, float, float, bool) {}

void ServerPlayer::flushEntitiesToRemove() {}
ServerLevel *ServerPlayer::getLevel() { return nullptr; }
void ServerPlayer::flagEntitiesToBeRemoved(unsigned int * /*flags*/, bool * /*removedFound*/) {}
int  ServerPlayer::getFlagIndexForChunk(const ChunkPos &/*pos*/, int /*dimension*/) { return 0; }
int  ServerPlayer::getPlayerViewDistanceModifier() { return 0; }

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

// ---------------------------------------------------------------------------
// ServerChunkCache. Constructor + the methods ServerLevel calls into.
// ---------------------------------------------------------------------------
ServerChunkCache::ServerChunkCache(ServerLevel * /*level*/, ChunkStorage * /*storage*/, ChunkSource * /*source*/) {}
ServerChunkCache::~ServerChunkCache() {}
bool        ServerChunkCache::hasChunk(int, int)                                  { return false; }
std::vector<LevelChunk *> *ServerChunkCache::getLoadedChunkList()                 { return nullptr; }
void        ServerChunkCache::drop(int, int)                                      {}
LevelChunk *ServerChunkCache::create(int, int)                                    { return nullptr; }
LevelChunk *ServerChunkCache::create(int, int, bool)                              { return nullptr; }
LevelChunk *ServerChunkCache::getChunk(int, int)                                  { return nullptr; }
LevelChunk *ServerChunkCache::getChunkLoadedOrUnloaded(int, int)                  { return nullptr; }
void        ServerChunkCache::dontDrop(int, int)                                  {}
void        ServerChunkCache::postProcess(ChunkSource *, int, int)                {}
bool        ServerChunkCache::saveAllEntities()                                   { return true; }
bool        ServerChunkCache::save(bool, ProgressListener *)                      { return true; }
bool        ServerChunkCache::tick()                                              { return false; }
bool        ServerChunkCache::shouldSave()                                        { return false; }
std::wstring ServerChunkCache::gatherStats()                                      { return std::wstring(); }
std::vector<Biome::MobSpawnerData *> *ServerChunkCache::getMobsAt(MobCategory *, int, int, int) { return nullptr; }
TilePos    *ServerChunkCache::findNearestMapFeature(Level *, const std::wstring &, int, int, int)    { return nullptr; }
void        ServerChunkCache::recreateLogicStructuresForChunk(int, int)           {}

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
