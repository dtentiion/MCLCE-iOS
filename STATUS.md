# Status

Honest snapshot of where the port is. Updated as things move.

## What runs right now

The app launches straight into the real LCE main menu. Here's what works end-to-end:

- Animated panorama background, LCE logo, random splash text re-rolled every load (same logic as console).
- Menu music. Tracks picked with the same GetRandomishTrack logic as console (unheard-first, reset when all played).
- UI sound effects on focus, select, back.
- Controller navigation. Xbox, PlayStation, and MFi gamepads all work through Apple's GameController framework. D-pad moves, A selects, B backs out.
- Full walk-through of Main Menu, Help & Options, and Settings.
- All five Settings sub-menus: Options, Audio, Control, Graphics, User Interface.
  - Sliders apply live. Music volume actually fades in real time, no scene reload.
  - Checkboxes persist.
  - Reset to Defaults resets every setting console resets.
- `settings.dat` in Documents, written on every change.
- Play Game opens the world list scene (SavesList + JoinList layout).
- How To Play menu shows its 24 topic entries.

## What doesn't run yet

- World creation and loading. The save-format reader isn't wired up, so Play Game reaches the world list but can't enter a world. Gameplay is the next rock after the menu tree is done.
- Leaderboards, DLC, Skin Select menus. Skeletons queued next, same pattern as LoadOrJoin and HowToPlay.
- Multiplayer and networking. None of it is in the iOS build yet. On the roadmap but behind gameplay.
- Tooltips strip at the bottom of each menu. The authored SWF has it, we don't wire it yet.
- Some rendering quirks on scrollbars, panel masking, and a couple of labels. Being cleaned up as scenes land.

## Gameplay code port: progress snapshot

Phase B (compile coverage), Phase C (link coverage), and Phase D Step 1 (probe lib wired into the .ipa) are done. Phase D2 (gameplay-host class compilation) is in progress.

- **Auto-probe** (per-file `clang++ -fsyntax-only` against the iOS toolchain): 1042 greens. ~795 of 831 `Minecraft.World/*.cpp` files (96%), 39 in `Minecraft.Client/Common/`, ~208 in `Minecraft.Client/` root.
- **Gameplay-host class status:**
  - ✅ ServerLevel.cpp green and in the lib
  - ✅ PlayerList.cpp green and in the lib
  - ✅ MultiPlayerLevel.cpp green and in the lib
  - ✅ MinecraftServer.cpp green and in the lib
  - ✅ LevelRenderer.cpp green and in the lib
  - 🔄 Minecraft.cpp red, blocked on upstream tutorial/trial/demo gamemode subclasses whose headers transitively pull the UI cascade we cannot pre-include
- **Probe static library `mcle_world_probe`**: 855+ files compile and archive. Core simulation classes plus all the gameplay-host classes above. All clusters of undefined symbols at link time are cleared.
- **Phase C link-test**: zero undefined symbols. The lib archives clean and links into the .ipa.
- **Phase D Step 1 wire-up**: `mcle_world_probe.a` is linked into the .ipa via `-Wl,-force_load`. A `mcle_game_init` + `mcle_game_tick` bootstrap pair in `App/GameBootstrap.cpp` is called from the existing render loop and logs to iOS Console via `os_log`. **Confirmed working on device** (iPhone 16e, 2026-04-27): the .ipa launches, the menu shell is unregressed, the bootstrap line + per-second tick lines stream cleanly.
- **Phase D2 grind status**: the `Minecraft.cpp` constructor / init chain references about a dozen upstream-only types (FullTutorialMode, TrialMode, ConsoleGameMode, etc.) whose headers each pull a UI/Iggy cascade. Continuing the per-symbol grind has diminishing returns; the next strategic move is either to write an iOS-specific top-level shell that bypasses the upstream Minecraft constructor (parity-breaking only at the top level) or to land Phase E (save loading) and Phase D real-renderer in parallel and stub through the Minecraft init via a custom simpler entry.
- **What still needs to happen for visible gameplay**: instantiate Minecraft (Phase D2 finish or top-level shell), wire NSFileManager save loading (Phase E), replace `gl*` and `C4JRender::*` no-op stubs with real Metal-backed bodies (Phase D-real renderer).

Mature shim infrastructure lives in `Minecraft.Client/iOS/`:

- `iOS_WinCompat.h`: Win32 type aliases, TLS, CriticalSection, mach-time bridge for QueryPerformanceCounter, file/thread/atomic stubs (CreateFile, CreateThread, Sleep, Interlocked\*64, GetFileSize, CreateDirectory, MEMORYSTATUS), level constants, PIX no-ops, secure-CRT printf, `_itow_s` template variants.
- `iOS_stdafx.h`: pre-includes Definitions, ArrayWithLength, System, Mth, Random, IO streams, Icon, TilePos, ChunkPos, Pos, ItemInstance, AttributeInstance, App_enums, App_Defines, Class, Attribute, ConsoleGameRulesConstants, Console_Awards_enum, Console_Debug_enum, Minecraft_Macros, Potion_Macros, ColourTable, Exceptions, StringHelpers, NetworkPlayerInterface, StringTable, DLCFile, DLCSkinFile, real Minecraft.h.
- `iOS_app_stub.h`: `McleAppStub` and `McleNetworkManagerStub` for the upstream `app` and `g_NetworkManager` globals. Variadic-template methods absorb call signatures while real iOS bindings are written.
- `4JLibs/inc/4J_Storage.h`, `4J_Profile.h`: real-shape platform globals (StorageManager, ProfileManager) the upstream code reaches for.

## How it's put together

- **UI runtime:** patched Ruffle fork (`third_party/ruffle_ios/` + branch `mclce-ios-patches` on [dtentiion/ruffle](https://github.com/dtentiion/ruffle)). Ruffle runs LCE's stock `.swf` menu files directly. Patches cover XUI bitmap imports, scene-lifecycle AS3 listener cleanup, and a handful of rendering fixes specific to LCE's authoring conventions.
- **Audio:** miniaudio with the stb_vorbis OGG decoder. Same library console uses.
- **Graphics:** wgpu through MetalANGLE.
- **Controller:** GameController.framework bridge in `Minecraft.Client/iOS/Input/INP_iOS_Controller.mm`, output snapshotted into the 4J `_360_JOY_BUTTON_*` bitmask the game's input layer was already written for.
- **Settings:** flat `unsigned char[32]` keyed by LCE's `eGameSetting` enum, persisted to `Documents/settings.dat` on every set. Parallels console's `Win64_SaveSettings` path.

## Key decisions that differ from the original port plan

The original plan in older versions of this file called for GameSWF (with Iggy-to-SWF conversion) and a direct Metal renderer. Neither turned out to be the right call:

- **GameSWF was dropped in favour of Ruffle** once AVM2 support in GameSWF turned out to be too incomplete for LCE's ActionScript 3 menus. Ruffle is actively maintained, handles AS3, and had a cleaner C FFI path.
- **The direct Metal renderer was deferred.** Ruffle's wgpu backend rides MetalANGLE on iOS and covers the menu workload. A standalone Metal renderer is still on the roadmap for gameplay, since wgpu via Ruffle is overkill for block / chunk rendering.
- **Audio went with miniaudio instead of AVAudioEngine.** Matches what console LCE actually uses, and gets OGG support for free.
- **Iggy-to-SWF conversion isn't needed at runtime.** The SWF files are already there in the console build's media folder (the Iggy header is a thin wrapper); JPEXS strips it once at build time.

## Notes

- Code signing is not done in CI on purpose. Users sign at install time. Keeps the repo public without exposing a dev cert.
- The `.ipa` artifacts from Actions are unsigned. Sideload tools (AltStore / Sideloadly / xtool / TrollStore) handle signing.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for what's next.
