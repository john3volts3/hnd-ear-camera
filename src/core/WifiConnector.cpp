#include "WifiConnector.h"
#include "Log.h"
#include <wlanapi.h>
#include <string>

namespace hndcam
{
    namespace
    {
        constexpr wchar_t CameraSsidPrefix[] = L"HNDEC_";
        constexpr DWORD ScanWaitMs = 3000;
        constexpr DWORD ConnectTimeoutMs = 12000;

        std::wstring SsidToString(const DOT11_SSID& ssid)
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(ssid.ucSSID), (int)ssid.uSSIDLength, nullptr, 0);
            std::wstring out(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(ssid.ucSSID), (int)ssid.uSSIDLength, out.data(), n);
            return out;
        }

        bool StringToSsid(const std::wstring& text, DOT11_SSID& ssid)
        {
            char utf8[64] = {};
            int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), utf8, sizeof(utf8), nullptr, nullptr);
            if (n <= 0 || n > DOT11_SSID_MAX_LENGTH)
                return false;
            ssid.uSSIDLength = (ULONG)n;
            memcpy(ssid.ucSSID, utf8, n);
            return true;
        }

        std::wstring XmlEscape(const std::wstring& text)
        {
            std::wstring out;
            for (wchar_t c : text)
            {
                switch (c)
                {
                case L'&': out += L"&amp;"; break;
                case L'<': out += L"&lt;"; break;
                case L'>': out += L"&gt;"; break;
                case L'"': out += L"&quot;"; break;
                default: out += c; break;
                }
            }
            return out;
        }

        std::wstring OpenNetworkProfile(const std::wstring& ssid)
        {
            const std::wstring name = XmlEscape(ssid);
            return L"<?xml version=\"1.0\"?>"
                L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
                L"<name>" + name + L"</name>"
                L"<SSIDConfig><SSID><name>" + name + L"</name></SSID></SSIDConfig>"
                L"<connectionType>ESS</connectionType>"
                L"<connectionMode>manual</connectionMode>"
                L"<MSM><security><authEncryption>"
                L"<authentication>open</authentication><encryption>none</encryption><useOneX>false</useOneX>"
                L"</authEncryption></security></MSM>"
                L"</WLANProfile>";
        }
    }

    WifiConnector::~WifiConnector()
    {
        if (_client)
            WlanCloseHandle(_client, nullptr);
    }

    bool WifiConnector::IsCameraSsid(const std::wstring& ssid)
    {
        return ssid.rfind(CameraSsidPrefix, 0) == 0;
    }

    bool WifiConnector::Open()
    {
        if (_client)
            return true;

        DWORD version = 0;
        DWORD err = WlanOpenHandle(2, nullptr, &version, &_client);
        if (err != ERROR_SUCCESS)
        {
            _client = nullptr;
            Log(L"Wi-Fi: WlanOpenHandle failed (%lu)", err);
            return false;
        }

        PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
        err = WlanEnumInterfaces(_client, nullptr, &interfaces);
        bool found = err == ERROR_SUCCESS && interfaces && interfaces->dwNumberOfItems > 0;
        if (found)
            _interface = interfaces->InterfaceInfo[0].InterfaceGuid;
        if (interfaces)
            WlanFreeMemory(interfaces);
        if (!found)
        {
            Log(L"Wi-Fi: no wireless interface (%lu)", err);
            WlanCloseHandle(_client, nullptr);
            _client = nullptr;
        }
        return found;
    }

    std::wstring WifiConnector::CurrentSsid()
    {
        if (!Open())
            return {};
        DWORD size = 0;
        PWLAN_CONNECTION_ATTRIBUTES attributes = nullptr;
        std::wstring ssid;
        if (WlanQueryInterface(_client, &_interface, wlan_intf_opcode_current_connection, nullptr, &size,
            reinterpret_cast<PVOID*>(&attributes), nullptr) == ERROR_SUCCESS && attributes)
        {
            if (attributes->isState == wlan_interface_state_connected)
                ssid = SsidToString(attributes->wlanAssociationAttributes.dot11Ssid);
            WlanFreeMemory(attributes);
        }
        return ssid;
    }

    std::wstring WifiConnector::JoinCameraNetwork(HANDLE stopEvent, const std::function<void(const std::wstring&)>& onJoining)
    {
        if (!Open())
            return {};

        // refresh the list of visible networks
        WlanScan(_client, &_interface, nullptr, nullptr, nullptr);
        if (WaitForSingleObject(stopEvent, ScanWaitMs) == WAIT_OBJECT_0)
            return {};

        PWLAN_AVAILABLE_NETWORK_LIST networks = nullptr;
        DWORD err = WlanGetAvailableNetworkList(_client, &_interface, 0, nullptr, &networks);
        if (err != ERROR_SUCCESS || !networks)
        {
            Log(L"Wi-Fi: WlanGetAvailableNetworkList failed (%lu)", err);
            return {};
        }

        std::wstring best;
        ULONG bestQuality = 0;
        for (DWORD i = 0; i < networks->dwNumberOfItems; i++)
        {
            const WLAN_AVAILABLE_NETWORK& n = networks->Network[i];
            const std::wstring ssid = SsidToString(n.dot11Ssid);
            if (!IsCameraSsid(ssid) || n.bSecurityEnabled || n.dot11BssType != dot11_BSS_type_infrastructure || !n.bNetworkConnectable)
                continue;
            if (best.empty() || n.wlanSignalQuality > bestQuality)
            {
                best = ssid;
                bestQuality = n.wlanSignalQuality;
            }
        }
        WlanFreeMemory(networks);

        if (best.empty() || best == CurrentSsid())
            return {}; // no camera network, or already on it (the camera may still be starting)

        Log(L"Wi-Fi: joining camera network %s (signal %lu%%)", best.c_str(), bestQuality);
        if (onJoining)
            onJoining(best);

        DOT11_SSID ssid{};
        if (!StringToSsid(best, ssid))
            return {};
        const std::wstring profile = OpenNetworkProfile(best);
        WLAN_CONNECTION_PARAMETERS params{};
        params.wlanConnectionMode = wlan_connection_mode_temporary_profile;
        params.strProfile = profile.c_str();
        params.pDot11Ssid = &ssid;
        params.dot11BssType = dot11_BSS_type_infrastructure;
        err = WlanConnect(_client, &_interface, &params, nullptr);
        if (err != ERROR_SUCCESS)
        {
            Log(L"Wi-Fi: WlanConnect failed (%lu)", err);
            return {};
        }

        const ULONGLONG deadline = GetTickCount64() + ConnectTimeoutMs;
        while (GetTickCount64() < deadline)
        {
            if (CurrentSsid() == best)
            {
                _joined = best;
                Log(L"Wi-Fi: connected to %s", best.c_str());
                return best;
            }
            if (WaitForSingleObject(stopEvent, 250) == WAIT_OBJECT_0)
                break;
        }
        Log(L"Wi-Fi: could not connect to %s", best.c_str());
        return {};
    }

    void WifiConnector::SetStreamingMode(bool enable)
    {
        if (!Open())
            return;
        BOOL value = enable ? TRUE : FALSE;
        DWORD err = WlanSetInterface(_client, &_interface, wlan_intf_opcode_media_streaming_mode, sizeof(value), &value, nullptr);
        Log(L"Wi-Fi: media streaming mode %s (%lu)", enable ? L"on" : L"off", err);
    }

    void WifiConnector::Release()
    {
        if (_joined.empty() || !_client)
            return;
        if (CurrentSsid() == _joined)
        {
            WlanDisconnect(_client, &_interface, nullptr);
            Log(L"Wi-Fi: disconnected from %s, Windows rejoins the usual network", _joined.c_str());
        }
        _joined.clear();
    }
}
