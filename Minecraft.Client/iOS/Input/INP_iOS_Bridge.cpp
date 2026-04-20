// Pure-C++ side of the iOS input bridge. Kept separate from the .mm file so
// it can be consumed by plain C++ code without pulling Objective-C into the
// translation unit.
//
// The heavy lifting lives in INP_iOS_Controller.mm. This file only exists so
// the static library has a non-Obj-C compile unit, which helps catch header
// hygiene problems early (no accidental Obj-C leakage into the C++ world).

#include "INP_iOS_Bridge.h"

// Compile-time checks that the bridge header is valid C++.
static_assert(sizeof(mcle_ios_pad_state) > 0, "mcle_ios_pad_state is empty");
