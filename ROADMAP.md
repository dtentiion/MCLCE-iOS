# Roadmap

Rough order. Some of these overlap and some get punted if they turn out to be bigger than they look.

## Phase 0: scaffold (done)

Repo, CMake iOS toolchain, GitHub Actions CI, iOS app shell that launches, GameController bridge mapped to the 4J / Xbox 360 button bitmask.

## Phase 1: UI runtime (done)

- Ruffle fork (`third_party/ruffle_ios/`) linked into the app and rendering LCE's stock SWF files.
- Patches for XUI bitmap imports, stage-level AS3 event cleanup on scene swap, focus behaviour, and a handful of LCE-specific rendering fixes on the fork at [dtentiion/ruffle](https://github.com/dtentiion/ruffle) branch `mclce-ios-patches`.
- Scene-transition machinery: replace_root_movie + per-scene init + focus routing through the SWF's own `FJ_Document.SetFocus`.
- Panorama, logo, tooltip sibling SWFs composited beneath the root.

## Phase 2: menu tree (in progress)

- Main Menu (done).
- Help & Options menu (done).
- Settings menu + all 5 sub-menus (Options, Audio, Control, Graphics, User Interface) functional with live slider application and disk persistence (done).
- LoadOrJoin world-list scene skeleton (done).
- HowToPlay scene skeleton (done).
- Leaderboard, DLCMain, SkinSelect, LanguageSelector scene skeletons (queued next).
- Reset-to-defaults confirmation dialog (after the dialog widget lands).
- FJ_Tooltips bottom hint strip (queued).
- Rendering polish pass across all menu scenes (after all scenes are in).

## Phase 3: audio (done)

- miniaudio (same library console LCE uses in `SoundEngine.cpp`) with stb_vorbis OGG decoder.
- Menu music with console-parity GetRandomishTrack logic.
- UI SFX (focus / select / back) with pitch jitter like console.
- Live volume changes from the Audio settings sliders without scene reload.

## Phase 4: save format + world loading (next big rock)

Biggest single unlock toward actually playing. LCE saves are a specific packed binary format with chunk data, player state, and world metadata. Goal of this phase is that Play Game → select a save → load into the world.

Sub-tasks:

- Port enough of `Minecraft.World` to compile for iOS ARM64 to parse a save header.
- Read save file from `Documents/saves/` (or wherever LCE's iOS path convention ends up), surface the world list in LoadOrJoin's SavesList instead of the hardcoded "Create New World" stub.
- Chunk deserialization, entity state, inventory. World generator second, since loading an existing world is the simpler first target.

## Phase 5: gameplay renderer

The menus use Ruffle via wgpu and that's fine. For actual blocks / chunks / entities we need a proper 3D renderer.

- Port `Minecraft.Client/Rendering` paths. Upstream has D3D11; we route through Metal.
- Shader translation pipeline (HLSL → SPIR-V → MSL → `.metallib`) is already working in CI, see the `Shader probe` workflow. Four real 4JLibs shaders round-trip cleanly today.
- The C++ side still needs porting: `RendererCore.cpp`, `RendererState.cpp`, `RendererTexture.cpp`, `RendererVertex.cpp`, `RendererCBuff.cpp`, `RendererMatrix.cpp` all assume D3D11 resource binding.
- Bundle `.metallib` at build time, load at runtime from `[NSBundle mainBundle]`.

## Phase 6: input pass 2

- Map every 4J_Input call site to the GameController bridge (menu nav already works, gameplay input is the next pass).
- Vibration / haptics via GCControllerHaptics.
- Controller reconnect handling during gameplay.
- Optional keyboard and mouse via GCKeyboard / GCMouse (iOS 14+) for iPad users who want it.

## Phase 7: networking

- LAN discovery (UDP 25566) via Bonjour / NSNetService.
- TCP connect to dedicated servers (should just work, upstream uses BSD sockets).
- Test against the Windows dedicated server running on a PC on the same network.
- Possible crossplay with the community Windows64 build if we pin against their commit.

## Phase 8: packaging + release

- Signed release builds as GitHub Releases tags, not just CI artifacts.
- INSTALL.md screenshots.
- No public TestFlight, sideload only.

## Things explicitly not on the roadmap

- On-screen touch controls. Xbox / MFi controllers cover the target userbase.
- App Store submission. Sideload-only for legal and practical reasons.
- Java Edition or Bedrock parity backports. Respect upstream CONTRIBUTING rules.
- Multiplayer auth services tied to Microsoft / Xbox Live. LAN and direct-IP only.

## Longer term, if any of this ever happens

- Android port sharing most of the C++ platform code once iOS stabilises.
- Mod loader API.
- DLC asset import from user-owned original game data.
