# Status

Snapshot of where the port actually stands. Kept honest. Updated as things move.

## Builds?

- Yes: CI builds the iOS app shell (no game code yet) on every push. Unsigned `.ipa` is attached to each workflow run. First green build was commit 0fe641e. Artifact size about 15 KB compressed.
- No: the game itself does not run. What ships right now is an empty shell that shows a black screen with a controller-state readout.

## What works

- Bundle layout, launch, orientation locking (landscape only).
- GameController.framework bridge, mapped to the 4J / Xbox 360 button bitmask. Connect an Xbox or PlayStation controller and the status label shows live input.
- Per-frame display link ticking the placeholder renderer.
- NSFileManager-backed paths for `GameHDD`, application support, documents, and bundle resources. Nothing writes to them yet.
- CMake build with Xcode as generator. Presets for device (arm64), simulator (arm64), and simulator (x86_64).

## What does not work

- Rendering: no real renderer. `mcle_render_frame` is a no-op.
- UI: Iggy is stubbed. Menus, HUD, inventory, chat will not draw.
- Audio: nothing wired yet.
- Networking: nothing wired yet. LAN discovery and multiplayer will need their own port pass.
- Save system: paths exist; actual serialization needs upstream Common code to compile against iOS.
- Most of upstream's `Minecraft.Client/Common` and all of `Minecraft.World` are not yet in the build graph.

## Known issues / questions

- GameSWF vs Ruffle: both are candidates to replace Iggy. GameSWF is C++, easier to embed, but old and unmaintained. Ruffle is in Rust, actively developed, harder to glue to a C++ game. Plan is GameSWF first, Ruffle as a fallback.
- Renderer approach: GL ES 3 + MetalANGLE is the current plan. If MetalANGLE is stale on modern iOS SDKs we fall back to a direct Metal backend, which is more work.
- Code signing: nothing gets signed in CI on purpose. Users sign at install time. This keeps the repo public without exposing a dev cert.

## Roadmap

See [ROADMAP.md](ROADMAP.md).
