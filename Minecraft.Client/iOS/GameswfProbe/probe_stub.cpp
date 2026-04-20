// Link-level smoke test. Creates a gameswf::player and wires our Metal
// render_handler into it. If this TU links, the render_handler's virtual
// table is complete and the player's entry points resolve.
//
// Kept in the probe target only; the actual app integration lives under
// Minecraft.Client/iOS/UI/.

#include "render_handler_metal.h"
#include "gameswf/gameswf.h"
#include "gameswf/gameswf_player.h"

namespace mcle_gameswf_probe {

// Reference both symbols so the linker does not discard them.
void _smoke_test() {
    gameswf::render_handler* rh = create_render_handler_metal();
    gameswf::set_render_handler(rh);

    // Player creation exercises a large fraction of the library's init path.
    gameswf::player* p = new gameswf::player();
    (void)p;

    // Intentionally do not destroy: this is a probe, not a runtime test.
}

} // namespace mcle_gameswf_probe
