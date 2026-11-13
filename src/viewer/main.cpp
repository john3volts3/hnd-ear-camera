// CAM-Oreille (HndViewer target): standalone browser viewer for the HND Ear Camera (no admin rights, no Python).
// Double-click it: the default browser opens http://127.0.0.1:<port>/ with the live picture.
// Closing the browser page stops the program. Same web page as tools/viewer/hnd_viewer.py.
// First launch of a downloaded copy: offers to install it in <system drive>\CAM-OREILLE (Installer.cpp).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <objbase.h>
#include <cstdio>
#include <string>
#include <vector>
#include "CameraClient.h"
#include "HndCam.h"
#include "Installer.h"
#include "Lang.h"
#include "Log.h"
#include "WifiAdapters.h"
#include "resource.h"

using hndcam::Tr;

namespace
{
    constexpr u_short FirstPort = 8765;
    constexpr int PortAttempts = 10;
    constexpr ULONGLONG PageClosedGraceMs = 5000;  // a page reload reconnects well before
    constexpr ULONGLONG NoPageTimeoutMs = 120000;  // the browser never opened the page: give up
    constexpr wchar_t AppTitle[] = HNDCAM_VIEWER_NAME L" " HNDCAM_VERSION_WSTR;

    hndcam::CameraClient* g_camera = nullptr;
    std::string g_page;
    HANDLE g_stop = nullptr;

    // --- browser page presence ------------------------------------------------------------------
    SRWLOCK g_presenceLock = SRWLOCK_INIT;
    int g_streams = 0;
    ULONGLONG g_lastSeen = 0;
    bool g_seenOnce = false;

    void Touch()
    {
        AcquireSRWLockExclusive(&g_presenceLock);
        g_lastSeen = GetTickCount64();
        g_seenOnce = true;
        ReleaseSRWLockExclusive(&g_presenceLock);
    }

    void StreamOpened()
    {
        AcquireSRWLockExclusive(&g_presenceLock);
        g_streams++;
        g_lastSeen = GetTickCount64();
        g_seenOnce = true;
        const int active = g_streams;
        ReleaseSRWLockExclusive(&g_presenceLock);
        hndcam::Log(L"HTTP: video stream opened (%d active)", active);
    }

    void StreamClosed()
    {
        AcquireSRWLockExclusive(&g_presenceLock);
        g_streams--;
        g_lastSeen = GetTickCount64();
        const int active = g_streams;
        ReleaseSRWLockExclusive(&g_presenceLock);
        hndcam::Log(L"HTTP: video stream closed (%d active)", active);
    }

    // milliseconds since the last page left; 0 while a page is open
    ULONGLONG PageIdleMs(bool* seenOnce)
    {
        AcquireSRWLockShared(&g_presenceLock);
        *seenOnce = g_seenOnce;
        ULONGLONG idle = (g_streams > 0 || !g_seenOnce) ? 0 : GetTickCount64() - g_lastSeen;
        ReleaseSRWLockShared(&g_presenceLock);
        return idle;
    }

    // --- HTTP helpers ---------------------------------------------------------------------------
    bool SendAll(SOCKET s, const void* data, size_t length)
    {
        const char* p = static_cast<const char*>(data);
        while (length > 0)
        {
            int n = send(s, p, (int)std::min<size_t>(length, 64 * 1024), 0);
            if (n <= 0)
                return false;
            p += n;
            length -= n;
        }
        return true;
    }

    bool SendResponse(SOCKET s, int code, const char* reason, const char* contentType, const std::string& body, const char* extraHeaders = "")
    {
        char header[512];
        int n = sprintf_s(header, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n%s\r\n",
            code, reason, contentType, body.size(), extraHeaders);
        return n > 0 && SendAll(s, header, n) && SendAll(s, body.data(), body.size());
    }

    // true when the browser closed the connection (readable with no data)
    bool ClientGone(SOCKET s)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval zero{};
        if (select(0, &set, nullptr, nullptr, &zero) <= 0)
            return false;
        char c;
        return recv(s, &c, 1, MSG_PEEK) <= 0;
    }

    std::string Utf8(const std::wstring& text)
    {
        if (text.empty())
            return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), n, nullptr, nullptr);
        return out;
    }

    std::string JsonString(const std::string& text)
    {
        std::string out = "\"";
        for (char c : text)
        {
            if (c == '"' || c == '\\')
                out += '\\';
            if ((unsigned char)c >= 0x20)
                out += c;
        }
        return out + "\"";
    }

    // --- request handlers -----------------------------------------------------------------------
    std::string StatusJson()
    {
        static SRWLOCK lock = SRWLOCK_INIT;
        static uint64_t fpsFrames = 0;
        static ULONGLONG fpsTick = 0;
        static double fps = 0;

        const auto st = g_camera->GetStatus();
        AcquireSRWLockExclusive(&lock);
        const ULONGLONG now = GetTickCount64();
        if (fpsTick == 0 || st.frames < fpsFrames)
        {
            fpsTick = now;
            fpsFrames = st.frames;
        }
        else if (now - fpsTick >= 1000)
        {
            fps = (st.frames - fpsFrames) * 1000.0 / (double)(now - fpsTick);
            fpsTick = now;
            fpsFrames = st.frames;
        }
        const double currentFps = st.state == hndcam::CameraState::Streaming ? fps : 0.0;
        ReleaseSRWLockExclusive(&lock);

        // "state" is the key read by the page (English, as in the Python viewer), "text" what it shows
        const char* state = "Starting";
        const char* text = Tr("Starting", "Démarrage");
        switch (st.state)
        {
        case hndcam::CameraState::NoWifi: state = "No Wi-Fi connection"; text = Tr(state, "Pas de connexion Wi-Fi"); break;
        case hndcam::CameraState::Searching: state = "Searching for the camera"; text = Tr(state, "Recherche de la caméra"); break;
        case hndcam::CameraState::Connecting: state = "Starting video"; text = Tr(state, "Démarrage de la vidéo"); break;
        case hndcam::CameraState::JoiningWifi: state = "Connecting to the camera Wi-Fi"; text = Tr(state, "Connexion au Wi-Fi de la caméra"); break;
        case hndcam::CameraState::Streaming: state = "Streaming"; text = Tr(state, "Image en direct"); break;
        case hndcam::CameraState::WifiOff: state = "Wi-Fi is turned off"; text = Tr(state, "Le Wi-Fi est éteint"); break;
        default: break;
        }

        char numbers[160];
        if (st.haveAngle)
            sprintf_s(numbers, "\"fps\":%.1f,\"angle\":%.1f,\"frames\":%llu", currentFps, st.angle, st.frames);
        else
            sprintf_s(numbers, "\"fps\":%.1f,\"angle\":null,\"frames\":%llu", currentFps, st.frames);
        return std::string("{\"state\":") + JsonString(state) + ",\"camera\":" + JsonString(Utf8(st.cameraName)) + "," + numbers
            + ",\"wifiOff\":" + (st.state == hndcam::CameraState::WifiOff ? "true" : "false")
            + ",\"text\":" + JsonString(text) + ",\"lang\":\"" + (hndcam::UiFrench() ? "fr" : "en") + "\""
            + ",\"version\":\"" HNDCAM_VERSION_STR "\"}";
    }

    void ServeStream(SOCKET s)
    {
        const char header[] = "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
        if (!SendAll(s, header, sizeof(header) - 1))
            return;

        StreamOpened();
        uint64_t seq = 0;
        std::vector<uint8_t> jpeg;
        while (WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0 && !ClientGone(s))
        {
            const uint64_t next = g_camera->WaitJpeg(seq, jpeg, 1000);
            if (next == seq)
                continue;
            seq = next;
            char part[96];
            int n = sprintf_s(part, "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", jpeg.size());
            if (!SendAll(s, part, n) || !SendAll(s, jpeg.data(), jpeg.size()) || !SendAll(s, "\r\n", 2))
                break;
        }
        StreamClosed();
    }

    DWORD WINAPI ConnectionThread(LPVOID param)
    {
        SOCKET s = (SOCKET)param;
        DWORD timeout = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        // read the request head
        std::string request;
        char buf[2048];
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16 * 1024)
        {
            int n = recv(s, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            request.append(buf, n);
        }

        const size_t sp1 = request.find(' ');
        const size_t sp2 = sp1 == std::string::npos ? sp1 : request.find(' ', sp1 + 1);
        if (sp2 != std::string::npos && request.compare(0, sp1, "GET") == 0)
        {
            std::string path = request.substr(sp1 + 1, sp2 - sp1 - 1);
            path = path.substr(0, path.find('?'));

            if (path == "/")
            {
                SendResponse(s, 200, "OK", "text/html; charset=utf-8", g_page);
            }
            else if (path == "/status.json")
            {
                Touch();
                SendResponse(s, 200, "OK", "application/json", StatusJson());
            }
            else if (path == "/snapshot.jpg")
            {
                std::vector<uint8_t> jpeg;
                if (g_camera->WaitJpeg(0, jpeg, 0) == 0)
                {
                    SendResponse(s, 503, "Service Unavailable", "text/plain", "No picture yet");
                }
                else
                {
                    SYSTEMTIME t;
                    GetLocalTime(&t);
                    char disposition[128];
                    sprintf_s(disposition, "Content-Disposition: attachment; filename=\"hnd_%04u%02u%02u_%02u%02u%02u.jpg\"\r\n",
                        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
                    SendResponse(s, 200, "OK", "image/jpeg", std::string(jpeg.begin(), jpeg.end()), disposition);
                }
            }
            else if (path == "/stream.mjpg")
            {
                ServeStream(s);
            }
            else
            {
                SendResponse(s, 404, "Not Found", "text/plain", "Not found");
            }
        }
        else if (!request.empty())
        {
            SendResponse(s, 405, "Method Not Allowed", "text/plain", "Method not allowed");
        }

        shutdown(s, SD_SEND);
        closesocket(s);
        return 0;
    }

    DWORD WINAPI AcceptThread(LPVOID param)
    {
        SOCKET listener = (SOCKET)param;
        for (;;)
        {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                break; // listener closed on exit
            HANDLE thread = CreateThread(nullptr, 0, ConnectionThread, (LPVOID)client, 0, nullptr);
            if (thread)
                CloseHandle(thread);
            else
                closesocket(client);
        }
        return 0;
    }

    bool LoadPage()
    {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_VIEWER_PAGE), RT_RCDATA);
        HGLOBAL data = res ? LoadResource(nullptr, res) : nullptr;
        const char* bytes = data ? static_cast<const char*>(LockResource(data)) : nullptr;
        if (!bytes)
            return false;
        g_page.assign(bytes, SizeofResource(nullptr, res));
        return true;
    }

    void InitLog()
    {
        // diagnostic log: %TEMP%\CAM-Oreille.log
        wchar_t logPath[MAX_PATH] = {};
        DWORD tempLength = GetTempPathW(ARRAYSIZE(logPath), logPath);
        if (tempLength > 0 && tempLength < MAX_PATH - 16)
        {
            wcscat_s(logPath, HNDCAM_VIEWER_NAME L".log");
            hndcam::LogInit(logPath);
        }
    }

    // --- Wi-Fi turned off: the user picks an adapter to turn on (administrator rights) ----------------
    constexpr int TurnOnButton = 100;
    constexpr int NotNowButton = 101;
    constexpr int FirstAdapterRadio = 200;

    struct WifiDialog
    {
        std::vector<hndcam::WifiAdapter> adapters;
        int selected = 0;
        bool restore = true; // turn the adapter off again when the viewer closes
    };

    // "192.168.0.x, Ethernet": the network this PC already uses (it has a default gateway), shown next to the camera range
    std::wstring CurrentNetworkText()
    {
        ULONG size = 16 * 1024;
        std::vector<uint8_t> buffer(size);
        const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        ULONG err = GetAdaptersAddresses(AF_INET, flags, nullptr, (PIP_ADAPTER_ADDRESSES)buffer.data(), &size);
        if (err == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(size);
            err = GetAdaptersAddresses(AF_INET, flags, nullptr, (PIP_ADAPTER_ADDRESSES)buffer.data(), &size);
        }
        if (err != NO_ERROR)
            return {};

        for (auto a = (PIP_ADAPTER_ADDRESSES)buffer.data(); a; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp || !a->FirstGatewayAddress)
                continue;
            for (auto u = a->FirstUnicastAddress; u; u = u->Next)
            {
                if (u->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                const auto* ip = reinterpret_cast<const uint8_t*>(&reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr);
                wchar_t text[160];
                if (u->OnLinkPrefixLength == 24)
                    swprintf_s(text, L"%u.%u.%u.x, %s", ip[0], ip[1], ip[2], a->FriendlyName);
                else
                    swprintf_s(text, L"%u.%u.%u.%u, %s", ip[0], ip[1], ip[2], ip[3], a->FriendlyName);
                return text;
            }
        }
        return {};
    }

    // Starts "CAM-Oreille.exe /wifi-on ..." with administrator rights: Windows shows its permission prompt.
    bool StartWifiHelper(HWND owner, const hndcam::WifiAdapter& adapter, bool restore)
    {
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, ARRAYSIZE(self));
        wchar_t params[512];
        swprintf_s(params, L"/wifi-on %lu %d \"%s\"", GetCurrentProcessId(), restore ? 1 : 0, adapter.instanceId.c_str());

        SHELLEXECUTEINFOW sei{ sizeof(sei) };
        sei.fMask = SEE_MASK_NOASYNC;
        sei.hwnd = owner;
        sei.lpVerb = L"runas";
        sei.lpFile = self;
        sei.lpParameters = params;
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei))
        {
            hndcam::Log(L"Wi-Fi: helper started for %s (turn off on exit: %d)", adapter.name.c_str(), restore);
            return true;
        }
        const DWORD err = GetLastError();
        hndcam::Log(err == ERROR_CANCELLED ? L"Wi-Fi: permission refused (%lu)" : L"Wi-Fi: helper not started (%lu)", err);
        return false;
    }

    HRESULT CALLBACK WifiDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM, LONG_PTR refData)
    {
        auto* dialog = reinterpret_cast<WifiDialog*>(refData);
        switch (msg)
        {
        case TDN_CREATED:
            SendMessageW(hwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, TurnOnButton, TRUE);
            SetForegroundWindow(hwnd);
            break;
        case TDN_RADIO_BUTTON_CLICKED:
            dialog->selected = (int)wParam - FirstAdapterRadio;
            break;
        case TDN_VERIFICATION_CLICKED:
            dialog->restore = wParam != 0;
            break;
        case TDN_BUTTON_CLICKED:
            // permission refused: the dialog stays open (try again, or "Not now")
            if ((int)wParam == TurnOnButton && !StartWifiHelper(hwnd, dialog->adapters[dialog->selected], dialog->restore))
                return S_FALSE;
            break;
        }
        return S_OK;
    }

    // Shown before the browser opens, while this program still has the foreground: the Windows
    // permission prompt then appears in front instead of blinking in the taskbar.
    void OfferToTurnWifiOn()
    {
        if (hndcam::WifiConnector::IsWifiOn())
            return;

        WifiDialog dialog;
        for (const auto& a : hndcam::ListWifiAdapters())
        {
            if (!a.hardwareOff)
                dialog.adapters.push_back(a);
        }
        if (dialog.adapters.empty())
        {
            hndcam::Log(L"Wi-Fi: off, and no adapter can be turned on by software");
            return; // the page shows the reminder
        }

        std::vector<std::wstring> labels;
        for (const auto& a : dialog.adapters)
            labels.push_back(a.name + (a.enabled ? Tr(L"   (Wi-Fi turned off)", L"   (Wi-Fi coupé)") : Tr(L"   (disabled)", L"   (désactivée)")));
        std::vector<TASKDIALOG_BUTTON> radios;
        for (size_t i = 0; i < labels.size(); i++)
            radios.push_back({ FirstAdapterRadio + (int)i, labels[i].c_str() });

        const std::wstring network = CurrentNetworkText();
        const std::wstring content =
            Tr(L"The camera creates its own Wi-Fi network (HNDEC_...) with its own address range, 192.168.1.x",
               L"La caméra crée son propre réseau Wi-Fi (HNDEC_...) avec sa propre plage d'adresses, 192.168.1.x")
            + (network.empty() ? std::wstring()
                : Tr(L", different from the network this PC uses now (", L", différente de celle du réseau utilisé en ce moment par ce PC (") + network + L")")
            + Tr(L". To join it, this PC needs a Wi-Fi adapter turned on, and all of them are off.\n\n"
                 L"Choose the adapter to turn on. Windows will ask for your permission: turning an adapter on needs administrator rights.",
                 L". Pour le rejoindre, ce PC a besoin d'une carte Wi-Fi allumée, et elles sont toutes éteintes.\n\n"
                 L"Choisissez la carte à allumer. Windows va demander votre autorisation : allumer une carte demande les droits administrateur.");

        TASKDIALOG_BUTTON buttons[] = { { TurnOnButton, Tr(L"Turn on Wi-Fi", L"Allumer le Wi-Fi") }, { NotNowButton, Tr(L"Not now", L"Pas maintenant") } };
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_VERIFICATION_FLAG_CHECKED;
        config.pszWindowTitle = AppTitle;
        config.pszMainIcon = TD_INFORMATION_ICON;
        config.pszMainInstruction = Tr(L"Turn on Wi-Fi to connect to the camera", L"Allumer le Wi-Fi pour se connecter à la caméra");
        config.pszContent = content.c_str();
        config.pButtons = buttons;
        config.cButtons = ARRAYSIZE(buttons);
        config.nDefaultButton = TurnOnButton;
        config.pRadioButtons = radios.data();
        config.cRadioButtons = (UINT)radios.size();
        config.nDefaultRadioButton = FirstAdapterRadio;
        config.pszVerificationText = Tr(L"Turn the Wi-Fi adapter off again when CAM-Oreille closes", L"Éteindre de nouveau la carte Wi-Fi à la fermeture de CAM-Oreille");
        config.pfCallback = WifiDialogProc;
        config.lpCallbackData = (LONG_PTR)&dialog;
        int clicked = 0;
        const HRESULT hr = TaskDialogIndirect(&config, &clicked, nullptr, nullptr);
        if (FAILED(hr))
            hndcam::LogHr(L"TaskDialogIndirect", hr);
        else if (clicked != TurnOnButton)
            hndcam::Log(L"Wi-Fi: left off by the user");
    }

    SOCKET Listen(u_short& port)
    {
        for (int i = 0; i < PortAttempts; i++)
        {
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET)
                return s;
            BOOL exclusive = TRUE;
            setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons((u_short)(FirstPort + i));
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // this PC only
            if (bind(s, (sockaddr*)&addr, sizeof(addr)) == 0 && listen(s, SOMAXCONN) == 0)
            {
                port = (u_short)(FirstPort + i);
                return s;
            }
            closesocket(s);
        }
        return INVALID_SOCKET;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // administrator helper started by the viewer itself to turn a Wi-Fi adapter on (see StartWifiHelper)
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc == 5 && _wcsicmp(argv[1], L"/wifi-on") == 0)
    {
        InitLog();
        const int code = hndcam::RunWifiOnHelper(wcstoul(argv[2], nullptr, 10), wcscmp(argv[3], L"1") == 0, argv[4]);
        LocalFree(argv);
        return code;
    }
    // "Uninstall" in Settings > Apps
    if (argv && argc == 2 && _wcsicmp(argv[1], L"/uninstall") == 0)
    {
        InitLog();
        LocalFree(argv);
        return hndcam::Uninstall();
    }
    if (argv)
        LocalFree(argv);

    // one viewer at a time: the camera accepts a single client
    HANDLE instance = CreateMutexW(nullptr, TRUE, HNDCAM_VIEWER_MUTEX);
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, Tr(L"CAM-Oreille is already open.\nLook for its page in your web browser.",
            L"CAM-Oreille est déjà ouvert.\nCherchez sa page dans votre navigateur."), AppTitle, MB_ICONINFORMATION);
        return 0;
    }

    InitLog();

    if (!LoadPage())
    {
        MessageBoxW(nullptr, Tr(L"The viewer page is missing from the program resources.",
            L"La page du visualiseur manque dans les ressources du programme."), AppTitle, MB_ICONERROR);
        return 1;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        MessageBoxW(nullptr, Tr(L"Windows networking could not be initialized.", L"Le réseau de Windows n'a pas pu être initialisé."), AppTitle, MB_ICONERROR);
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // downloaded copy (WhatsApp, e-mail link...): offer to install it, then let the installed copy take over
    std::wstring installedExe;
    if (hndcam::InstallIfWanted(installedExe))
    {
        ReleaseMutex(instance);
        CloseHandle(instance);
        AllowSetForegroundWindow(ASFW_ANY); // its Wi-Fi window may come to the front
        ShellExecuteW(nullptr, L"open", installedExe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        CoUninitialize();
        WSACleanup();
        return 0;
    }

    u_short port = 0;
    SOCKET listener = Listen(port);
    if (listener == INVALID_SOCKET)
    {
        MessageBoxW(nullptr, Tr(L"No free local port was found for the viewer page (8765-8774).",
            L"Aucun port local libre n'a été trouvé pour la page (8765-8774)."), AppTitle, MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    OfferToTurnWifiOn();

    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_camera = new hndcam::CameraClient(HNDCAM_OUT_WIDTH, HNDCAM_OUT_HEIGHT);
    g_camera->SetRenderOutput(false); // the browser decodes and rotates the picture
    g_camera->Start();

    HANDLE acceptThread = CreateThread(nullptr, 0, AcceptThread, (LPVOID)listener, 0, nullptr);

    wchar_t url[64];
    swprintf_s(url, L"http://127.0.0.1:%u/", port);
    hndcam::Log(L"CAM-Oreille %s serving %s", HNDCAM_VERSION_WSTR, url);
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);

    const ULONGLONG started = GetTickCount64();
    for (;;)
    {
        Sleep(250);
        bool seenOnce = false;
        const ULONGLONG idle = PageIdleMs(&seenOnce);
        if (seenOnce && idle > PageClosedGraceMs)
            break; // page closed
        if (!seenOnce && GetTickCount64() - started > NoPageTimeoutMs)
        {
            wchar_t text[256];
            swprintf_s(text, Tr(L"The viewer page was not opened.\nOpen %s in your web browser, then start CAM-Oreille again.",
                L"La page n'a pas été ouverte.\nOuvrez %s dans votre navigateur, puis relancez CAM-Oreille."), url);
            MessageBoxW(nullptr, text, AppTitle, MB_ICONWARNING);
            break;
        }
    }

    SetEvent(g_stop);
    closesocket(listener);
    if (acceptThread)
    {
        WaitForSingleObject(acceptThread, 2000);
        CloseHandle(acceptThread);
    }
    g_camera->Stop(); // sends the video-off command to the camera
    WSACleanup();
    CoUninitialize();
    if (instance)
        CloseHandle(instance);
    return 0; // remaining connection threads end with the process
}
