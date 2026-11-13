#pragma once
#include <string>

namespace hndcam
{
    // Installation in <system drive>\CAM-OREILLE, without administrator rights (any user may create a
    // folder at the root of the system drive): program copy, desktop icon, entry in Settings > Apps.
    // Offered when this copy is not the installed one and is newer (or nothing is installed).
    // Returns true when installed: the caller must exit and start 'installedExe' instead.
    bool InstallIfWanted(std::wstring& installedExe);

    // "CAM-Oreille.exe /uninstall" (Settings > Apps): removes the desktop icon, the Apps entry and the folder.
    int Uninstall();
}
