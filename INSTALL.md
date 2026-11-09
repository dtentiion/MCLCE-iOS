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

There are no game files included - you'll need to grab them from another (working) copy of the TU19 Windows64 build.

1. Launch the app, which will create a folder for it in the **Files** app - **On My iPhone > Minecraft LCE**
2. Copy the `Common`, `Windows64Media` and `music` folders from your Windows64 copy over to this location, preserving folder and file names
3. If you wish to use different icons for the PlayStation buttons, copy `skinHDOrbis.swf` and `skinOrbis.swf` from `OrbisMedia/Media` in the TU19 source to `OrbisMedia/Media` in the app's folder - this isn't essential
4. Make sure you've completely closed the app, then launch it again. Initial load will take slightly longer as it needs to unpack `MediaWindows64.arc`

Skin/texture packs should be placed in `Windows64Media/DLC`, just as they would be for Windows64. Other files are ignored, and the app creates its own `saves` folder and `settings.dat` as appropriate.

If you've set this up according to previous versions of these instructions, you shouldn't need to make any changes - existing files will be left untouched.

## Common problems

- 'Unable to install': the app was signed using a different Apple ID - redo this using the appropriate method
- 'App is damaged': the `.ipa` file may have been incorrectly re-zipped. Obtain a fresh copy from Releases, not from a third party
- 'Developer Mode required': see the relevant section above
- Controller isn't working: you'll need to be running iOS 14 or later, and (in the case of an Xbox Series controller) update the controller's firmware using the Xbox Accessories app on Windows
- Blank or incorrectly-displayed menus: `Common/Media/MediaWindows64.arc` is missing, or the app wasn't fully closed and reopened after copying

## Obtaining the latest build from Windows

There's a PowerShell script which will acquire the most recent CI build:

```powershell
.\scripts\fetch-latest-ipa.ps1
```

This uses the GitHub CLI, so if you've not already done so you'll need to authenticate by running `gh auth login`.
