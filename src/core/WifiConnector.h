#pragma once
#include <windows.h>
#include <functional>
#include <string>

namespace hndcam
{
    // Joins the Wi-Fi network of an HND camera (open network named "HNDEC_...") with the Native Wifi API.
    // A temporary profile is used: nothing is saved in the Wi-Fi settings of the PC.
    class WifiConnector
    {
    public:
        WifiConnector() = default;
        ~WifiConnector();

        WifiConnector(const WifiConnector&) = delete;
        WifiConnector& operator=(const WifiConnector&) = delete;

        // Scans and, when a camera network other than the current one is visible, connects to the strongest.
        // onJoining is called just before connecting. Returns the SSID joined, empty when nothing was done.
        std::wstring JoinCameraNetwork(HANDLE stopEvent, const std::function<void(const std::wstring&)>& onJoining);

        // Disconnects from the camera network if this object joined it: Windows then rejoins the usual network.
        void Release();

        // SSID of the current Wi-Fi connection, empty when not connected.
        std::wstring CurrentSsid();

        // Media streaming mode: Windows avoids background Wi-Fi scans, which freeze the video for a moment.
        // The setting lasts while this object is open, or until disabled.
        void SetStreamingMode(bool enable);

        static bool IsCameraSsid(const std::wstring& ssid);

        // False when no Wi-Fi adapter is enabled, or when Wi-Fi is turned off (Wi-Fi button, airplane mode,
        // physical switch). True when unknown (Wi-Fi service not reachable).
        static bool IsWifiOn();

        // Radio switches of one Wi-Fi interface: software = Wi-Fi button / airplane mode, hardware = physical switch.
        // Both stay true when the state cannot be read.
        static void GetRadioState(HANDLE client, const GUID& interfaceGuid, bool& softwareOn, bool& hardwareOn);

    private:
        bool Open();

        HANDLE _client = nullptr;
        GUID _interface{};
        std::wstring _joined;
    };
}
