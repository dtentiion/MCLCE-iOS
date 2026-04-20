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

## GameSWF probe: full player compiles clean

Running `-DENABLE_GAMESWF_PROBE=ON` (manual workflow "Port probes") builds green for iOS ARM64 on the complete SWF player. Scope:

- All 21 portable sources in `base/` (containers, triangulation, image manipulation, GC, timers, utf8, config, etc.)
- All ~45 portable sources in `gameswf/` (player, SWF stream parser, shape rendering, AVM1 / AVM2 virtual machines, ActionScript, sprite, button, font, fontlib, tesselation, text, character, canvas, filters, morph, all AS3 builtin classes, etc.)

Excluded from the probe (not portable as-is, not needed yet):
- Renderer handlers for D3D / OGL / OGL ES / Intel Wireless GL / Xbox. We write our own Metal render_handler.
- Sound handlers for SDL / OpenAL. We wire AVFoundation later.
- `gameswf_freetype.cpp` and `gameswf_test_ogl.cpp`. FreeType comes with the font pass, test_ogl is demo code.
- `base/jpeg.cpp` (pending jpeglib wiring) and `base/png_helper.cpp` (pending libpng).

Walls cleared, all automated by `scripts/patch-gameswf.sh`:
1. `fmax` / `fmin` rename to `gs_fmax` / `gs_fmin` (libc++ collision).
2. `compiler_assert(x)` macro neutralized (newer clang trips on its `switch` duplicate-case trick inside template bodies).
3. `vm_stack : private array<as_value>` flipped to `public` so derived classes can name `array<T>` unqualified, which the original code assumed.
4. `compatibility_include.h` rewritten with guarded `#define`s so our iOS config overrides rather than gets overridden by the Marmalade defaults.
5. `__DATE__` / `__TIME__` adjacency to string literals space-separated so the C++11 UDL parser does not treat them as suffixes.

Not-yet-handled in the probe (next walls to tackle):
- Pull in the bundled `jpeglib/` sources, flip `TU_CONFIG_LINK_TO_JPEGLIB=1`.
- Write a Metal-backed `render_handler` under `Minecraft.Client/iOS/UI/` and link it into the probe (will give us our first SWF-driven frame on screen).
- Load a test `.swf` from the app bundle and drive the player.
- FreeType for font rendering (can defer; many SWFs embed their own).

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
