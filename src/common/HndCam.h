#pragma once

// Product-wide constants shared by the media source DLL, the setup and the tools.

#define HNDCAM_VERSION_MAJOR 1
#define HNDCAM_VERSION_MINOR 0
#define HNDCAM_VERSION_PATCH 0
#define HNDCAM_VERSION_STR "1.0.0"
#define HNDCAM_VERSION_WSTR L"1.0.0"

#define HNDCAM_PRODUCT_NAME L"HND Ear Camera"
#define HNDCAM_PUBLISHER L"hnd-ear-camera project"
#define HNDCAM_INSTALL_DIR_NAME L"HND Ear Camera"
#define HNDCAM_SOURCE_DLL_NAME L"HndCamSource.dll"
#define HNDCAM_SETUP_EXE_NAME L"HndCamSetup.exe"
#define HNDCAM_TASK_NAME L"HND Ear Camera - restore"
#define HNDCAM_UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\HndEarCamera"

// {43B7D877-ACE8-4D4D-9A8D-FA946B7B77AA}
#define HNDCAM_CLSID_WSTR L"{43B7D877-ACE8-4D4D-9A8D-FA946B7B77AA}"
#ifdef __cplusplus
#include <guiddef.h>
inline constexpr GUID CLSID_HndCamSource = { 0x43b7d877, 0xace8, 0x4d4d, { 0x9a, 0x8d, 0xfa, 0x94, 0x6b, 0x7b, 0x77, 0xaa } };
#endif

// Output video format exposed to Windows
#define HNDCAM_OUT_WIDTH 640
#define HNDCAM_OUT_HEIGHT 480
#define HNDCAM_OUT_FPS 30
