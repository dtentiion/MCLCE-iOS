# Installing MCLCE-iOS

There is no playable build yet. When there is, download the latest `Minecraft.LCE.ipa` from [Releases](https://github.com/dtentiion/MCLCE-iOS/releases) (once tagged) or from the Actions tab (every push builds one). Then pick one of the methods below.

## AltStore

Works on Windows and macOS. Requires a free Apple ID.

1. Install AltServer on your computer from https://altstore.io
2. Install the AltStore app on your iPhone/iPad
3. Trust your device and sign in with an Apple ID inside AltStore
4. Drag `Minecraft.LCE.ipa` into AltStore on your computer, or share it to AltStore from the Files app on your device
5. AltStore signs and installs. Cert lasts 7 days; AltStore refreshes it automatically if you open the app at least once a week

## Sideloadly

Works on Windows and macOS. Same basic idea as AltStore, different tooling.

1. Download Sideloadly from https://sideloadly.io
2. Plug in your iPhone with a USB cable
3. Drag the `.ipa` into Sideloadly, sign in with your Apple ID, click Start
4. Trust the developer profile on the device under Settings > General > VPN & Device Management

## xtool (Linux / Windows)

xtool is a cross-platform Xcode replacement. Useful if you don't want to install AltServer.

1. Install xtool from https://github.com/xtool-org/xtool
2. `xtool auth` to sign in with your Apple ID
3. `xtool install Minecraft.LCE.ipa` with your device plugged in

## TrollStore (supported iOS versions only)

If your device is on an iOS version TrollStore supports (15.0 to 16.6.1, plus a few others at time of writing), TrollStore signs the .ipa permanently with no 7-day cert dance.

1. Have TrollStore installed (check https://ios.cfw.guide/installing-trollstore/ for your version)
2. AirDrop the `.ipa` to your device or share it via Files
3. Open in TrollStore and install

## Providing your own LCE assets

The app cannot ship Minecraft content because it is copyrighted by Mojang. You provide your own:

1. On a PC, extract `MediaWindows64.arc` from your LCE install using [PCK Studio](https://github.com/LCERD/PCK-Studio) or our bundled `scripts/list-arc.py` helper.
2. Copy every `.swf` from the extracted folder into a single directory. The app will look for them by name. The ones the menu tree currently uses include:
   - `MainMenu1080.swf`, `HelpAndOptionsMenu1080.swf`, `SettingsMenu1080.swf`
   - All five `Settings*Menu1080.swf` (Options, Audio, Control, Graphics, UI)
   - `LoadOrJoinMenu1080.swf`, `HowToPlayMenu1080.swf`, `LeaderboardMenu1080.swf`, `DLCMainMenu1080.swf`, `SkinSelectMenu1080.swf`
   - `MessageBox1080.swf`, `MenuBackground1080.swf`, `Panorama1080.swf`, `ToolTips1080.swf`, `ComponentLogo1080.swf`
   - The base skin set: `skinHD.swf`, `skinHDGraphics.swf`, `skinHDLabels.swf`, plus `skinHDWin.swf` (this last one is what the controller-button tooltip glyphs come from on the Windows64 dump; the app aliases it as `platformskinHD.swf` automatically on first launch).
3. On the iPhone, open the **Files** app.
4. Navigate to **On My iPhone** then the **Minecraft LCE** folder (the app creates it the first time you launch).
5. Drop the SWFs in.
6. Relaunch the app. It looks in this folder on startup and uses whatever it finds.

Missing SWFs degrade gracefully: a missing `Panorama1080.swf` just leaves the menu background blank, missing `ToolTips1080.swf` hides the bottom hint strip, etc. The minimum to see anything is `MainMenu1080.swf`.

## After the first install: enable Developer Mode

On iOS 16 and newer, the first time you open a sideloaded app the system will refuse and tell you "Developer Mode required". One-time toggle:

1. Settings -> Privacy & Security
2. Scroll to the bottom, tap **Developer Mode**
3. Flip it **On**, confirm the restart prompt
4. After the phone reboots, confirm once more and enter your passcode
5. Relaunch the app and it opens normally

You only do this once per device. It is not specific to this app.

## Troubleshooting

- "Unable to install": the app has been signed for another Apple ID. Re-sign via the method you used.
- "App is damaged": the .ipa may have been re-zipped with the wrong structure. Redownload from Actions artifacts, not from a third party repack.
- "Developer Mode required": see the section above.
- Controller does not appear: make sure you're on iOS 14 or newer. For Xbox Series pads, update the controller firmware via the Xbox Accessories app on a Windows machine once.

## Fetching the latest build from Windows

There is a helper script that pulls the newest CI artifact:

```powershell
.\scripts\fetch-latest-ipa.ps1
```

It uses the GitHub CLI. If you haven't authenticated yet, run `gh auth login` first.
