// Anchor TU for the probe static archive. Also home for single-TU
// definitions of platform-global instances upstream code references
// via `extern`. Real iOS implementations live under platform/ subdirs;
// this file just gives the linker a definition so the archive resolves.

#include "iOS_stdafx.h"
#include "4JLibs/inc/4J_Profile.h"

namespace mcle_world_probe { inline void _anchor() {} }

// Definition of the global ProfileManager instance. Upstream gameplay
// code (MultiPlayerLevel, Consoles_App, ...) writes `ProfileManager.X()`
// expecting the C_4JProfile global to exist at link time. Real iOS
// profile work lands under Minecraft.Client/iOS/Profile/.
C_4JProfile ProfileManager;
