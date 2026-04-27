// Bootstrap forwarders.
//
// The render loop in MinecraftViewController.mm:2307 calls
// mcle_game_tick() once per frame. The actual implementations of
// mcle_game_init / mcle_game_tick / mcle_game_shutdown live in
// Minecraft.Client/iOS/Game/MCLEGameLoop.cpp inside the static lib
// `mcle_ios_game`. Linker pulls them from there.
//
// This file is intentionally empty so the App target keeps building
// (it expects GameBootstrap.cpp to exist by name). Once the bootstrap
// API surface stabilises we can fold the App's bootstrap source list
// directly into mcle_ios_game and remove this stub.
