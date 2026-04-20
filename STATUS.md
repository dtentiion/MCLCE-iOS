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
- **Shader translation pipeline.** All four real 4JLibs HLSL shaders round-trip HLSL → SPIR-V → MSL → `.metallib` in CI. See the `Shader probe` workflow. Means Phase 2 does not need manual shader rewriting.
- **Iggy-to-SWF converter script.** `scripts/iggy-to-swf.sh` drives JPEXS to turn `.iggy` files into standard `.swf` that GameSWF can consume. Build-time only, not in CI (game assets are not shipped here).

## What does not work

- Rendering: no real renderer. `mcle_render_frame` is a no-op.
- UI: Iggy is stubbed. Menus, HUD, inventory, chat will not draw.
- Audio: nothing wired yet.
- Networking: nothing wired yet. LAN discovery and multiplayer will need their own port pass.
- Save system: paths exist; actual serialization needs upstream Common code to compile against iOS.
- Most of upstream's `Minecraft.Client/Common` and all of `Minecraft.World` are not yet in the build graph.

## World probe: current wall

The optional `-DENABLE_WORLD_PROBE=ON` target now attempts to compile `AABB.cpp` and `Vec3.cpp` from upstream. Two walls cleared, one new wall hit.

Cleared so far:
- `sal.h` (Microsoft Source Annotation Language) shimmed with empty macros.
- Core Win32 typedefs (`WORD`, `DWORD`, `LPVOID`, `LPCWSTR`, `SIZE_T`, `ULONG_PTR`, `FILETIME`, `CRITICAL_SECTION`, `VOID`, `PBYTE`) mapped in `Minecraft.Client/iOS/iOS_WinCompat.h`.

Blocking the probe right now:
- `reference to 'byte' is ambiguous`. Upstream `extraX64.h` does `typedef unsigned char byte;` alongside `using namespace std;` somewhere transitive. On C++17+ libc++ this collides with `std::byte`. MSVC suppressed this via `_HAS_STD_BYTE=0`; libc++ has no equivalent switch. Likely fixes, ordered by effort:
  1. Patch upstream headers to rename the typedef to `u8` or `Byte`, or qualify every use.
  2. Force the probe TU to `-std=c++14` where `std::byte` does not exist. Brittle; won't work once we pull in C++17-only code.
  3. Do a small header injection that does `namespace std { using ::byte; }` before C++17 is introduced. Very hacky.

See `world-probe.yml` Actions run artifact for full log.

## Known issues / questions

- GameSWF vs Ruffle: both are candidates to replace Iggy. GameSWF is C++, easier to embed, but old and unmaintained. Ruffle is in Rust, actively developed, harder to glue to a C++ game. Plan is GameSWF first, Ruffle as a fallback.
- Renderer approach: GL ES 3 + MetalANGLE is the current plan. If MetalANGLE is stale on modern iOS SDKs we fall back to a direct Metal backend, which is more work.
- Code signing: nothing gets signed in CI on purpose. Users sign at install time. This keeps the repo public without exposing a dev cert.

## Roadmap

See [ROADMAP.md](ROADMAP.md).
