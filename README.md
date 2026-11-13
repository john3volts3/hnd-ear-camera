# CAM-Oreille: HND ear camera on Windows

[Version française](README.fr.md)

Use an **HND Wi-Fi ear camera** (HND-NE3, Wi-Fi network named `HNDEC_...`, normally used with the Android app "HND") on a Windows PC.

| Tool | What it does | Requirements |
|---|---|---|
| **CAM-Oreille.exe** | Live picture in your web browser, automatic rotation, photos; installs itself in `C:\CAM-OREILLE` with a desktop icon | Windows 10/11, no admin rights (except to turn a Wi-Fi adapter on) |
| HndCamSetup.exe *(set aside, not tested)* | Adds "HND Ear Camera" as a webcam (Camera app, Teams, Zoom, browsers) | Windows 11, admin rights once |

The app is in French when Windows is displayed in French or uses French regional formats, in English otherwise.

## Install
1. Get `CAM-Oreille-<version>.zip` (see [Share with friends](#share-with-friends)) and double-click `CAM-Oreille.exe` in it (no need to extract the zip).
2. Windows says "Windows protected your PC": click **More info**, then **Run anyway** (the program is not signed).
3. Click **Install and start**: the program is copied to `C:\CAM-OREILLE` (system drive), a **CAM-Oreille** icon is added to the desktop and an entry to Settings > Apps. No administrator rights are needed. **Start without installing** runs it once.
4. Next times: double-click the desktop icon (no Windows warning any more).

- **Update:** double-click a newer `CAM-Oreille.exe`: it offers **Update and start** (installed and new version shown).
- **Remove:** Settings > Apps > Installed apps > CAM-Oreille > Uninstall (desktop icon, Apps entry and folder are deleted).

## Use
1. Turn the camera on (blue light) and close the camera app on your phone.
2. Double-click the **CAM-Oreille** desktop icon.
3. **Wi-Fi turned off on the PC** (adapter disabled in Device Manager, or Wi-Fi button off): a window explains that the camera has its own Wi-Fi network and address range (192.168.1.x), and lists the Wi-Fi adapters of the PC. Pick one and click **Turn on Wi-Fi**: Windows asks for permission (administrator rights). With the box "Turn the Wi-Fi adapter off again when CAM-Oreille closes" ticked (default), the adapter goes back to its previous state at the end. With **Not now**, the page shows a reminder that Wi-Fi is off.
4. Your browser opens. The PC joins the camera Wi-Fi by itself and the picture appears within a few seconds. The camera can be turned on before or after CAM-Oreille.
5. Buttons: **Auto-rotate**, **Orientation** (90° / 5° steps, remembered), **Save photo** (saved as displayed). The version is shown under the title.
6. Close the browser tab: CAM-Oreille stops and the PC returns to its usual Wi-Fi.

Notes
- The camera accepts one client at a time (phone **or** PC).
- While the camera is used, a PC without a network cable has no internet: its Wi-Fi is on the camera network.
- A PC with a network cable keeps its internet through the cable: the camera Wi-Fi (54 Mbit/s) always comes second for Windows.
- Nothing is saved in the Wi-Fi settings of the PC (temporary connection profile).
- Diagnostic log: `%TEMP%\CAM-Oreille.log`.

A Python version of the viewer (standard library only, test tool) is in `tools/viewer` (`HND_Viewer.cmd`).

## Share with friends
Send the package `CAM-Oreille-<version>.zip` (program + bilingual notice):
- **WhatsApp:** send the `.zip` as a document.
- **E-mail:** mail services block `.exe` files, even inside a `.zip`: send a **download link** instead: the [latest release](../../releases/latest) page of this repository, or a Google Drive link.
- Do not send a script to run (`.cmd`, `.ps1`, command to paste in PowerShell): it is blocked by mail services and it is the method used by scams.

## Build
Requirements: Visual Studio Build Tools 2022 (C++ x64 workload, Windows SDK, CMake).

```
build.cmd
```

Output in `build\bin`: `CAM-Oreille.exe`, `HndCamProbe.exe` (console test tool), `HndCamSource.dll`, `HndCamSetup.exe`. The package is `CAM-Oreille.exe` + `tools/viewer/READ-ME_viewer.txt` (as `READ-ME - LISEZ-MOI.txt`, UTF-8 with BOM) in a `.zip`.

## How it works
The camera speaks a proprietary UDP protocol (discovery on port 46526, motion sensor on 52219, JPEG video on 44506), documented in [FSD.md](FSD.md) (French). Credits: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
