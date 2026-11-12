# HND Ear Camera for Windows

[Version française](README.fr.md)

Use an **HND Wi-Fi ear camera** (HND-NE3, Wi-Fi network named `HNDEC_...`, normally used with the Android app "HND") on a Windows PC.

| Tool | What it does | Requirements |
|---|---|---|
| **HndViewer.exe** | Live picture in your web browser, automatic rotation, photos | Windows 10/11, nothing to install, no admin rights |
| HndCamSetup.exe *(in progress)* | Adds "HND Ear Camera" as a webcam (Camera app, Teams, Zoom, browsers) | Windows 11, admin rights once |

## Viewer: quick start
1. Turn the camera on (blue light) and close the camera app on your phone.
2. Double-click `HndViewer.exe`. On first launch Windows may say "Windows protected your PC": click **More info**, then **Run anyway** (the program is not signed).
3. Your browser opens. The PC joins the camera Wi-Fi by itself and the picture appears within a few seconds. The camera can be turned on before or after the viewer.
4. Buttons: **Auto-rotate**, **Orientation** (90° / 5° steps, remembered), **Save photo** (saved as displayed).
5. Close the browser tab: the viewer stops and the PC returns to its usual Wi-Fi.

Notes
- The camera accepts one client at a time (phone **or** PC).
- While the camera is used, a PC without a network cable has no internet: its Wi-Fi is on the camera network.
- Nothing is saved in the Wi-Fi settings of the PC (temporary connection profile).
- Diagnostic log: `%TEMP%\HndViewer.log`.

A Python version of the viewer (standard library only) is in `tools/viewer` (`HND_Viewer.cmd`).

## Build
Requirements: Visual Studio Build Tools 2022 (C++ x64 workload, Windows SDK, CMake).

```
build.cmd
```

Output in `build\bin`: `HndViewer.exe`, `HndCamProbe.exe` (console test tool), `HndCamSource.dll`, `HndCamSetup.exe`.

## How it works
The camera speaks a proprietary UDP protocol (discovery on port 46526, motion sensor on 52219, JPEG video on 44506), documented in [FSD.md](FSD.md) (French). Credits: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
