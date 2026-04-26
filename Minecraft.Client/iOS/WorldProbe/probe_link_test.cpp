// Phase C bootstrap: a one-symbol executable that force-loads the
// mcle_world_probe static archive so every TU in it gets pulled into
// the link. The linker then surfaces every unresolved external symbol
// as a hard error, which is the Phase C work list.
//
// This is expected to fail to link with a large number of undefined
// symbols. That is the point: the failure log is the next-step
// priority queue. Each undefined symbol is either:
//   - a forward-declared class whose method body lives outside the
//     probe set (Entity, Mob, etc) - need to add their .cpp to the
//     probe or write a body
//   - a global object referenced by upstream code (Tile::cloth, etc)
//     - need a defining TU
//   - a CMinecraftApp method not absorbed by McleAppStub - need to
//     add the method to the stub
//
// We DO NOT promise this target builds clean. The world-probe job
// continues to be the source of truth for compile coverage. This
// target's failure log is the source of truth for link coverage.

#include "AABB.h"
#include "Mth.h"

extern "C" int probe_link_test_main(int /*argc*/, char** /*argv*/) {
    AABB box(0, 0, 0, 1, 1, 1);
    float r = Mth::sqrt(2.0f);
    return (int)(box.x0 + r);
}
