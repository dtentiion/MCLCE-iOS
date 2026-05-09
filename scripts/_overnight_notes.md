# Overnight session notes

Session: 2026-04-22, after the "menu is interactive" milestones landed.

## What shipped while you slept

Each is its own commit on `main`. Newest first. Drop into AltStore/Sideloadly in whatever order, grab whichever build is latest when you wake.

1. **`audio: swap AVAudioPlayer for miniaudio, native OGG support`** —
   Replaces the iOS-native AVAudioPlayer path with miniaudio (same
   single-header library the console LCE uses in SoundEngine.cpp).
   Means you can **drop `menu1.ogg`..`menu4.ogg` straight from the
   console build's `music/music/` folder into Documents** — no
   transcoding. Shuffle logic preserved.

2. **`audio: shuffle across menu tracks`** — earlier MP3-only shuffle,
   now superseded by the miniaudio swap.

3. **`exit: Exit Game button terminates the app`** — pressing A on Button6
   ("Exit Game") now waits ~0.3s for the press animation, stops the menu
   music, and calls `exit(0)`. Release-build only concern. Mirrors
   console's Exit Game.

4. **`menu: generic per-scene button config; HelpAndOptions gets real
   labels`** — refactor + feature. The focus state machine no longer
   hardcodes MainMenu's six buttons; it reads from
   `self.menuButtonConfig` (set per scene by `-initMainMenuButtons` /
   `-initHelpAndOptionsButtons`). Options menu buttons now show
   **Change Skin / How to Play / Controls / Settings / Credits**
   instead of the `FJ_Label` placeholder. Nav wraps correctly on both
   6-button and 5-button menus.

5. **`audio: play menu music from Documents on loop`** — original
   AVAudioPlayer implementation; superseded by #1.

## What didn't ship (and why)

- **Panorama overlay** — needs multi-SWF compositing through Ruffle's
  `Loader` class. Tried the naive "instantiate_class_on_root('Panorama')"
  path earlier tonight; it instantiates the stub but the content
  (`Panorama_Background_S`) lives in a separate SWF (`Panorama1080.swf`)
  that isn't loaded. Proper fix requires exposing Loader.loadBytes or an
  equivalent `add_sibling_swf_to_root` FFI — 2-3 hours of careful
  surgery, too risky to attempt unsupervised. Deferred to next session.

- **Splash text (iggy_Splash)** — turned out `iggy_Splash` in MainMenu.swf
  is a chid=2 sprite placing just a Shape, with no AS3 class binding. It's
  a visual placeholder, not a text object. To put real splash text there
  we'd need to create and attach a TextField dynamically. Deferred.

## Test checklist for the morning

Sideload the newest IPA (whichever of the builds above is highest in
GHA — the miniaudio one if it built green). Plug in controller.

- [ ] Menu music plays. Copy `menu1.ogg`..`menu4.ogg` from
      `C:\Users\bilaw\Documents\GitHub\MinecraftConsoles\build\windows64\Minecraft.Client\Release\music\music\`
      into the device's app Documents folder. Should shuffle across
      all four tracks. If miniaudio build failed and you fall back to
      the earlier MP3 build, `Downloads/menu_music.mp3` is pre-
      transcoded and ready.
- [ ] Labels appear on main menu (Play Game, Leaderboards, etc.)
- [ ] D-pad walks focus up/down, wrapping
- [ ] A on Help & Options transitions to options menu
- [ ] Options menu buttons show labels (Change Skin, How to Play, etc.)
- [ ] D-pad wraps through all 5 options buttons correctly
- [ ] B returns to main menu, labels restored
- [ ] A on Exit Game closes the app

## Known issues to expect

- Main menu background is still grey — panorama not yet rendered
- Options menu has 7 SWF slots but only 5 are labeled; the two unlabeled
  ones (Button6, Button7) still exist in the SWF and will participate in
  D-pad nav as empty placeholders. Cleanup is `removeControl()`-equivalent
  which I haven't ported yet.
- Pressing A on main menu buttons other than Help & Options / Exit Game
  just logs — no scene transitions wired for Leaderboards / Achievements /
  DLC / Play Game yet.

## Next-session priorities (suggested)

1. Panorama via Loader.loadBytes (~2h, big visual payoff)
2. Splash text (needs TextField creation FFI)
3. Wire more main menu scene transitions (each one is ~5 min of config,
   assuming target SWFs are available)
4. Remove empty Button6/Button7 from HelpAndOptions view
