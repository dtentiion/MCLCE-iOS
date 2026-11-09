# Contributing

This is a one-person hobby project, so there aren't elaborate procedures in place - but there are some guidelines to follow.

## What should you work on?

- items marked 'Known issues' in [STATUS.md](STATUS.md)
- items marked 'Upcoming' in [ROADMAP.md](ROADMAP.md)
- testing on an iPad, or other older iPhones / iOS releases, and raising an issue with results

And, crucially, raise an issue before doing anything substantial, to avoid duplication of effort.

## Ground rules

- strive to maintain parity with TU19 - where things behave differently to Windows64, try to emulate what TU19 does, not implement new, different, functionality
- don't include any minecraft game files in the repository - players provide these
- don't include any apple signing information (eg .p12, .mobileprovision, .p8) - we deliberately build unsigned .ipa files via CI

Small, well-focused changes are easier to identify the relevant code to isolate any problems - 'port bugs' are much more amenable to binary search than other types of issue.

## How the code is structured

- `upstream/` contains the TU19 source, via a submodule - don't modify this - if you need to make changes to some TU19 source which have implications for iOS, write a suitable script in `scripts/` (eg the existing `patch-*.py` scripts) which will be run by CI prior to building
- `Minecraft.Client/iOS/` contains the code required to run the game on iOS - the app itself, the metal renderer, the code which ties the other components together, input / storage / sound facilities and the menu / hud
- `third_party/ruffle_ios/` provides the interface to our modified version of ruffle, which renders the game's .swf based menus

## Sending changes

1. obtain a fork, and create a branch
2. build - if you have a mac and xcode 16 you can use the relevant cmake presets, otherwise push the branch and obtain the resulting .ipa from actions on your fork
3. test on a device, if possible
4. submit a PR against `main`, indicating what you've done, and why

## Style

- where appropriate, follow the existing coding conventions in the affected areas (eg use tabs, CamelCase for types and variables, 'm_' prefixes for member variables)
- new code in `Minecraft.Client/iOS/` should be written in C++17, or objective-c++
- comments should indicate the rationale for any particular decision - don't comment out code you wish to remove, simply remove it

There isn't at present a project wide licence - don't include licence markers on files at this time.
