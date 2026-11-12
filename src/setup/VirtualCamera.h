#pragma once
#include <windows.h>

// Windows 11 virtual camera registration (MFCreateVirtualCamera). MFStartup must have been called.
namespace setup
{
    // Creates the "HND Ear Camera" software camera (system lifetime, all users) and starts it.
    HRESULT RegisterVirtualCamera();

    // Removes the virtual camera (Media Foundation API, then device cleanup by CLSID as a fallback).
    HRESULT RemoveVirtualCamera();

    // True when a camera device backed by our media source CLSID is present.
    bool IsVirtualCameraPresent();
}
