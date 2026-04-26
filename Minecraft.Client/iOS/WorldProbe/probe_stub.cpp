// Anchor TU for the probe static archive. Also home for single-TU
// definitions of platform-global instances upstream code references
// via `extern`. Real iOS implementations live under platform/ subdirs;
// this file just gives the linker a definition so the archive resolves.

#include "iOS_stdafx.h"
#include "4JLibs/inc/4J_Profile.h"
#include "4JLibs/inc/4J_Storage.h"

namespace mcle_world_probe { inline void _anchor() {} }

// Definitions of platform-globals upstream gameplay code references via
// `extern`. Real iOS implementations land under Minecraft.Client/iOS/
// (Profile/, Storage/, etc) but the probe lib needs a single TU that
// owns the symbol so the link resolves.
C_4JProfile ProfileManager;
C4JStorage  StorageManager;
