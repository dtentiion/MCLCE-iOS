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

Two paths. Pick one after prototyping both. Listed in the order of least pain:

1. Rewrite the renderer backend against OpenGL ES 3.0. Wrap it with MetalANGLE on device. This gets us a working Metal pipeline without writing Metal directly, and the GL ES code is more portable if we ever want Android or Linux.
2. Write a native Metal backend. Faster at runtime, but full HLSL to MSL shader translation and a pipeline state rewrite is a lot of work.

Whichever path, the shader sources in `upstream/Minecraft.Client/Common/res/shaders` need translation. Keep that translation in one place so the other path remains open.

## Phase 3: UI (GameSWF for Iggy)

The upstream UI layer calls into Iggy, which is closed source on Windows. The `.iggy` files shipped with the game are SWFs under a different name.

- Vendor GameSWF into `third_party/gameswf`.
- Implement the Iggy C API surface in `Minecraft.Client/iOS/UI/Iggy_Shim.cpp` by delegating to GameSWF C++ objects (create a movie, play, draw, query focusable objects, call AS3 methods, etc).
- Start with a single screen (the title / press-start screen) end-to-end, then expand.
- If GameSWF turns out to be too stale for the AS3 features LCE uses, switch to Ruffle via its C API.

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
