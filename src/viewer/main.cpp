// HndViewer: standalone browser viewer for the HND Ear Camera (no installation, no admin rights, no Python).
// Double-click it: the default browser opens http://127.0.0.1:<port>/ with the live picture.
// Closing the browser page stops the program. Same web page as tools/viewer/hnd_viewer.py.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <cstdio>
#include <string>
#include <vector>
#include "CameraClient.h"
#include "HndCam.h"
#include "Log.h"
#include "resource.h"

namespace
{
    constexpr u_short FirstPort = 8765;
    constexpr int PortAttempts = 10;
    constexpr ULONGLONG PageClosedGraceMs = 5000;  // a page reload reconnects well before
    constexpr ULONGLONG NoPageTimeoutMs = 120000;  // the browser never opened the page: give up
    constexpr wchar_t AppTitle[] = L"HND Ear Camera viewer";

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

        const char* state = "Starting";
        switch (st.state)
        {
        case hndcam::CameraState::NoWifi: state = "No Wi-Fi connection"; break;
        case hndcam::CameraState::Searching: state = "Searching for the camera"; break;
        case hndcam::CameraState::Connecting: state = "Starting video"; break;
        case hndcam::CameraState::JoiningWifi: state = "Connecting to the camera Wi-Fi"; break;
        case hndcam::CameraState::Streaming: state = "Streaming"; break;
        default: break;
        }

        char numbers[160];
        if (st.haveAngle)
            sprintf_s(numbers, "\"fps\":%.1f,\"angle\":%.1f,\"frames\":%llu", currentFps, st.angle, st.frames);
        else
            sprintf_s(numbers, "\"fps\":%.1f,\"angle\":null,\"frames\":%llu", currentFps, st.frames);
        return std::string("{\"state\":") + JsonString(state) + ",\"camera\":" + JsonString(Utf8(st.cameraName)) + "," + numbers + "}";
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
    // one viewer at a time: the camera accepts a single client
    HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\HndEarCameraViewer");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"The HND Ear Camera viewer is already running.\nLook for its page in your web browser.", AppTitle, MB_ICONINFORMATION);
        return 0;
    }

    // diagnostic log: %TEMP%\HndViewer.log
    wchar_t logPath[MAX_PATH] = {};
    DWORD tempLength = GetTempPathW(ARRAYSIZE(logPath), logPath);
    if (tempLength > 0 && tempLength < MAX_PATH - 16)
    {
        wcscat_s(logPath, L"HndViewer.log");
        hndcam::LogInit(logPath);
    }

    if (!LoadPage())
    {
        MessageBoxW(nullptr, L"The viewer page is missing from the program resources.", AppTitle, MB_ICONERROR);
        return 1;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        MessageBoxW(nullptr, L"Windows networking could not be initialized.", AppTitle, MB_ICONERROR);
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    u_short port = 0;
    SOCKET listener = Listen(port);
    if (listener == INVALID_SOCKET)
    {
        MessageBoxW(nullptr, L"No free local port was found for the viewer page (8765-8774).", AppTitle, MB_ICONERROR);
        WSACleanup();
        return 1;
    }

    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_camera = new hndcam::CameraClient(HNDCAM_OUT_WIDTH, HNDCAM_OUT_HEIGHT);
    g_camera->SetRenderOutput(false); // the browser decodes and rotates the picture
    g_camera->Start();

    HANDLE acceptThread = CreateThread(nullptr, 0, AcceptThread, (LPVOID)listener, 0, nullptr);

    wchar_t url[64];
    swprintf_s(url, L"http://127.0.0.1:%u/", port);
    hndcam::Log(L"HndViewer %s serving %s", HNDCAM_VERSION_WSTR, url);
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
            swprintf_s(text, L"The viewer page was not opened.\nOpen %s in your web browser, then start the viewer again.", url);
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
