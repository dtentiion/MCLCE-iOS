# MCLCE-iOS

[![iOS build](https://github.com/dtentiion/MCLCE-iOS/actions/workflows/ios.yml/badge.svg)](https://github.com/dtentiion/MCLCE-iOS/actions/workflows/ios.yml)
[![Visits](https://hits.sh/github.com/dtentiion/MCLCE-iOS.svg?label=visits&color=6cc644)](https://hits.sh/github.com/dtentiion/MCLCE-iOS/)
[![Downloads](https://img.shields.io/github/downloads/dtentiion/MCLCE-iOS/total?color=6cc644&label=downloads)](https://github.com/dtentiion/MCLCE-iOS/releases)
[![Stars](https://img.shields.io/github/stars/dtentiion/MCLCE-iOS?color=6cc644)](https://github.com/dtentiion/MCLCE-iOS/stargazers)

Native iOS port of [MCLCE/MinecraftConsoles](https://github.com/MCLCE/MinecraftConsoles) (the TU19 Legacy Console Edition base).

**Status: early work-in-progress. Nothing runs yet.** If you're looking for a playable build, this isn't it. Check back later or follow the roadmap.

The CI does produce a signed-on-install `.ipa` on every push (just an empty app shell at the moment; no game logic). Grab the latest from the [Actions tab](https://github.com/dtentiion/MCLCE-iOS/actions).

## What this is

The goal is a straight native iOS build of the LCE codebase. No emulator, no Wine, no JIT tricks. Regular `.ipa` you sideload with AltStore, Sideloadly, or xtool.

## Controls

Controller only for now (Xbox, PlayStation, or any MFi gamepad). iOS 14 and up supports Xbox/PS controllers natively, no pairing weirdness. On-screen touch controls might come later but aren't a priority.

## Supported devices

- iPhone and iPad
- iOS 14 or newer
- Controller required

## How it's built

The source code is C++ and CMake based, matching upstream. Since I only have a Windows machine, the actual iOS build runs on GitHub Actions macOS runners and the compiled `.ipa` gets published as a workflow artifact. See [.github/workflows/ios.yml](.github/workflows/ios.yml).

If you want to build it yourself you'll need a Mac with Xcode 15+. On Windows you can fork this repo, push a branch, and grab the artifact from Actions.

## Installing

Until there's a working build, this section is empty. When there is:

1. Download the latest `MCLCE-iOS.ipa` from Releases.
2. Install it with one of:
   - **AltStore** (needs free Apple ID, resigns every 7 days)
   - **Sideloadly** (same idea, desktop app)
   - **xtool** on Windows/Linux (see [xtool-org/xtool](https://github.com/xtool-org/xtool))
   - **TrollStore** if you're on a supported iOS version (permanent sign)

## Current blockers

Three big ones, in order of pain:

1. **Iggy**. The upstream UI system uses Iggy, a closed-source SWF renderer. Replacing it with [GameSWF](http://tulrich.com/geekstuff/gameswf.html) so it can render the same UI assets on iOS. In progress.
2. **Renderer**. Upstream uses D3D11. iOS only has Metal. Plan is to rewrite the renderer against GL ES 3.0 and let [MetalANGLE](https://github.com/kakashidinho/metalangle) translate to Metal. Not started.
3. **Windows-isms**. `Windows.h`, `wsock32`, `XInput`, `CRITICAL_SECTION`, and friends are all over the code. Each one needs a portable replacement or an iOS shim. Mostly straightforward, just tedious.

See [ROADMAP.md](ROADMAP.md) for the full plan.

## Contributing

If you know iOS, Metal, GL ES, or reverse-engineering SWF/Flash runtimes, help is very welcome. Open an issue before starting anything big so we don't duplicate work.

For general LCE porting conventions please follow the upstream [CONTRIBUTING.md](https://github.com/MCLCE/MinecraftConsoles/blob/main/CONTRIBUTING.md) from MinecraftConsoles. This repo inherits those rules.

## Legal

This project is based on the MinecraftConsoles source tree. Minecraft is a trademark of Mojang AB / Microsoft. Not affiliated with, endorsed by, or connected to Mojang, Microsoft, or 4J Studios. No Minecraft game assets are redistributed in this repo. You need a legal copy of the original game's data files to actually play.
