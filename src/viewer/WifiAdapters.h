#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace hndcam
{
    // A physical Wi-Fi adapter of this PC, including adapters disabled in Device Manager.
    struct WifiAdapter
    {
        std::wstring name;        // as shown in Device Manager
        std::wstring instanceId;  // device instance ID
        GUID interfaceGuid{};     // network interface (NetCfgInstanceId)
        bool enabled = false;     // device enabled
        bool radioOn = false;     // enabled, with Wi-Fi turned on (not off with the Wi-Fi button or airplane mode)
        bool hardwareOff = false; // turned off with a physical switch: software cannot turn it on
    };

    // Works without administrator rights.
    std::vector<WifiAdapter> ListWifiAdapters();

    // Administrator side, started by the viewer as "HndViewer.exe /wifi-on <viewer pid> <0|1> <instance id>":
    // enables the adapter and turns its Wi-Fi on. With 1, turns them off again when the viewer process exits.
    int RunWifiOnHelper(DWORD viewerPid, bool restoreOnExit, const std::wstring& instanceId);
}
