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

// ---------------------------------------------------------------------------
// C4JThread::EventArray. iOS doesn't use the cross-thread waitable-event
// pattern; every call resolves to a no-op.
// ---------------------------------------------------------------------------
void C4JThread::EventArray::ClearAll() {}
void C4JThread::EventArray::Set(int /*index*/) {}

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

// ---------------------------------------------------------------------------
// File. iOS doesn't have GetFileAttributes / Win32 file APIs in the way
// upstream File.cpp uses them; treat every file as non-existent and every
// list as empty. McRegionLevelStorageSource::getLevelList sees no saves and
// the bootstrap idles, which is the expected first-launch behaviour.
// ---------------------------------------------------------------------------
File::File(const File &/*parent*/, const std::wstring &child) {
    (void)child;
}
File::File(const std::wstring &/*pathname*/) {}
bool File::_delete()                  { return false; }
bool File::mkdir() const              { return false; }
bool File::exists() const             { return false; }
bool File::isDirectory() const        { return false; }
int64_t File::length()                { return 0; }
std::wstring File::getName() const    { return std::wstring(); }
const std::wstring File::getPath() const { return std::wstring(); }
std::vector<File *> *File::listFiles() const { return nullptr; }
bool File::renameTo(File /*dest*/)    { return false; }

int  FileKeyHash::operator()(const File &/*k*/) const                    { return 0; }
bool FileKeyEq::  operator()(const File &/*a*/, const File &/*b*/) const { return false; }

// ---------------------------------------------------------------------------
// Compression. iOS doesn't ship XBox / PS XMemCompress; without a real save
// to read or write, every Compress/Decompress is a no-op returning S_OK.
// ---------------------------------------------------------------------------
Compression *Compression::getCompression() { return nullptr; }
HRESULT Compression::Decompress(void * /*pDst*/, unsigned int * /*pDstSize*/, void * /*pSrc*/, unsigned int /*srcSize*/) { return 0; }
HRESULT Compression::CompressLZXRLE(void *, unsigned int *, void *, unsigned int)   { return 0; }
HRESULT Compression::DecompressLZXRLE(void *, unsigned int *, void *, unsigned int) { return 0; }
void    Compression::SetDecompressionType(ESavePlatform /*plat*/) {}

// ---------------------------------------------------------------------------
// ConsoleSaveFileOriginal. The iOS shell never constructs one of these
// directly; the symbol is only pulled in because DirectoryLevelStorageSource
// has a `ConsoleSaveFileOriginal tempSave(levelId)` reference. Stub the
// entire virtual surface so the vtable resolves cleanly.
// ---------------------------------------------------------------------------
ConsoleSaveFileOriginal::ConsoleSaveFileOriginal(const std::wstring &/*fileName*/, LPVOID /*pvSaveData*/, DWORD /*fileSize*/, bool /*forceCleanSave*/, ESavePlatform /*plat*/) {}
ConsoleSaveFileOriginal::~ConsoleSaveFileOriginal() {}

FileEntry *ConsoleSaveFileOriginal::createFile(const ConsoleSavePath &/*fileName*/) { return nullptr; }
void  ConsoleSaveFileOriginal::deleteFile(FileEntry * /*file*/) {}
void  ConsoleSaveFileOriginal::setFilePointer(FileEntry *, LONG, PLONG, DWORD) {}
BOOL  ConsoleSaveFileOriginal::writeFile(FileEntry *, LPCVOID, DWORD, LPDWORD) { return 0; }
BOOL  ConsoleSaveFileOriginal::zeroFile(FileEntry *, DWORD, LPDWORD)           { return 0; }
BOOL  ConsoleSaveFileOriginal::readFile(FileEntry *, LPVOID, DWORD, LPDWORD)   { return 0; }
BOOL  ConsoleSaveFileOriginal::closeHandle(FileEntry *)                        { return 0; }
void  ConsoleSaveFileOriginal::finalizeWrite() {}
bool  ConsoleSaveFileOriginal::doesFileExist(ConsoleSavePath /*file*/)         { return false; }
void  ConsoleSaveFileOriginal::Flush(bool /*autosave*/, bool /*updateThumbnail*/) {}
#ifndef _CONTENT_PACKAGE
void  ConsoleSaveFileOriginal::DebugFlushToFile(void *, unsigned int) {}
#endif
unsigned int ConsoleSaveFileOriginal::getSizeOnDisk()                          { return 0; }
std::wstring ConsoleSaveFileOriginal::getFilename()                            { return std::wstring(); }
std::vector<FileEntry *> *ConsoleSaveFileOriginal::getFilesWithPrefix(const std::wstring &)         { return nullptr; }
std::vector<FileEntry *> *ConsoleSaveFileOriginal::getRegionFilesByDimension(unsigned int)          { return nullptr; }
int   ConsoleSaveFileOriginal::getSaveVersion()         { return 0; }
int   ConsoleSaveFileOriginal::getOriginalSaveVersion() { return 0; }
void  ConsoleSaveFileOriginal::LockSaveAccess()    {}
void  ConsoleSaveFileOriginal::ReleaseSaveAccess() {}
ESavePlatform ConsoleSaveFileOriginal::getSavePlatform()        { return SAVE_FILE_PLATFORM_LOCAL; }
bool  ConsoleSaveFileOriginal::isSaveEndianDifferent()           { return false; }
void  ConsoleSaveFileOriginal::setLocalPlatform()                {}
void  ConsoleSaveFileOriginal::setPlatform(ESavePlatform /*p*/)  {}
ByteOrder ConsoleSaveFileOriginal::getSaveEndian()               { return LITTLEENDIAN; }
ByteOrder ConsoleSaveFileOriginal::getLocalEndian()              { return LITTLEENDIAN; }
void  ConsoleSaveFileOriginal::setEndian(ByteOrder /*e*/)        {}
bool  ConsoleSaveFileOriginal::isLocalEndianDifferent(ESavePlatform /*p*/) { return false; }
void  ConsoleSaveFileOriginal::ConvertRegionFile(File /*src*/) {}
void  ConsoleSaveFileOriginal::ConvertToLocalPlatform()        {}
void *ConsoleSaveFileOriginal::getWritePointer(FileEntry *)    { return nullptr; }

// ---------------------------------------------------------------------------
// DirectoryLevelStorage. McRegionLevelStorage extends this and is in the
// lib; its ctor calls our stubbed parent ctor.
// ---------------------------------------------------------------------------
DirectoryLevelStorage::DirectoryLevelStorage(ConsoleSaveFile *saveFile, const File /*dir*/, const std::wstring &/*levelId*/, bool /*createPlayerDir*/)
    : playerDir(L""),
      dataDir(L""),
      sessionId(0),
      m_bHasLoadedMapDataMappings(false),
      m_saveFile(saveFile) {}
DirectoryLevelStorage::~DirectoryLevelStorage() {}

void                DirectoryLevelStorage::flushSaveFile(bool /*autosave*/) {}
void                DirectoryLevelStorage::checkSession() {}
LevelData          *DirectoryLevelStorage::prepareLevel() { return nullptr; }
void                DirectoryLevelStorage::saveLevelData(LevelData *, std::vector<std::shared_ptr<Player>> *) {}
void                DirectoryLevelStorage::saveLevelData(LevelData *) {}
void                DirectoryLevelStorage::save(std::shared_ptr<Player>) {}
CompoundTag        *DirectoryLevelStorage::load(std::shared_ptr<Player>)                { return nullptr; }
CompoundTag        *DirectoryLevelStorage::loadPlayerDataTag(PlayerUID)                 { return nullptr; }
void                DirectoryLevelStorage::clearOldPlayerFiles() {}
PlayerIO           *DirectoryLevelStorage::getPlayerIO()    { return this; }
ConsoleSavePath     DirectoryLevelStorage::getDataFile(const std::wstring &id) { return ConsoleSavePath(id); }
std::wstring        DirectoryLevelStorage::getLevelId()    { return std::wstring(); }
int                 DirectoryLevelStorage::getAuxValueForMap(PlayerUID, int, int, int, int) { return 0; }
void                DirectoryLevelStorage::saveMapIdLookup() {}
void                DirectoryLevelStorage::deleteMapFilesForPlayer(std::shared_ptr<Player>) {}
void                DirectoryLevelStorage::saveAllCachedData() {}
ChunkStorage       *DirectoryLevelStorage::createChunkStorage(Dimension * /*dim*/) { return nullptr; }
void                DirectoryLevelStorage::closeAll() {}

// ---------------------------------------------------------------------------
// SavedDataStorage. Stubs cover ServerLevel's references; real persistence
// is deferred until LevelStorage paths actually hit disk.
// ---------------------------------------------------------------------------
SavedDataStorage::SavedDataStorage(LevelStorage *storage) : levelStorage(storage) {}
std::shared_ptr<SavedData> SavedDataStorage::get(const std::type_info &/*clazz*/, const std::wstring &/*id*/) { return nullptr; }
void SavedDataStorage::set(const std::wstring &/*id*/, std::shared_ptr<SavedData> /*data*/) {}
void SavedDataStorage::save() {}

// ---------------------------------------------------------------------------
// ServerChunkCache. Constructor + the methods ServerLevel calls into.
// ---------------------------------------------------------------------------
ServerChunkCache::ServerChunkCache(ServerLevel * /*level*/, ChunkStorage * /*storage*/, ChunkSource * /*source*/) {}
ServerChunkCache::~ServerChunkCache() {}
bool        ServerChunkCache::hasChunk(int, int)                                  { return false; }
std::vector<LevelChunk *> *ServerChunkCache::getLoadedChunkList()                 { return nullptr; }
void        ServerChunkCache::drop(int, int)                                      {}
LevelChunk *ServerChunkCache::create(int, int)                                    { return nullptr; }
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
RandomLevelSource::RandomLevelSource(Level * /*level*/, int64_t /*seed*/, bool genStructures)
    : generateStructures(genStructures) {}
RandomLevelSource::~RandomLevelSource() {}
LevelChunk *RandomLevelSource::create(int, int)                                  { return nullptr; }
LevelChunk *RandomLevelSource::getChunk(int, int)                                { return nullptr; }
void        RandomLevelSource::lightChunk(LevelChunk *)                          {}
bool        RandomLevelSource::hasChunk(int, int)                                { return false; }
void        RandomLevelSource::postProcess(ChunkSource *, int, int)              {}
bool        RandomLevelSource::save(bool, ProgressListener *)                    { return true; }
bool        RandomLevelSource::tick()                                            { return false; }
bool        RandomLevelSource::shouldSave()                                      { return false; }
std::wstring RandomLevelSource::gatherStats()                                    { return std::wstring(); }
std::vector<Biome::MobSpawnerData *> *RandomLevelSource::getMobsAt(MobCategory *, int, int, int) { return nullptr; }
TilePos    *RandomLevelSource::findNearestMapFeature(Level *, const std::wstring &, int, int, int)    { return nullptr; }
void        RandomLevelSource::recreateLogicStructuresForChunk(int, int)         {}

// ---------------------------------------------------------------------------
// Chunk (the rendering chunk on Minecraft.Client side).
// ---------------------------------------------------------------------------
Chunk::Chunk()  {}
Chunk::~Chunk() {}

// ---------------------------------------------------------------------------
// FlatGeneratorInfo. Used at world creation; no flat-world support yet.
// ---------------------------------------------------------------------------
FlatGeneratorInfo::~FlatGeneratorInfo() {}
int FlatGeneratorInfo::getBiome() { return 0; }
FlatGeneratorInfo *FlatGeneratorInfo::fromValue(const std::wstring &/*input*/) { return nullptr; }

// ---------------------------------------------------------------------------
// EnchantedBookItem. Item factory paths; not driven by the bootstrap.
// ---------------------------------------------------------------------------
void EnchantedBookItem::addEnchantment(std::shared_ptr<ItemInstance> /*item*/, EnchantmentInstance * /*enchantment*/) {}
std::shared_ptr<ItemInstance> EnchantedBookItem::createForEnchantment(EnchantmentInstance * /*enchant*/) { return nullptr; }

// ---------------------------------------------------------------------------
// LevelData. The full virtual surface is stubbed because defining the key
// function (setTagData) here forces the vtable to emit in this TU; once
// emitted, every other slot must resolve. Real LevelData.cpp doesn't
// compile because of missing macros (HELL_LEVEL_MAX_*); revisit when those
// are wired.
// ---------------------------------------------------------------------------
LevelData::LevelData(CompoundTag * /*tag*/) {}
LevelData::LevelData(LevelSettings * /*settings*/, const std::wstring &name) : levelName(name) {}

void           LevelData::setTagData(CompoundTag *) {}
int64_t        LevelData::getSeed()                  { return 0; }
int            LevelData::getXSpawn()                { return 0; }
int            LevelData::getYSpawn()                { return 64; }
int            LevelData::getZSpawn()                { return 0; }
int            LevelData::getXStronghold()           { return 0; }
int            LevelData::getZStronghold()           { return 0; }
int            LevelData::getXStrongholdEndPortal()  { return 0; }
int            LevelData::getZStrongholdEndPortal()  { return 0; }
int64_t        LevelData::getGameTime()              { return 0; }
int64_t        LevelData::getDayTime()               { return 0; }
int64_t        LevelData::getSizeOnDisk()            { return 0; }
CompoundTag   *LevelData::getLoadedPlayerTag()       { return nullptr; }
void           LevelData::setSeed(int64_t)           {}
void           LevelData::setXSpawn(int)             {}
void           LevelData::setYSpawn(int)             {}
void           LevelData::setZSpawn(int)             {}
void           LevelData::setHasStronghold()         {}
bool           LevelData::getHasStronghold()         { return false; }
void           LevelData::setXStronghold(int)        {}
void           LevelData::setZStronghold(int)        {}
void           LevelData::setHasStrongholdEndPortal(){}
bool           LevelData::getHasStrongholdEndPortal(){ return false; }
void           LevelData::setXStrongholdEndPortal(int){}
void           LevelData::setZStrongholdEndPortal(int){}
void           LevelData::setGameTime(int64_t)       {}
void           LevelData::setDayTime(int64_t)        {}
void           LevelData::setSizeOnDisk(int64_t)     {}
void           LevelData::setLoadedPlayerTag(CompoundTag *){}
void           LevelData::setSpawn(int, int, int)    {}
std::wstring   LevelData::getLevelName()             { return levelName; }
void           LevelData::setLevelName(const std::wstring &n) { levelName = n; }
int            LevelData::getVersion()               { return 0; }
void           LevelData::setVersion(int)            {}
int64_t        LevelData::getLastPlayed()            { return 0; }
bool           LevelData::isThundering()             { return false; }
void           LevelData::setThundering(bool)        {}
int            LevelData::getThunderTime()           { return 0; }
void           LevelData::setThunderTime(int)        {}
bool           LevelData::isRaining()                { return false; }
void           LevelData::setRaining(bool)           {}
int            LevelData::getRainTime()              { return 0; }
void           LevelData::setRainTime(int)           {}
GameType      *LevelData::getGameType()              { return nullptr; }
bool           LevelData::isGenerateMapFeatures()    { return false; }
bool           LevelData::getSpawnBonusChest()       { return false; }
void           LevelData::setGameType(GameType *)    {}
bool           LevelData::useNewSeaLevel()           { return false; }
bool           LevelData::getHasBeenInCreative()     { return false; }
void           LevelData::setHasBeenInCreative(bool) {}
LevelType     *LevelData::getGenerator()             { return nullptr; }
void           LevelData::setGenerator(LevelType *)  {}
std::wstring   LevelData::getGeneratorOptions()      { return std::wstring(); }
void           LevelData::setGeneratorOptions(const std::wstring &) {}
bool           LevelData::isHardcore()               { return false; }
bool           LevelData::getAllowCommands()         { return false; }
void           LevelData::setAllowCommands(bool)     {}
bool           LevelData::isInitialized()            { return false; }
void           LevelData::setInitialized(bool)       {}
GameRules     *LevelData::getGameRules()             { return &gameRules; }
int            LevelData::getXZSize()                { return 256; }
#ifdef _LARGE_WORLDS
int            LevelData::getXZSizeOld()             { return 0; }
void           LevelData::getMoatFlags(bool *bClassic, bool *bSmall, bool *bMedium) {
    if (bClassic) *bClassic = false;
    if (bSmall)   *bSmall   = false;
    if (bMedium)  *bMedium  = false;
}
int            LevelData::getXZHellSizeOld()         { return 0; }
#endif
int            LevelData::getHellScale()             { return 3; }
