// Anchor TU for the probe static archive. Also home for single-TU
// definitions of platform-global instances upstream code references
// via `extern`. Real iOS implementations live under platform/ subdirs;
// this file just gives the linker a definition so the archive resolves.

#include "iOS_stdafx.h"
#include "4JLibs/inc/4J_Profile.h"
#include "4JLibs/inc/4J_Storage.h"
#include "ChestTile.h"
#include "ZonedChunkStorage.h"

namespace mcle_world_probe { inline void _anchor() {} }

// Definitions of platform-globals upstream gameplay code references via
// `extern`. Real iOS implementations land under Minecraft.Client/iOS/
// (Profile/, Storage/, etc) but the probe lib needs a single TU that
// owns the symbol so the link resolves.
C_4JProfile ProfileManager;
C4JStorage  StorageManager;
class CTelemetryManager;
CTelemetryManager *TelemetryManager = nullptr;

// Out-of-line definitions for upstream class statics that were declared
// with in-class initializers but never defined out-of-line in the .cpp
// chain we have. MSVC tolerates this; macOS/iOS clang+ld require the
// definition for any odr-use (e.g. taking the address, passing by
// reference, or initialiser-list evaluation).
//
// Values match the in-class declarations in the corresponding headers.
const int ChestTile::TYPE_TRAP;
const int ZonedChunkStorage::CHUNKS_PER_ZONE_BITS = 5;
const int ZonedChunkStorage::CHUNKS_PER_ZONE = 1 << ZonedChunkStorage::CHUNKS_PER_ZONE_BITS;
