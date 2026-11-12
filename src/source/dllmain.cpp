#include "SourceCommon.h"
#include "Activator.h"
#include <strsafe.h>

namespace
{
    HMODULE g_module = nullptr;
    volatile LONG g_objects = 0;
    volatile LONG g_locks = 0;
    INIT_ONCE g_logOnce = INIT_ONCE_STATIC_INIT;

    BOOL CALLBACK InitLog(PINIT_ONCE, PVOID, PVOID*)
    {
        hndcam::LogInit(L"source.log");
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
        hndcam::Log(L"HndCamSource %s loaded in %s", HNDCAM_VERSION_WSTR, exe);
        return TRUE;
    }

    class ClassFactory :
        public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IClassFactory>
    {
    public:
        STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override
        {
            if (!object)
                return E_POINTER;
            *object = nullptr;
            if (outer)
                return CLASS_E_NOAGGREGATION;

            InitOnceExecuteOnce(&g_logOnce, InitLog, nullptr, nullptr);
            ComPtr<hndsrc::Activator> activator;
            HRESULT hr = Microsoft::WRL::MakeAndInitialize<hndsrc::Activator>(&activator);
            if (SUCCEEDED(hr))
                hr = activator->QueryInterface(riid, object);
            if (FAILED(hr))
                hndcam::LogHr(L"ClassFactory::CreateInstance", hr);
            return hr;
        }

        STDMETHODIMP LockServer(BOOL lock) override
        {
            if (lock)
                InterlockedIncrement(&g_locks);
            else
                InterlockedDecrement(&g_locks);
            return S_OK;
        }

    private:
        hndsrc::ObjectCounter _counter;
    };

    LSTATUS SetStringValue(HKEY key, const wchar_t* name, const wchar_t* value)
    {
        return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value), (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
    }
}

namespace hndsrc
{
    void ObjectCreated() { InterlockedIncrement(&g_objects); }
    void ObjectDestroyed() { InterlockedDecrement(&g_objects); }

    Settings ReadSettings()
    {
        Settings settings;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\HND Ear Camera", 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS)
        {
            DWORD value = 0, size = sizeof(value);
            if (RegQueryValueExW(key, L"AutoRotate", nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS)
                settings.autoRotate = value != 0;
            size = sizeof(value);
            if (RegQueryValueExW(key, L"RotationOffset", nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS)
                settings.rotationOffset = (double)(int)value; // signed degrees stored as REG_DWORD
            RegCloseKey(key);
        }
        return settings;
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

_Check_return_
STDAPI DllGetClassObject(_In_ REFCLSID clsid, _In_ REFIID riid, _Outptr_ LPVOID* object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (clsid != CLSID_HndCamSource)
        return CLASS_E_CLASSNOTAVAILABLE;

    ComPtr<ClassFactory> factory = Microsoft::WRL::Make<ClassFactory>();
    if (!factory)
        return E_OUTOFMEMORY;
    return factory->QueryInterface(riid, object);
}

__control_entrypoint(DllExport)
STDAPI DllCanUnloadNow()
{
    return (g_objects == 0 && g_locks == 0) ? S_OK : S_FALSE;
}

// A virtual camera media source must be registered in HKLM: the Frame Server services load it.
STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(g_module, path, ARRAYSIZE(path)))
        return HRESULT_FROM_WIN32(GetLastError());

    wchar_t keyPath[128];
    StringCchPrintfW(keyPath, ARRAYSIZE(keyPath), L"Software\\Classes\\CLSID\\%s\\InprocServer32", HNDCAM_CLSID_WSTR);
    HKEY key = nullptr;
    LSTATUS st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (st == ERROR_SUCCESS)
    {
        st = SetStringValue(key, nullptr, path);
        if (st == ERROR_SUCCESS)
            st = SetStringValue(key, L"ThreadingModel", L"Both");
        RegCloseKey(key);
    }
    return HRESULT_FROM_WIN32(st);
}

STDAPI DllUnregisterServer()
{
    wchar_t keyPath[128];
    StringCchPrintfW(keyPath, ARRAYSIZE(keyPath), L"Software\\Classes\\CLSID\\%s", HNDCAM_CLSID_WSTR);
    LSTATUS st = RegDeleteTreeW(HKEY_LOCAL_MACHINE, keyPath);
    return (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) ? S_OK : HRESULT_FROM_WIN32(st);
}
