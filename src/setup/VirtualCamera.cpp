#include <windows.h>
#include <wrl/client.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>
#include "VirtualCamera.h"
#include "HndCam.h"
#include "Log.h"
#include <initguid.h>
#include <devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfvirtualcamera.h>
// ks headers last: ks.h redefines GUID_NULL
#include <ks.h>
#include <ksmedia.h>

using Microsoft::WRL::ComPtr;
using hndcam::Log;
using hndcam::LogHr;

namespace setup
{
    namespace
    {
        HRESULT CreateCamera(IMFVirtualCamera** camera)
        {
            return MFCreateVirtualCamera(
                MFVirtualCameraType_SoftwareCameraSource,
                MFVirtualCameraLifetime_System,
                MFVirtualCameraAccess_AllUsers,
                HNDCAM_PRODUCT_NAME,
                HNDCAM_CLSID_WSTR,
                nullptr,
                0,
                camera);
        }

        bool IsOurDevice(const wchar_t* symbolicLink)
        {
            HKEY key = nullptr;
            if (CM_Open_Device_Interface_KeyW(symbolicLink, KEY_READ, RegDisposition_OpenExisting, &key, 0) != CR_SUCCESS)
                return false;
            wchar_t clsid[64] = {};
            DWORD size = sizeof(clsid) - sizeof(wchar_t);
            bool match = RegQueryValueExW(key, L"CustomCaptureSourceClsid", nullptr, nullptr, reinterpret_cast<BYTE*>(clsid), &size) == ERROR_SUCCESS
                && _wcsicmp(clsid, HNDCAM_CLSID_WSTR) == 0;
            RegCloseKey(key);
            return match;
        }

        // Symbolic links of the camera devices backed by our CLSID
        std::vector<std::wstring> FindOurDevices()
        {
            std::vector<std::wstring> result;
            ComPtr<IMFAttributes> attributes;
            if (FAILED(MFCreateAttributes(&attributes, 2)))
                return result;
            attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
            attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, KSCATEGORY_VIDEO_CAMERA);

            IMFActivate** activates = nullptr;
            UINT32 count = 0;
            if (FAILED(MFEnumDeviceSources(attributes.Get(), &activates, &count)))
                return result;

            for (UINT32 i = 0; i < count; i++)
            {
                wchar_t* link = nullptr;
                UINT32 length = 0;
                if (SUCCEEDED(activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &length)))
                {
                    if (IsOurDevice(link))
                        result.emplace_back(link);
                    CoTaskMemFree(link);
                }
                activates[i]->Release();
            }
            CoTaskMemFree(activates);
            return result;
        }

        HRESULT RemoveDevice(const std::wstring& symbolicLink)
        {
            wchar_t instanceId[MAX_DEVICE_ID_LEN] = {};
            ULONG size = sizeof(instanceId);
            DEVPROPTYPE type = 0;
            CONFIGRET cr = CM_Get_Device_Interface_PropertyW(symbolicLink.c_str(), &DEVPKEY_Device_InstanceId, &type, reinterpret_cast<BYTE*>(instanceId), &size, 0);
            DEVINST devInst = 0;
            if (cr == CR_SUCCESS)
                cr = CM_Locate_DevNodeW(&devInst, instanceId, CM_LOCATE_DEVNODE_NORMAL | CM_LOCATE_DEVNODE_PHANTOM);
            if (cr == CR_SUCCESS)
            {
                PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
                wchar_t vetoName[MAX_PATH] = {};
                CM_Query_And_Remove_SubTreeW(devInst, &veto, vetoName, MAX_PATH, CM_REMOVE_NO_RESTART);
                cr = CM_Uninstall_DevNode(devInst, 0);
            }
            return HRESULT_FROM_WIN32(CM_MapCrToWin32Err(cr, ERROR_INVALID_DATA));
        }
    }

    HRESULT RegisterVirtualCamera()
    {
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(&camera);
        if (FAILED(hr))
        {
            LogHr(L"MFCreateVirtualCamera", hr);
            return hr;
        }
        hr = camera->Start(nullptr);
        if (FAILED(hr))
        {
            LogHr(L"IMFVirtualCamera::Start", hr);
            camera->Shutdown();
            return hr;
        }
        // with a system lifetime the camera stays registered after this process exits
        camera->Shutdown();
        Log(L"Virtual camera registered");
        return S_OK;
    }

    HRESULT RemoveVirtualCamera()
    {
        HRESULT result = S_OK;
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(&camera);
        if (SUCCEEDED(hr))
        {
            hr = camera->Remove();
            if (FAILED(hr))
                LogHr(L"IMFVirtualCamera::Remove", hr);
            camera->Shutdown();
        }
        else
        {
            LogHr(L"MFCreateVirtualCamera (for removal)", hr);
        }

        // fallback: remove any device still backed by our media source
        for (const auto& link : FindOurDevices())
        {
            HRESULT hrDev = RemoveDevice(link);
            Log(L"Removing device %s: 0x%08X", link.c_str(), (unsigned)hrDev);
            if (FAILED(hrDev))
                result = hrDev;
        }
        return IsVirtualCameraPresent() ? (FAILED(result) ? result : E_FAIL) : S_OK;
    }

    bool IsVirtualCameraPresent()
    {
        return !FindOurDevices().empty();
    }
}
