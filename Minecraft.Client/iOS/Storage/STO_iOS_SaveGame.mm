#import <Foundation/Foundation.h>
#include "STO_iOS_Paths.h"

// Stub implementations of the save-game interface 4J_Storage exposes on the
// upstream Windows64 build. Real wiring happens once we pull in the upstream
// CConsoleSaveFile* classes. For now this file exists so the static library
// has content and the linker has symbols to resolve.

extern "C" {

// Returns a NUL-terminated UTF-8 path to the root directory used for the
// LCE "GameHDD" tree on iOS. Guaranteed to exist by ios_gamehdd_dir().
const char* mcle_save_root(void) {
    return ios_gamehdd_dir();
}

} // extern "C"
