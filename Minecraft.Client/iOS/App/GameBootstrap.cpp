// Single C entry point the view controller calls once per frame. Eventually
// this is where upstream's CMinecraft::Tick() or equivalent is invoked.
//
// Right now it does nothing so the scaffold build runs without pulling in
// any of the upstream Minecraft.Client or Minecraft.World sources. Those get
// layered in later behind their own CMake switch.

extern "C" void mcle_game_tick(void) {
    // TODO: call into the upstream main loop once upstream sources compile
    // for iOS. Until then this is a no-op.
}
