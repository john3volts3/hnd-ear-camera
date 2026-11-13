#include "Installer.h"
#include "HndCam.h"
#include "Lang.h"
#include "Log.h"
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace hndcam
{
    namespace
    {
        constexpr int InstallButton = 100;
        constexpr int JustStartButton = 101;
        constexpr wchar_t WindowTitle[] = HNDCAM_VIEWER_NAME L" " HNDCAM_VERSION_WSTR;
        constexpr ULONGLONG ThisVersion = ((ULONGLONG)HNDCAM_VERSION_MAJOR << 48) | ((ULONGLONG)HNDCAM_VERSION_MINOR << 32)
            | ((ULONGLONG)HNDCAM_VERSION_PATCH << 16);

        std::wstring SelfPath()
        {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
            return path;
        }

        std::wstring InstallDir()
        {
            wchar_t drive[16] = {};
            const DWORD n = GetEnvironmentVariableW(L"SystemDrive", drive, ARRAYSIZE(drive));
            if (n == 0 || n >= ARRAYSIZE(drive))
                wcscpy_s(drive, L"C:");
            return std::wstring(drive) + L"\\" HNDCAM_VIEWER_DIR_NAME;
        }

        std::wstring DesktopLink()
        {
            PWSTR desktop = nullptr;
            std::wstring link;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)))
                link = std::wstring(desktop) + L"\\" HNDCAM_VIEWER_NAME L".lnk";
            CoTaskMemFree(desktop);
            return link;
        }

        // File version as major.minor.patch.build in 16-bit fields, 0 when the file is missing
        ULONGLONG FileVersion(const std::wstring& path)
        {
            DWORD handle = 0;
            const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
            if (size == 0)
                return 0;
            std::vector<BYTE> data(size);
            VS_FIXEDFILEINFO* info = nullptr;
            UINT length = 0;
            if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())
                || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
                return 0;
            return ((ULONGLONG)info->dwFileVersionMS << 32) | info->dwFileVersionLS;
        }

        std::wstring VersionText(ULONGLONG version)
        {
            wchar_t text[32];
            swprintf_s(text, L"%u.%u.%u", (unsigned)(version >> 48), (unsigned)((version >> 32) & 0xFFFF), (unsigned)((version >> 16) & 0xFFFF));
            return text;
        }

        HRESULT CreateDesktopShortcut(const std::wstring& target, const std::wstring& dir)
        {
            const std::wstring link = DesktopLink();
            if (link.empty())
                return E_FAIL;
            ComPtr<IShellLinkW> shell;
            HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&shell));
            if (FAILED(hr))
                return hr;
            shell->SetPath(target.c_str());
            shell->SetWorkingDirectory(dir.c_str());
            shell->SetDescription(Tr(L"Live picture of the HND ear camera", L"Image en direct de la caméra d'oreille HND"));
            shell->SetIconLocation(target.c_str(), 0);
            ComPtr<IPersistFile> file;
            hr = shell.As(&file);
            if (SUCCEEDED(hr))
                hr = file->Save(link.c_str(), TRUE);
            return hr;
        }

        void SetString(HKEY key, const wchar_t* name, const std::wstring& value)
        {
            RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
        }

        void SetDword(HKEY key, const wchar_t* name, DWORD value)
        {
            RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        }

        // Entry in Settings > Apps, for the current user: no administrator rights
        void RegisterUninstall(const std::wstring& target, const std::wstring& dir)
        {
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, HNDCAM_VIEWER_UNINSTALL_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
                return;
            SetString(key, L"DisplayName", HNDCAM_VIEWER_NAME);
            SetString(key, L"DisplayVersion", HNDCAM_VERSION_WSTR);
            SetString(key, L"Publisher", HNDCAM_PUBLISHER);
            SetString(key, L"DisplayIcon", target);
            SetString(key, L"InstallLocation", dir);
            SetString(key, L"UninstallString", L"\"" + target + L"\" /uninstall");
            SetDword(key, L"NoModify", 1);
            SetDword(key, L"NoRepair", 1);
            WIN32_FILE_ATTRIBUTE_DATA attributes{};
            if (GetFileAttributesExW(target.c_str(), GetFileExInfoStandard, &attributes))
                SetDword(key, L"EstimatedSize", attributes.nFileSizeLow / 1024 + 1); // KB
            RegCloseKey(key);
        }

        bool Install(const std::wstring& self, const std::wstring& dir, const std::wstring& target)
        {
            if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            {
                LogHr(L"Install: create folder", HRESULT_FROM_WIN32(GetLastError()));
                return false;
            }
            if (!CopyFileW(self.c_str(), target.c_str(), FALSE))
            {
                LogHr(L"Install: copy program", HRESULT_FROM_WIN32(GetLastError()));
                return false;
            }
            // The downloaded file is marked as coming from the internet and Windows already warned the
            // user about it once: the installed copy is not marked, the desktop icon starts without a new warning.
            DeleteFileW((target + L":Zone.Identifier").c_str());
            const HRESULT hr = CreateDesktopShortcut(target, dir);
            if (FAILED(hr))
                LogHr(L"Install: desktop icon", hr);
            RegisterUninstall(target, dir);
            Log(L"Install: version %s installed in %s", HNDCAM_VERSION_WSTR, dir.c_str());
            return true;
        }
    }

    bool InstallIfWanted(std::wstring& installedExe)
    {
        const std::wstring self = SelfPath();
        const std::wstring dir = InstallDir();
        const std::wstring target = dir + L"\\" HNDCAM_VIEWER_EXE_NAME;
        if (_wcsicmp(self.c_str(), target.c_str()) == 0)
            return false; // this is the installed copy

        const ULONGLONG installed = FileVersion(target);
        if (installed >= ThisVersion)
            return false; // same or newer version installed: this copy simply runs

        wchar_t content[640];
        const wchar_t* instruction = nullptr;
        TASKDIALOG_BUTTON buttons[2];
        if (installed == 0)
        {
            instruction = Tr(L"Install CAM-Oreille on this PC?", L"Installer CAM-Oreille sur ce PC ?");
            swprintf_s(content, Tr(
                L"CAM-Oreille is copied to %s and an icon is added to the desktop. No administrator rights are needed.\n\n"
                L"To remove it later: Settings > Apps > Installed apps > CAM-Oreille.",
                L"CAM-Oreille est copié dans %s et une icône est ajoutée sur le bureau. Aucun droit administrateur n'est nécessaire.\n\n"
                L"Pour le retirer plus tard : Paramètres > Applications > Applications installées > CAM-Oreille."), dir.c_str());
            buttons[0] = { InstallButton, Tr(L"Install and start", L"Installer et démarrer") };
            buttons[1] = { JustStartButton, Tr(L"Start without installing", L"Démarrer sans installer") };
        }
        else
        {
            instruction = Tr(L"Update CAM-Oreille on this PC?", L"Mettre à jour CAM-Oreille sur ce PC ?");
            swprintf_s(content, Tr(L"Installed version: %s\nThis version: %s", L"Version installée : %s\nCette version : %s"),
                VersionText(installed).c_str(), HNDCAM_VERSION_WSTR);
            buttons[0] = { InstallButton, Tr(L"Update and start", L"Mettre à jour et démarrer") };
            buttons[1] = { JustStartButton, Tr(L"Start this copy without updating", L"Démarrer cette copie sans mettre à jour") };
        }

        TASKDIALOGCONFIG config{ sizeof(config) };
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
        config.pszWindowTitle = WindowTitle;
        config.pszMainIcon = TD_INFORMATION_ICON;
        config.pszMainInstruction = instruction;
        config.pszContent = content;
        config.pButtons = buttons;
        config.cButtons = ARRAYSIZE(buttons);
        config.nDefaultButton = InstallButton;
        int clicked = 0;
        const HRESULT hr = TaskDialogIndirect(&config, &clicked, nullptr, nullptr);
        Log(L"Install: %s offered (installed version: %s), answer: %s", installed ? L"update" : L"installation",
            installed ? VersionText(installed).c_str() : L"none", clicked == InstallButton ? L"yes" : L"no");
        if (FAILED(hr) || clicked != InstallButton)
            return false;

        if (!Install(self, dir, target))
        {
            TaskDialog(nullptr, nullptr, WindowTitle, Tr(L"CAM-Oreille could not be installed.", L"CAM-Oreille n'a pas pu être installé."),
                Tr(L"This copy starts anyway. If CAM-Oreille is already open, close it and try again.",
                   L"Cette copie démarre quand même. Si CAM-Oreille est déjà ouvert, fermez-le et réessayez."),
                TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
            return false;
        }
        installedExe = target;
        return true;
    }

    int Uninstall()
    {
        // the program file cannot be deleted while the viewer runs
        HANDLE running = OpenMutexW(SYNCHRONIZE, FALSE, HNDCAM_VIEWER_MUTEX);
        if (running)
        {
            CloseHandle(running);
            TaskDialog(nullptr, nullptr, WindowTitle, Tr(L"CAM-Oreille is open.", L"CAM-Oreille est ouvert."),
                Tr(L"Close its page in the web browser, wait a few seconds, then try again.",
                   L"Fermez sa page dans le navigateur, attendez quelques secondes, puis réessayez."),
                TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
            return 1;
        }

        const std::wstring dir = InstallDir();
        wchar_t content[256];
        swprintf_s(content, Tr(L"The desktop icon and the %s folder are deleted.", L"L'icône du bureau et le dossier %s sont supprimés."), dir.c_str());
        int button = 0;
        if (TaskDialog(nullptr, nullptr, WindowTitle, Tr(L"Remove CAM-Oreille from this PC?", L"Retirer CAM-Oreille de ce PC ?"),
            content, TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, TD_WARNING_ICON, &button) != S_OK || button != IDYES)
            return 0;

        const std::wstring link = DesktopLink();
        if (!link.empty())
            DeleteFileW(link.c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, HNDCAM_VIEWER_UNINSTALL_KEY);
        Log(L"Uninstall: desktop icon and Apps entry removed");
        TaskDialog(nullptr, nullptr, WindowTitle, Tr(L"CAM-Oreille was removed from this PC.", L"CAM-Oreille a été retiré de ce PC."),
            nullptr, TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);

        // The running program cannot delete itself: a hidden command deletes it once this process has
        // exited, then removes the folder only if it is empty (nothing else is ever deleted).
        wchar_t system[MAX_PATH] = {};
        GetSystemDirectoryW(system, ARRAYSIZE(system));
        std::wstring command = std::wstring(L"\"") + system + L"\\cmd.exe\" /c ping -n 3 127.0.0.1 >nul & del /f /q \""
            + dir + L"\\" HNDCAM_VIEWER_EXE_NAME L"\" & rmdir \"" + dir + L"\"";
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        return 0;
    }
}
