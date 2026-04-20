# Contributing

Thanks for having a look. This is a one-person hobby port at the moment, so the process is pretty informal.

## What's worth working on

See [STATUS.md](STATUS.md) for the current "what builds, what doesn't" snapshot and [ROADMAP.md](ROADMAP.md) for the broader plan. The biggest unblockers right now:

- Anything that helps the `-DENABLE_WORLD_PROBE=ON` target chew through more of upstream `Minecraft.World`. See the "World probe: current wall" section in STATUS.
- Research into whether Iggy can be replaced by GameSWF (or Ruffle). Even a small proof of concept rendering one `.iggy` asset on iOS is extremely valuable.
- A sketch Metal or GL ES renderer backend replacing the `4J_Render` stubs.

## Ground rules

- Respect upstream [MCLCE/MinecraftConsoles](https://github.com/MCLCE/MinecraftConsoles) contribution rules regarding parity with LCE. This repo inherits them.
- Don't commit Minecraft game assets. The port relies on user-supplied asset files at runtime.
- Don't commit Apple signing material (`.p12`, `.mobileprovision`, `.p8`). The CI intentionally produces unsigned `.ipa` files.
- Prefer small, focused commits over big sweeping ones. It is easier to bisect port regressions that way.

## Sending a change

1. Fork, make a feature branch.
2. Run the build locally if you can. Anyone with a Mac and Xcode can use the provided CMake presets. Otherwise push and let CI tell you.
3. Open a PR against `main` with a short description of what you changed and why. Link to a STATUS section if the change unblocks something tracked there.
4. Be patient. This is a side project.

## Style

- C++17 is fine. No need to restrict to C++14 unless a specific header demands it.
- Match upstream style where you are editing upstream-derived code (tabs, CamelCase types, m_ prefixed members).
- For new iOS platform code under `Minecraft.Client/iOS/`, modern Objective-C++ style with ARC is preferred.
- Keep comments short and about the why, not the what. Dead code and commented-out blocks get removed.

## Licensing

The iOS-specific code in `Minecraft.Client/iOS/` is yours to relicense under whatever upstream eventually decides on. Don't add an explicit license header to individual files yet; the project-wide license will be decided once things stabilize.
