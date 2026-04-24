# MCLCE-iOS

[![iOS build](https://github.com/dtentiion/MCLCE-iOS/actions/workflows/ios.yml/badge.svg)](https://github.com/dtentiion/MCLCE-iOS/actions/workflows/ios.yml)
[![Visits](https://hits.sh/github.com/dtentiion/MCLCE-iOS.svg?label=visits&color=6cc644)](https://hits.sh/github.com/dtentiion/MCLCE-iOS/)
[![Downloads](https://img.shields.io/github/downloads/dtentiion/MCLCE-iOS/total?color=6cc644&label=downloads)](https://github.com/dtentiion/MCLCE-iOS/releases)
[![Stars](https://img.shields.io/github/stars/dtentiion/MCLCE-iOS?color=6cc644)](https://github.com/dtentiion/MCLCE-iOS/stargazers)

Native iOS port of [MCLCE/MinecraftConsoles](https://github.com/MCLCE/MinecraftConsoles) (the TU19 Legacy Console Edition base).

**Status: menus mostly working, gameplay not started yet.** The app launches to the real LCE main menu with the panorama, logo, music, and controller navigation all working. You can walk through the full Help & Options / Settings tree. "Play Game" opens the world list but can't actually load a world yet because the save format reader isn't wired up. See [STATUS.md](STATUS.md) for the honest breakdown.

CI builds an `.ipa` on every push. Grab the latest from the [Actions tab](https://github.com/dtentiion/MCLCE-iOS/actions).

## What this is

A straight native iOS build of the LCE codebase. No emulator, no Wine, no JIT tricks. Regular `.ipa` you sideload with AltStore, Sideloadly, xtool, or TrollStore.

## Controls

Controller only. Xbox, PlayStation, or any MFi gamepad. iOS 14 and up supports Xbox and PS controllers natively. On-screen touch controls aren't planned.

## Supported devices

- iPhone and iPad
- iOS 15 or newer
- Controller required

## How it's built

The game is C++ with CMake, same as upstream. Since I only have a Windows machine, the actual iOS build runs on GitHub Actions macOS runners and the compiled `.ipa` gets published as a workflow artifact. See [.github/workflows/ios.yml](.github/workflows/ios.yml).

If you want to build it yourself you need a Mac with Xcode 15+. On Windows you can fork the repo, push a branch, and grab the artifact from Actions.

Under the hood:

- **UI:** the game's menus are all Flash (`.swf`). A custom [Ruffle](https://ruffle.rs/) fork runs them, with patches for the weirder things LCE's Flash authoring does. See `third_party/ruffle_ios/` and the fork at [dtentiion/ruffle](https://github.com/dtentiion/ruffle) (branch `mclce-ios-patches`).
- **Audio:** [miniaudio](https://miniaud.io), same single-header library the console LCE uses in `SoundEngine.cpp`. Plays the stock `.ogg` tracks directly, no transcoding.
- **Graphics:** wgpu via Ruffle, which routes through MetalANGLE on iOS.
- **Controller:** Apple's GameController framework, translated to the Xbox-360-style bitmask the game's 4J input layer already expects.

## Installing

See [INSTALL.md](INSTALL.md).

## Roadmap

See [ROADMAP.md](ROADMAP.md) and [STATUS.md](STATUS.md) for where things actually stand.

## Contributing

If you know iOS, Metal, GL ES, or anything about LCE's Flash UI internals, help is very welcome. Open an issue before starting anything big so we don't duplicate work.

For general LCE porting conventions follow the upstream [CONTRIBUTING.md](https://github.com/MCLCE/MinecraftConsoles/blob/main/CONTRIBUTING.md) from MinecraftConsoles. This repo inherits those rules.

## Legal

This project is based on the MinecraftConsoles source tree. Minecraft is a trademark of Mojang AB / Microsoft. Not affiliated with, endorsed by, or connected to Mojang, Microsoft, or 4J Studios. No Minecraft game assets are redistributed in this repo.
