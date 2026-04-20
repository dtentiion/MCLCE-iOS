# Installing MCLE-iOS

There is no playable build yet. When there is, download the latest `Minecraft.LCE.ipa` from [Releases](https://github.com/dtentiion/MCLE-iOS/releases) (once tagged) or from the Actions tab (every push builds one). Then pick one of the methods below.

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

## Troubleshooting

- "Unable to install": the app has been signed for another Apple ID. Re-sign via the method you used.
- "App is damaged": the .ipa may have been re-zipped with the wrong structure. Redownload from Actions artifacts, not from a third party repack.
- Controller does not appear: make sure you're on iOS 14 or newer. For Xbox Series pads, update the controller firmware via the Xbox Accessories app on a Windows machine once.

## Fetching the latest build from Windows

There is a helper script that pulls the newest CI artifact:

```powershell
.\scripts\fetch-latest-ipa.ps1
```

It uses the GitHub CLI. If you haven't authenticated yet, run `gh auth login` first.
