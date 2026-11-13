#include "WifiAdapters.h"
#include "WifiConnector.h"
#include "Log.h"
#include <objbase.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wlanapi.h>
#include <algorithm>

namespace hndcam
{
    namespace
    {
        // GUID_DEVCLASS_NET (devguid.h): network adapters
        constexpr GUID NetClassGuid = { 0x4d36e972, 0xe325, 0x11ce, { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
        constexpr DWORD IfTypeWifi = 71;          // IF_TYPE_IEEE80211
        constexpr DWORD NcfPhysical = 0x4;        // driver "Characteristics": physical adapter (excludes Wi-Fi Direct, VPN...)
        constexpr ULONGLONG InterfaceWaitMs = 15000; // the Wi-Fi interface appears a few seconds after the device starts

        DWORD ReadDword(HKEY key, const wchar_t* name)
        {
            DWORD value = 0, size = sizeof(value), type = 0;
            if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS || type != REG_DWORD)
                return 0;
            return value;
        }

        std::wstring ReadString(HKEY key, const wchar_t* name)
        {
            wchar_t value[128] = {};
            DWORD size = sizeof(value) - sizeof(wchar_t), type = 0;
            if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &size) != ERROR_SUCCESS || type != REG_SZ)
                return {};
            return value;
        }

        std::wstring DeviceText(HDEVINFO devs, SP_DEVINFO_DATA& info, DWORD property)
        {
            wchar_t text[256] = {};
            if (!SetupDiGetDeviceRegistryPropertyW(devs, &info, property, nullptr, reinterpret_cast<BYTE*>(text), sizeof(text) - sizeof(wchar_t), nullptr))
                return {};
            return text;
        }

        // Reads radioOn / hardwareOff from the Wi-Fi service. False when its interface is not there (yet).
        bool ReadRadio(WifiAdapter& adapter)
        {
            HANDLE client = nullptr;
            DWORD version = 0;
            if (WlanOpenHandle(2, nullptr, &version, &client) != ERROR_SUCCESS)
                return false;
            bool found = false;
            PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
            if (WlanEnumInterfaces(client, nullptr, &interfaces) == ERROR_SUCCESS && interfaces)
            {
                for (DWORD i = 0; i < interfaces->dwNumberOfItems && !found; i++)
                {
                    if (interfaces->InterfaceInfo[i].InterfaceGuid != adapter.interfaceGuid)
                        continue;
                    found = true;
                    bool softwareOn = true, hardwareOn = true;
                    WifiConnector::GetRadioState(client, adapter.interfaceGuid, softwareOn, hardwareOn);
                    adapter.radioOn = softwareOn && hardwareOn;
                    adapter.hardwareOff = !hardwareOn;
                }
                WlanFreeMemory(interfaces);
            }
            WlanCloseHandle(client, nullptr);
            return found;
        }

        bool CallPropertyChange(HDEVINFO devs, SP_DEVINFO_DATA& info, DWORD stateChange, DWORD scope)
        {
            SP_PROPCHANGE_PARAMS params{};
            params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
            params.StateChange = stateChange;
            params.Scope = scope;
            params.HwProfile = 0; // current hardware profile
            return SetupDiSetClassInstallParamsW(devs, &info, &params.ClassInstallHeader, sizeof(params))
                && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, devs, &info);
        }

        // Same calls as Device Manager / devcon: enable = global then current profile, disable = current profile.
        bool SetDeviceEnabled(const std::wstring& instanceId, bool enable)
        {
            HDEVINFO devs = SetupDiCreateDeviceInfoList(nullptr, nullptr);
            if (devs == INVALID_HANDLE_VALUE)
                return false;
            SP_DEVINFO_DATA info{ sizeof(info) };
            bool ok = SetupDiOpenDeviceInfoW(devs, instanceId.c_str(), nullptr, 0, &info) != FALSE;
            if (ok)
            {
                if (enable)
                    CallPropertyChange(devs, info, DICS_ENABLE, DICS_FLAG_GLOBAL); // fails harmlessly when only the profile is disabled
                ok = CallPropertyChange(devs, info, enable ? DICS_ENABLE : DICS_DISABLE, DICS_FLAG_CONFIGSPECIFIC);
            }
            if (ok)
                Log(L"Wi-Fi helper: adapter %s", enable ? L"enabled" : L"disabled");
            else
                LogHr(enable ? L"Wi-Fi helper: enable adapter" : L"Wi-Fi helper: disable adapter", HRESULT_FROM_WIN32(GetLastError()));
            SetupDiDestroyDeviceInfoList(devs);
            return ok;
        }

        // Software radio switch (the Wi-Fi button of Windows), for every PHY of the interface.
        bool SetRadio(const GUID& interfaceGuid, bool on)
        {
            HANDLE client = nullptr;
            DWORD version = 0;
            if (WlanOpenHandle(2, nullptr, &version, &client) != ERROR_SUCCESS)
                return false;

            std::vector<DWORD> phys{ 0 };
            DWORD size = 0;
            PWLAN_RADIO_STATE state = nullptr;
            if (WlanQueryInterface(client, &interfaceGuid, wlan_intf_opcode_radio_state, nullptr, &size,
                reinterpret_cast<PVOID*>(&state), nullptr) == ERROR_SUCCESS && state)
            {
                if (state->dwNumberOfPhys > 0)
                    phys.clear();
                for (DWORD i = 0; i < std::min<DWORD>(state->dwNumberOfPhys, WLAN_MAX_PHY_INDEX); i++)
                    phys.push_back(state->PhyRadioState[i].dwPhyIndex);
                WlanFreeMemory(state);
            }

            DWORD err = ERROR_SUCCESS;
            for (DWORD phy : phys)
            {
                WLAN_PHY_RADIO_STATE radio{};
                radio.dwPhyIndex = phy;
                radio.dot11SoftwareRadioState = on ? dot11_radio_state_on : dot11_radio_state_off;
                const DWORD e = WlanSetInterface(client, &interfaceGuid, wlan_intf_opcode_radio_state, sizeof(radio), &radio, nullptr);
                if (e != ERROR_SUCCESS)
                    err = e;
            }
            WlanCloseHandle(client, nullptr);
            Log(L"Wi-Fi helper: radio %s (%lu)", on ? L"on" : L"off", err);
            return err == ERROR_SUCCESS;
        }
    }

    std::vector<WifiAdapter> ListWifiAdapters()
    {
        std::vector<WifiAdapter> result;
        HDEVINFO devs = SetupDiGetClassDevsW(&NetClassGuid, nullptr, nullptr, DIGCF_PRESENT);
        if (devs == INVALID_HANDLE_VALUE)
            return result;

        SP_DEVINFO_DATA info{ sizeof(info) };
        for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &info); i++)
        {
            // the driver key tells Wi-Fi from other adapters, also for a disabled device
            HKEY key = SetupDiOpenDevRegKey(devs, &info, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
            if (key == INVALID_HANDLE_VALUE)
                continue;
            const DWORD ifType = ReadDword(key, L"*IfType");
            const DWORD characteristics = ReadDword(key, L"Characteristics");
            const std::wstring guidText = ReadString(key, L"NetCfgInstanceId");
            RegCloseKey(key);

            WifiAdapter adapter;
            if (ifType != IfTypeWifi || !(characteristics & NcfPhysical) || guidText.empty()
                || FAILED(CLSIDFromString(guidText.c_str(), &adapter.interfaceGuid)))
                continue;

            wchar_t id[MAX_DEVICE_ID_LEN] = {};
            if (!SetupDiGetDeviceInstanceIdW(devs, &info, id, ARRAYSIZE(id), nullptr))
                continue;
            adapter.instanceId = id;
            adapter.name = DeviceText(devs, info, SPDRP_FRIENDLYNAME);
            if (adapter.name.empty())
                adapter.name = DeviceText(devs, info, SPDRP_DEVICEDESC);

            ULONG status = 0, problem = 0;
            adapter.enabled = !(CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) == CR_SUCCESS
                && (status & DN_HAS_PROBLEM) && problem == CM_PROB_DISABLED);
            if (adapter.enabled)
                ReadRadio(adapter);
            result.push_back(adapter);
        }
        SetupDiDestroyDeviceInfoList(devs);
        return result;
    }

    int RunWifiOnHelper(DWORD viewerPid, bool restoreOnExit, const std::wstring& instanceId)
    {
        // opened first: the restore waits for the viewer to exit
        HANDLE viewer = OpenProcess(SYNCHRONIZE, FALSE, viewerPid);

        WifiAdapter adapter;
        bool found = false;
        for (const auto& a : ListWifiAdapters())
        {
            if (_wcsicmp(a.instanceId.c_str(), instanceId.c_str()) == 0)
            {
                adapter = a;
                found = true;
            }
        }
        if (!found)
        {
            Log(L"Wi-Fi helper: adapter %s not found", instanceId.c_str());
            if (viewer)
                CloseHandle(viewer);
            return 1;
        }
        Log(L"Wi-Fi helper: %s (enabled=%d, Wi-Fi on=%d)", adapter.name.c_str(), adapter.enabled, adapter.radioOn);

        bool enabledHere = false;
        bool radioHere = false;
        if (!adapter.enabled)
        {
            if (!SetDeviceEnabled(instanceId, true))
            {
                if (viewer)
                    CloseHandle(viewer);
                return 2;
            }
            enabledHere = true;
            const ULONGLONG deadline = GetTickCount64() + InterfaceWaitMs;
            while (!ReadRadio(adapter) && GetTickCount64() < deadline)
                Sleep(250);
        }
        if (!adapter.radioOn && !adapter.hardwareOff && SetRadio(adapter.interfaceGuid, true))
            radioHere = true;

        if (restoreOnExit && (enabledHere || radioHere))
        {
            if (viewer)
                WaitForSingleObject(viewer, INFINITE);
            Log(L"Wi-Fi helper: viewer closed, turning the adapter off again");
            if (radioHere)
                SetRadio(adapter.interfaceGuid, false);
            if (enabledHere)
                SetDeviceEnabled(instanceId, false);
        }
        if (viewer)
            CloseHandle(viewer);
        return 0;
    }
}
