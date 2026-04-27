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

Phase B (compile coverage) and Phase C (link coverage) target the upstream gameplay tree. Snapshot at the time of this writing:

- **Auto-probe** (per-file `clang++ -fsyntax-only` against the iOS toolchain): 960 greens. ~795 of 831 `Minecraft.World/*.cpp` files (96%), ~37 in `Minecraft.Client/Common/`, ~123 in `Minecraft.Client/` root including ServerLevel.cpp + DerivedServerLevel + RemotePlayer + EntityTracker + a chunk of entity models + particles + screens.
- **Probe static library `mcle_world_probe`**: 850+ files compile and archive. Core simulation classes are all in: Entity, Level, LevelChunk, Player, Tile, TileEntity, Mob, LivingEntity, ItemInstance, Container, ServerLevel.
- **Phase C link-test**: force-load executable target started this stretch with 32 undefined symbols. Now down to 9. Cleared the Entity::, Level::, ServerLevel::, Player::, ChestTile::, ZonedChunkStorage::, and DLCManager:: clusters by adding the matching .cpp files to the lib (or providing out-of-line static defs / ctor/dtor stubs in `probe_stub.cpp`).
- **Remaining 9 link blockers**: 5 specific gameplay-host classes (Minecraft.cpp, MinecraftServer.cpp, PlayerList.cpp, MultiPlayerLevel.cpp, LevelRenderer.cpp). All transitively pull the UI/Iggy/render chain (UIControl_Base.h, IggyBitmapFontProvider, etc.) which is replaced on iOS by the SWF runtime. Phase D's GL ES 3.0 renderer bringup is what unblocks these.
- **What this unlocks once Phase D lands**: world loading, chunk simulation, entity ticking, the full gameplay loop. Save-format reader is the next rock after that.

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
