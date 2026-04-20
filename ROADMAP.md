# Roadmap

Rough order. Some of these overlap and some get punted if they turn out to be bigger than they look.

## Phase 0: scaffold (done)

- Repo, CMake iOS toolchain, GitHub Actions CI, iOS app shell that launches, controller bridge.

## Phase 1: compile Minecraft.World for iOS

Upstream's `Minecraft.World` is the most portable chunk of code. It contains block logic, chunks, entities, world generation, and a lot of generic C++. Goal of this phase is to get it compiling as a static library for iOS ARM64.

Sub-tasks:

- Add a `Minecraft.World` target to the iOS CMake graph, pointed at upstream sources.
- Provide a compatibility header covering every Windows-ism the world code reaches for. Start with `WORD`, `DWORD`, `BYTE`, `HRESULT`, `CRITICAL_SECTION`, `InitializeCriticalSection`, `Sleep`, and expand as the linker complains.
- Stub or port the tiny platform surface the world code actually uses. Most of it is timers and locks; iOS equivalents are trivial.
- Land green CI: world compiles, app shell links against it, nothing calls it yet.

## Phase 2: renderer

Native Metal is now the plan. Reason: the shader translation problem turned out to be automated.

Working tooling (validated in the `Shader probe` CI workflow):

- Input:  HLSL from `third_party/4JLibs/Windows_Libs/Dev/Render/shaders/*.hlsl`.
- Pipeline: `glslangValidator -D` produces SPIR-V. `spirv-cross --msl` produces Metal Shading Language. `xcrun metal` compiles MSL to `.air`, `xcrun metallib` packs to `.metallib`.
- Output: valid `.metallib` files on every push of a shader change. `main_VS`, `main_PS`, `screen_VS`, `screen_PS` all round-trip cleanly today.

Remaining renderer work with this pipeline in hand:

- C++ side: port `RendererCore.cpp`, `RendererState.cpp`, `RendererTexture.cpp`, `RendererVertex.cpp`, `RendererCBuff.cpp`, `RendererMatrix.cpp` from `third_party/4JLibs/Windows_Libs/Dev/Render` against Metal instead of D3D11. The pipeline-state and resource-binding code needs a rewrite; the high-level draw dispatch can stay.
- Bundle generated `.metallib` files at build time and load them at runtime from `[NSBundle mainBundle]`.
- Wire `mcle_render_init` / `mcle_render_frame` into the new backend.

GL ES + MetalANGLE stays as a backup plan if the Metal renderer port gets too painful.

## Phase 3: UI (GameSWF + Iggy-to-SWF conversion)

The upstream UI calls into Iggy, which is closed source. The `.iggy` files shipped with the game are a 4J wrapper around standard SWF.

Two paths into this phase now that tooling is available:

1. **Convert at build time.** JPEXS has an Iggy-to-SWF converter in its Java library. `scripts/iggy-to-swf.sh` drives the JPEXS CLI against a directory of `.iggy` files. The output `.swf` files go in `Resources/` and get bundled into the `.ipa`. GameSWF then reads normal SWF at runtime. This is the current plan.
2. **Read `.iggy` at runtime.** Would require writing an iggy-aware loader inside GameSWF. More fragile. Avoid unless option 1 hits a wall.

Sub-tasks in order:

- Vendor GameSWF (done, see `third_party/gameswf`).
- Stand up a minimal CMake target that compiles the GameSWF core for iOS ARM64. Expected to surface zlib / libpng / jpeg dependencies; bring those in via iOS-available versions.
- Hook GameSWF's `render_handler` interface to our Metal backend from Phase 2.
- Implement the Iggy C API shim in `Minecraft.Client/iOS/UI/Iggy_Shim.cpp` by delegating to GameSWF's C++ player object.
- Start with a single screen (title / press-start) end-to-end, then expand.
- If GameSWF cannot handle the AS3 features LCE uses, switch to Ruffle via its C API.

## Phase 4: input pass 2

- Map every 4J_Input call site to our GameController bridge.
- Vibration / haptics via GCControllerHaptics.
- Controller reconnect handling during gameplay.
- Optional: keyboard + mouse via GCKeyboard / GCMouse (iOS 14+) if anyone actually wants it on iPad.

## Phase 5: audio

- Replace whatever Windows audio path the upstream game uses with AVAudioEngine or AudioToolbox.
- Music, SFX, 3D positional for entities.

## Phase 6: networking

- LAN discovery (UDP 25566) needs a Bonjour / NetService translation on iOS.
- TCP connect to dedicated servers just works if we use BSD sockets, which upstream mostly does.
- Test against the Windows dedicated server running on a PC on the same network.

## Phase 7: packaging + release

- Signed release builds as GitHub Releases tags, not just CI artifacts.
- INSTALL.md polished with screenshots.
- Public TestFlight is not planned. Sideload-only.

## Things explicitly not on the roadmap

- On-screen touch controls. Xbox / MFi controllers cover our users.
- App Store submission. This is a sideload-only project for legal and practical reasons.
- Java Edition or Bedrock parity backports. Respect upstream CONTRIBUTING rules on that.
- Multiplayer auth services tied to Microsoft / Xbox Live. LAN and direct-IP only.

## Longer term, if any of this ever happens

- Android port sharing most of the C++ platform code once iOS stabilizes.
- Mod loader API.
- DLC asset import from user-owned original game data.
