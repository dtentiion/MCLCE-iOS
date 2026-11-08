# Installing MCLCE-iOS

You can download the latest `Minecraft.LCE.ipa` from [Releases](https://github.com/dtentiion/MCLCE-iOS/releases/latest) - this is rebuilt on every push to `main`

There are several ways to get this onto your device...

## AltStore

This works on both Windows and macOS, and requires a free Apple ID

1. Get AltServer from https://altstore.io
2. Get the AltStore app on your device
3. Once installed, set up the app on your device using your Apple ID
4. Either drag the `.ipa` onto AltServer, or use the Files app on your device to 'share' it to AltStore
5. AltStore will sign and install it - this certificate is valid for 7 days, but AltStore will update it provided you use it at least once a week

## Sideloadly

This also works on both Windows and macOS

1. Get Sideloadly from https://sideloadly.io
2. Connect your device to your computer via USB
3. Drag the `.ipa` onto Sideloadly, use your Apple ID to authenticate and click 'Start'
4. On your device, go to Settings > General > VPN & Device Management and trust the relevant developer profile

## xtool (Linux / Windows)

Another option is xtool, which is a cross-platform alternative to Xcode

1. Get xtool from https://github.com/xtool-org/xtool
2. Run `xtool auth` to authenticate using your Apple ID
3. Run `xtool install Minecraft.LCE.ipa`, ensuring your device is connected

## TrollStore (supported iOS versions only)

Where your device is running an appropriate version of iOS (15.0 to 16.6.1, and a handful of others at the time of writing) you can use TrollStore to obtain a permanent certificate

1. Get TrollStore (see https://ios.cfw.guide/installing-trollstore/ for instructions relevant to your version)
2. Either AirDrop the `.ipa` to your device, or use the Files app to 'share' it
3. Open it within TrollStore and install

## Enable Developer Mode (iOS 16 and newer)

Where you're running iOS 16 or later, you'll be prompted to enable 'Developer Mode' the first time you launch such an app - you need to do this just once per device, and it isn't specific to this app

1. Go to Settings > Privacy & Security
2. Scroll to the bottom and select 'Developer Mode'
3. Toggle this to 'On', agreeing to the resulting reboot
4. Upon returning, agree to enable Developer Mode again, and enter your passcode
5. You should now be able to launch the application

## Getting the game files

You won't be able to include any files from the TU19 Windows64 build in the app, but you can copy some important ones from there.

1. Once you've launched the app, you should find its folder in the **Files** app, at **On My iPhone > Minecraft LCE**. All subsequent items should be added to this folder.
2. From a PC, we'll need to extract `Common/Media/MediaWindows64.arc`:
   ```
   python scripts/list-arc.py Common/Media/MediaWindows64.arc --extract arc-out
   ```
   (alternatively, [PCK Studio](https://github.com/LCERD/PCK-Studio) will do the job) and add all the resulting files to the top of the app's folder.
3. Add the following to the top of the app's folder too:
   - `Mojang Font_7.ttf` and `Mojang Font_11.ttf` from `Common/Media/font`
   - `Panorama_Background_N.png` and `Panorama_Background_S.png` from `Common/Media/Graphics`
   - all the files in `music/music` (the menu music)
   - `skinOrbis.swf` and `skinHDOrbis.swf` from `OrbisMedia/Media` (needed for the PlayStation button icons)
4. Create a new 'UI' folder in the app's folder, and add all the files from `Windows64Media/Sound/Minecraft/UI` to it (these are the menu sounds).
5. Add the following folders to the app's folder, retaining their current names:
   - `Common/res` to `Common/res`
   - `Windows64Media/DLC`, `Windows64Media/Sound` and `Windows64Media/Tutorial` to `Windows64Media`
   - `music` to `music`
6. Quit the app completely, then restart it.

Skin and texture packs should be placed in `Windows64Media/DLC`, just as they would be on a normal Windows64 installation. The app will create its own `saves` folder and `settings.dat`.

## Common problems

- 'Unable to install': the app was signed using a different Apple ID - redo this using the appropriate method
- 'App is damaged': the `.ipa` file may have been incorrectly re-zipped. Obtain a fresh copy from Releases, not from a third party
- 'Developer Mode required': see the relevant section above
- Controller isn't working: you'll need to be running iOS 14 or later, and (in the case of an Xbox Series controller) update the controller's firmware using the Xbox Accessories app on Windows
- Blank or incorrectly-displayed menus: one or more of the files mentioned in steps 2 or 3 may be missing

## Obtaining the latest build from Windows

There's a PowerShell script which will acquire the most recent CI build:

```powershell
.\scripts\fetch-latest-ipa.ps1
```

This uses the GitHub CLI, so if you've not already done so you'll need to authenticate by running `gh auth login`.
