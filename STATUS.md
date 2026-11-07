# Status

What can it do?

All of the items listed below have been tested on an iPhone 16e, and run entirely on-device.

## Known good

**Startup and menus**

- Intro screens, autosave message, and the main menu (including day/night panorama, logo, splash text and menu music)
- Help & Options - all five 'Settings' pages, How to Play, Controls, Credits and Change Skin (with 3D previews, and any skin packs you provide)
- Leaderboards and access to the Minecraft Store - there's no online service required to use these
- Exit Game

**Worlds**

- World selection (with thumbnails), the Load menu (including game mode and host options) and Save Options (to rename and/or delete worlds)
- ability to Create New World - providing a name and seed via the controller keyboard, More Options and texture packs
- the tutorial world
- saving from the pause menu, via the autosave timer, and when the application goes to the background

**In game**

- all aspects of Survival and Creative gameplay, including world creation, mobs, building and mining, eating, bows, sleeping and experience
- relevant information on screen (hotbar, health/hunger/armour/air/xp, horse health, item names, chat and death messages, paperdoll)
- the pause and host options menus, death screen and respawning
- inventory, 2x2/3x3 crafting, the creative menu, chests, furnace, dispenser, hopper, brewing stand, enchanting table, anvil, beacon, villager interaction, horse equipment, fireworks and signs
- the Nether and End dimensions, and the relevant end poem
- maps showing your location, animated water, lava and fire, weather and lightning
- appropriate music and sound effects (including footstep and other ambient sounds)
- texture and skin packs from your supplied files

Other features (e.g. inventory sorting) are missing, as are some aspects of world creation and game-play.

## Controllers

Xbox, PlayStation and MFi pads. PlayStation button icons appear if a PlayStation pad is connected.

## Performance

60 fps in a world on an iPhone 16e, using ~720 MB of memory

## Features not included

* Any form of multiplayer (LAN, online, split-screen)
* Controller 'rumble'
* Keyboard/mouse

## Known issues

* Rain/thunder may appear out-of-nowhere, rather than slowly building up
* If you pick up an item whilst the crafting interface is displayed, the available recipes won't update until you revisit the interface
* 'Eyes of ender' may fail to locate a stronghold in worlds created using previous versions of this app
* There can be up to 3 minute gaps between pieces of menu music (closer to 1 minute on Windows64)
* Only tested on iPhone so far

## Build details

* Game code - TU19 compiled for iOS, with some additional code to deal with differences to the windows/console versions (eg. file and storage facilities, threading)
* Rendering - the game's draw calls (via 4J) are implemented against a Metal API, and normal OpenGL calls are redirected through this
* UI - the game's SWF based menus and in-game 'HUD' are displayed using a modified version of [Ruffle](https://github.com/dtentiion/ruffle) ('mclce-ios-patches' branch), overlaid on top of the rendered world
* Audio - miniaudio, used by the console version
* Controller - Apple's GameController framework, configured to match expectations of the game
* Saves - in the app's 'Documents' directory

## Other details

The `.ipa` binaries available via Releases are unsigned, and can be signed by various sideloading tools (eg. AltStore, Sideloadly, xtool, TrollStore) upon installation

There are no game specific assets included - see [INSTALL.md](INSTALL.md) for more details

See [ROADMAP.md](ROADMAP.md) for future plans
