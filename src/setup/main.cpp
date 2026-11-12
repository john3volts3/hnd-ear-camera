// HndCamSetup: self-contained installer of the HND Ear Camera virtual camera (Windows 11).
//   HndCamSetup.exe                     interactive: install / uninstall
//   HndCamSetup.exe /install [/quiet]
//   HndCamSetup.exe /uninstall [/quiet]
//   HndCamSetup.exe /restore            run by the startup task: re-creates the camera when missing
// The media source DLL is embedded as a resource and copied to %ProgramFiles%\HND Ear Camera.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>
#include <mfapi.h>
#include <string>
#include "HndCam.h"
#include "Log.h"
#include "VirtualCamera.h"
#include "resource.h"

using hndcam::Log;
using hndcam::LogHr;

namespace
{
    bool g_quiet = false;
    constexpr wchar_t SettingsKey[] = L"Software\\HND Ear Camera";

    // --- helpers ----------------------------------------------------------------------------------
    std::wstring KnownFolder(REFKNOWNFOLDERID id)
    {
        PWSTR path = nullptr;
        std::wstring result;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
            result = path;
        CoTaskMemFree(path);
        return result;
    }

    std::wstring InstallDir() { return KnownFolder(FOLDERID_ProgramFiles) + L"\\" HNDCAM_INSTALL_DIR_NAME; }
    std::wstring DataDir() { return KnownFolder(FOLDERID_ProgramData) + L"\\" HNDCAM_INSTALL_DIR_NAME; }
    std::wstring DllPath() { return InstallDir() + L"\\" HNDCAM_SOURCE_DLL_NAME; }
    std::wstring InstalledSetupPath() { return InstallDir() + L"\\" HNDCAM_SETUP_EXE_NAME; }

    std::wstring SelfPath()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        return path;
    }

    std::wstring ErrorText(HRESULT hr)
    {
        wchar_t text[512] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)hr, 0, text, ARRAYSIZE(text), nullptr);
        wchar_t line[640];
        swprintf_s(line, L"Error 0x%08X: %s", (unsigned)hr, text);
        return line;
    }

    void ShowMessage(PCWSTR instruction, const std::wstring& content, PCWSTR icon)
    {
        if (!g_quiet)
            TaskDialog(nullptr, nullptr, HNDCAM_PRODUCT_NAME, instruction, content.c_str(), TDCBF_OK_BUTTON, icon, nullptr);
    }

    bool IsWindows11()
    {
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        RTL_OSVERSIONINFOW info{ sizeof(info) };
        return rtlGetVersion && rtlGetVersion(&info) == 0 && info.dwMajorVersion >= 10 && info.dwBuildNumber >= 22000;
    }

    HRESULT RunHidden(std::wstring commandLine, DWORD* exitCode)
    {
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            return HRESULT_FROM_WIN32(GetLastError());
        WaitForSingleObject(pi.hProcess, 60000);
        GetExitCodeProcess(pi.hProcess, exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return S_OK;
    }

    std::wstring SchTasks()
    {
        wchar_t dir[MAX_PATH] = {};
        GetSystemDirectoryW(dir, ARRAYSIZE(dir));
        return std::wstring(L"\"") + dir + L"\\schtasks.exe\"";
    }

    // Deletes a file now, or at the next reboot when it is in use
    void DeleteOrSchedule(const std::wstring& path)
    {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return;
        if (!DeleteFileW(path.c_str()))
        {
            MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            Log(L"%s is in use, deletion scheduled at reboot", path.c_str());
        }
    }

    // Moves an in-use file aside so a new version can be written, and schedules the old one for deletion
    void MoveAside(const std::wstring& path)
    {
        wchar_t aside[MAX_PATH];
        swprintf_s(aside, L"%s.%llu.old", path.c_str(), GetTickCount64());
        if (MoveFileExW(path.c_str(), aside, MOVEFILE_REPLACE_EXISTING))
            MoveFileExW(aside, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    HRESULT WriteAll(const std::wstring& path, const void* data, DWORD size)
    {
        for (int attempt = 0; attempt < 2; attempt++)
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                DWORD written = 0;
                BOOL ok = WriteFile(h, data, size, &written, nullptr) && written == size;
                CloseHandle(h);
                return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
            }
            DWORD err = GetLastError();
            if (attempt == 0 && (err == ERROR_SHARING_VIOLATION || err == ERROR_ACCESS_DENIED))
            {
                MoveAside(path); // loaded by the Frame Server: replace it anyway
                continue;
            }
            return HRESULT_FROM_WIN32(err);
        }
        return E_FAIL;
    }

    HRESULT ExtractDll(const std::wstring& path)
    {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SOURCE_DLL), RT_RCDATA);
        HGLOBAL data = res ? LoadResource(nullptr, res) : nullptr;
        const void* bytes = data ? LockResource(data) : nullptr;
        if (!bytes)
            return HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND);
        return WriteAll(path, bytes, SizeofResource(nullptr, res));
    }

    HRESULT CopySelf(const std::wstring& destination)
    {
        const std::wstring self = SelfPath();
        if (_wcsicmp(self.c_str(), destination.c_str()) == 0)
            return S_OK;
        if (CopyFileW(self.c_str(), destination.c_str(), FALSE))
            return S_OK;
        MoveAside(destination);
        return CopyFileW(self.c_str(), destination.c_str(), FALSE) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }

    // files received through a browser or a messaging app carry the "Mark of the Web"
    void RemoveMarkOfTheWeb(const std::wstring& path)
    {
        DeleteFileW((path + L":Zone.Identifier").c_str());
    }

    LSTATUS SetString(HKEY key, const wchar_t* name, const std::wstring& value)
    {
        return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    }

    LSTATUS SetDword(HKEY key, const wchar_t* name, DWORD value)
    {
        return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    HRESULT RegisterCom(const std::wstring& dllPath)
    {
        std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + HNDCAM_CLSID_WSTR + L"\\InprocServer32";
        HKEY key = nullptr;
        LSTATUS st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keyPath.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
        if (st == ERROR_SUCCESS)
        {
            st = SetString(key, nullptr, dllPath);
            if (st == ERROR_SUCCESS)
                st = SetString(key, L"ThreadingModel", L"Both");
            RegCloseKey(key);
        }
        return HRESULT_FROM_WIN32(st);
    }

    void UnregisterCom()
    {
        std::wstring keyPath = std::wstring(L"Software\\Classes\\CLSID\\") + HNDCAM_CLSID_WSTR;
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, keyPath.c_str());
    }

    // default settings, kept when they already exist (update)
    void WriteDefaultSettings()
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, SettingsKey, 0, nullptr, 0, KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return;
        DWORD value = 0, size = sizeof(value);
        if (RegQueryValueExW(key, L"AutoRotate", nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS)
            SetDword(key, L"AutoRotate", 1);
        size = sizeof(value);
        if (RegQueryValueExW(key, L"RotationOffset", nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS)
            SetDword(key, L"RotationOffset", 0);
        SetString(key, L"Version", HNDCAM_VERSION_WSTR);
        RegCloseKey(key);
    }

    // %ProgramData%\HND Ear Camera\logs, writable by the Frame Server (Local Service)
    void PrepareLogDirectory()
    {
        const std::wstring data = DataDir();
        const std::wstring logs = data + L"\\logs";
        CreateDirectoryW(data.c_str(), nullptr);

        PSECURITY_DESCRIPTOR sd = nullptr;
        // System, Administrators: full; Local Service, Users, app containers: modify
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;LS)(A;OICI;0x1301bf;;;BU)(A;OICI;0x1301bf;;;AC)",
            SDDL_REVISION_1, &sd, nullptr))
            return;
        SECURITY_ATTRIBUTES sa{ sizeof(sa), sd, FALSE };
        if (!CreateDirectoryW(logs.c_str(), &sa) && GetLastError() == ERROR_ALREADY_EXISTS)
            SetFileSecurityW(logs.c_str(), DACL_SECURITY_INFORMATION, sd);
        LocalFree(sd);
    }

    HRESULT CreateStartupTask(const std::wstring& setupPath)
    {
        // the virtual camera does not survive a reboot: re-create it at startup
        std::wstring cmd = SchTasks() + L" /Create /F /TN \"" HNDCAM_TASK_NAME L"\" /SC ONSTART /DELAY 0000:30 /RU SYSTEM /RL HIGHEST /TR \"\\\""
            + setupPath + L"\\\" /restore\"";
        DWORD code = 1;
        HRESULT hr = RunHidden(cmd, &code);
        if (SUCCEEDED(hr) && code != 0)
            hr = E_FAIL;
        Log(L"Create startup task: 0x%08X (exit %lu)", (unsigned)hr, code);
        return hr;
    }

    void DeleteStartupTask()
    {
        DWORD code = 0;
        RunHidden(SchTasks() + L" /Delete /F /TN \"" HNDCAM_TASK_NAME L"\"", &code);
    }

    HRESULT WriteUninstallEntry(const std::wstring& dir, const std::wstring& setupPath)
    {
        HKEY key = nullptr;
        LSTATUS st = RegCreateKeyExW(HKEY_LOCAL_MACHINE, HNDCAM_UNINSTALL_KEY, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
        if (st != ERROR_SUCCESS)
            return HRESULT_FROM_WIN32(st);

        WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
        GetFileAttributesExW(DllPath().c_str(), GetFileExInfoStandard, &a);
        GetFileAttributesExW(setupPath.c_str(), GetFileExInfoStandard, &b);

        SetString(key, L"DisplayName", HNDCAM_PRODUCT_NAME);
        SetString(key, L"DisplayVersion", HNDCAM_VERSION_WSTR);
        SetString(key, L"Publisher", HNDCAM_PUBLISHER);
        SetString(key, L"InstallLocation", dir);
        SetString(key, L"DisplayIcon", setupPath);
        SetString(key, L"UninstallString", L"\"" + setupPath + L"\" /uninstall");
        SetString(key, L"QuietUninstallString", L"\"" + setupPath + L"\" /uninstall /quiet");
        SetDword(key, L"NoModify", 1);
        SetDword(key, L"NoRepair", 1);
        SetDword(key, L"EstimatedSize", (a.nFileSizeLow + b.nFileSizeLow) / 1024 + 1);
        RegCloseKey(key);
        return S_OK;
    }

    bool IsInstalled()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, HNDCAM_UNINSTALL_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
            return false;
        RegCloseKey(key);
        return true;
    }

    // --- actions ------------------------------------------------------------------------------------
    HRESULT Install()
    {
        const std::wstring dir = InstallDir();
        CreateDirectoryW(dir.c_str(), nullptr);
        PrepareLogDirectory();
        Log(L"Install %s from %s", HNDCAM_VERSION_WSTR, SelfPath().c_str());

        // an existing camera keeps the old DLL loaded: remove it first
        if (setup::IsVirtualCameraPresent())
        {
            setup::RemoveVirtualCamera();
            Sleep(1000);
        }

        HRESULT hr = ExtractDll(DllPath());
        if (FAILED(hr)) { LogHr(L"Extract media source", hr); return hr; }

        const std::wstring setupPath = InstalledSetupPath();
        hr = CopySelf(setupPath);
        if (FAILED(hr)) { LogHr(L"Copy setup", hr); return hr; }
        RemoveMarkOfTheWeb(DllPath());
        RemoveMarkOfTheWeb(setupPath);

        hr = RegisterCom(DllPath());
        if (FAILED(hr)) { LogHr(L"Register COM", hr); return hr; }
        WriteDefaultSettings();

        hr = setup::RegisterVirtualCamera();
        if (FAILED(hr)) return hr;

        CreateStartupTask(setupPath); // failure only affects the restore after reboot: logged
        WriteUninstallEntry(dir, setupPath);
        Log(L"Install done");
        return S_OK;
    }

    HRESULT Uninstall()
    {
        Log(L"Uninstall %s", HNDCAM_VERSION_WSTR);
        HRESULT hr = setup::RemoveVirtualCamera();
        if (FAILED(hr))
            LogHr(L"Remove virtual camera", hr);

        DeleteStartupTask();
        UnregisterCom();
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, SettingsKey, KEY_WOW64_64KEY, 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, HNDCAM_UNINSTALL_KEY, KEY_WOW64_64KEY, 0);

        const std::wstring dir = InstallDir();
        DeleteOrSchedule(DllPath());
        DeleteOrSchedule(InstalledSetupPath()); // scheduled when uninstalling from the installed copy
        if (!RemoveDirectoryW(dir.c_str()))
            MoveFileExW(dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        Log(L"Uninstall done");
        return hr;
    }

    HRESULT Restore()
    {
        if (setup::IsVirtualCameraPresent())
        {
            Log(L"Restore: virtual camera already present");
            return S_OK;
        }
        Log(L"Restore: re-creating the virtual camera");
        return setup::RegisterVirtualCamera();
    }

    void OpenCameraApp()
    {
        ShellExecuteW(nullptr, L"open", L"microsoft.windows.camera:", nullptr, nullptr, SW_SHOWNORMAL);
    }

    int ReportInstall(HRESULT hr)
    {
        if (FAILED(hr))
        {
            ShowMessage(L"The installation failed.", ErrorText(hr) + L"\n\nDetails: " + DataDir() + L"\\logs\\setup.log", TD_ERROR_ICON);
            return 1;
        }
        if (g_quiet)
            return 0;

        TASKDIALOG_BUTTON buttons[] = { { 100, L"Open the Camera app" }, { IDCLOSE, L"Close" } };
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
        config.pszWindowTitle = HNDCAM_PRODUCT_NAME;
        config.pszMainIcon = TD_INFORMATION_ICON;
        config.pszMainInstruction = L"HND Ear Camera is installed.";
        config.pszContent =
            L"To use it:\n"
            L"1. Turn the camera on.\n"
            L"2. Connect this PC's Wi-Fi to the camera network (HNDEC_...).\n"
            L"3. In the Camera app, Teams, Zoom or a web browser, choose \"HND Ear Camera\".\n\n"
            L"The camera accepts one client at a time: disconnect the phone app first.";
        config.pButtons = buttons;
        config.cButtons = ARRAYSIZE(buttons);
        config.nDefaultButton = 100;
        int clicked = 0;
        TaskDialogIndirect(&config, &clicked, nullptr, nullptr);
        if (clicked == 100)
            OpenCameraApp();
        return 0;
    }

    int ReportUninstall(HRESULT hr)
    {
        if (FAILED(hr))
        {
            ShowMessage(L"HND Ear Camera was removed, with warnings.", ErrorText(hr) + L"\n\nRestart the PC to complete the removal.", TD_WARNING_ICON);
            return 1;
        }
        ShowMessage(L"HND Ear Camera was removed.", L"", TD_INFORMATION_ICON);
        return 0;
    }

    int Interactive()
    {
        const bool installed = IsInstalled();
        TASKDIALOG_BUTTON buttons[2];
        UINT count = 0;
        buttons[count++] = { 100, installed ? L"Reinstall / update\nInstalls this version again" : L"Install\nAdds \"HND Ear Camera\" to the cameras of this PC" };
        if (installed)
            buttons[count++] = { 101, L"Uninstall\nRemoves the virtual camera from this PC" };

        TASKDIALOGCONFIG config{ sizeof(config) };
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        config.pszWindowTitle = HNDCAM_PRODUCT_NAME L" " HNDCAM_VERSION_WSTR;
        config.pszMainIcon = TD_SHIELD_ICON;
        config.pszMainInstruction = installed ? L"HND Ear Camera is installed on this PC." : L"Install HND Ear Camera?";
        config.pszContent =
            L"Makes the HND Wi-Fi ear camera (HNDEC_... network) usable as a webcam: "
            L"Windows Camera app, Teams, Zoom, web browsers...";
        config.pButtons = buttons;
        config.cButtons = count;
        int clicked = 0;
        if (FAILED(TaskDialogIndirect(&config, &clicked, nullptr, nullptr)))
            return 1;
        if (clicked == 100)
            return ReportInstall(Install());
        if (clicked == 101)
            return ReportUninstall(Uninstall());
        return 0;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    enum class Action { Interactive, Install, Uninstall, Restore } action = Action::Interactive;
    for (int i = 1; i < argc; i++)
    {
        if (_wcsicmp(argv[i], L"/install") == 0) action = Action::Install;
        else if (_wcsicmp(argv[i], L"/uninstall") == 0) action = Action::Uninstall;
        else if (_wcsicmp(argv[i], L"/restore") == 0) { action = Action::Restore; g_quiet = true; }
        else if (_wcsicmp(argv[i], L"/quiet") == 0) g_quiet = true;
    }
    LocalFree(argv);

    hndcam::LogInit(L"setup.log");
    if (!IsWindows11())
    {
        ShowMessage(L"Windows 11 is required.", L"The virtual camera uses a Windows 11 feature (MFCreateVirtualCamera) that Windows 10 does not have.", TD_ERROR_ICON);
        return 2;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 3;
    HRESULT hrMf = MFStartup(MF_VERSION);
    if (FAILED(hrMf))
    {
        ShowMessage(L"Media Foundation is not available.", ErrorText(hrMf), TD_ERROR_ICON);
        CoUninitialize();
        return 3;
    }

    int code = 0;
    switch (action)
    {
    case Action::Install: code = ReportInstall(Install()); break;
    case Action::Uninstall:
        if (!g_quiet && TaskDialog(nullptr, nullptr, HNDCAM_PRODUCT_NAME, L"Remove HND Ear Camera from this PC?", nullptr,
            TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, TD_WARNING_ICON, &code) == S_OK && code != IDYES)
        {
            code = 0;
            break;
        }
        code = ReportUninstall(Uninstall());
        break;
    case Action::Restore: code = SUCCEEDED(Restore()) ? 0 : 1; break;
    default: code = Interactive(); break;
    }

    MFShutdown();
    CoUninitialize();
    return code;
}
