#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "CameraClient.h"
#include "Log.h"
#include <wincodec.h>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace hndcam
{
    namespace
    {
        constexpr u_short DiscoveryPort = 46526;
        constexpr u_short ImuPort = 52219;
        constexpr u_short VideoPort = 44506;

        const uint8_t CmdDiscover[] = { 0x66, 0x39, 0x01, 0x01 };
        const uint8_t CmdImuOn[] = { 0x86, 0x06, 0x01 };
        const uint8_t CmdVideoOn[] = { 0x20, 0x36 };
        const uint8_t CmdVideoOff[] = { 0x20, 0x37 };

        constexpr DWORD DiscoveryReplyMs = 1200;
        constexpr DWORD RetryDelayMs = 1500;
        constexpr ULONGLONG ResendStartMs = 2500;   // no frame for this long: resend start commands
        constexpr ULONGLONG ReconnectMs = 7000;     // no frame for this long: rediscover the camera
        constexpr ULONGLONG PartialExpiryMs = 1000;
        constexpr ULONGLONG JoinRetryMs = 6000;      // Wi-Fi scan for a camera network at most this often

        // Display rotation (clockwise degrees) derived from the accelerometer.
        // Measured on an HND-NE3: the firmware angle (bytes 18-19) equals -90 - roll with
        // roll = atan2(ay, -az), and the picture is upright when rotated clockwise by the
        // firmware angle, i.e. by -90 - roll (checked by the user in the viewer).
        constexpr double RotationBiasDeg = -90.0;
        // Below this share of gravity in the roll plane (camera pointing up/down), roll is meaningless: hold.
        constexpr double MinRollPlaneG = 0.45;
        constexpr double SmoothingFactor = 0.25;

        int16_t Accel12(const uint8_t* p)
        {
            // 12-bit two's complement: high byte, then low nibble
            return (int16_t)(((p[0] << 8) | ((p[1] & 0x0F) << 4))) >> 4;
        }

        std::wstring JsonValue(const std::string& json, const char* key)
        {
            std::string pattern = std::string("\"") + key + "\"";
            size_t k = json.find(pattern);
            if (k == std::string::npos)
                return {};
            size_t q1 = json.find('"', json.find(':', k + pattern.size()) + 1);
            size_t q2 = q1 == std::string::npos ? q1 : json.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos)
                return {};
            std::string v = json.substr(q1 + 1, q2 - q1 - 1);
            return std::wstring(v.begin(), v.end());
        }

        ULONGLONG Now() { return GetTickCount64(); }
    }

    const wchar_t* StateName(CameraState state)
    {
        switch (state)
        {
        case CameraState::NoWifi: return L"NoWifi";
        case CameraState::Searching: return L"Searching";
        case CameraState::Connecting: return L"Connecting";
        case CameraState::Streaming: return L"Streaming";
        case CameraState::JoiningWifi: return L"JoiningWifi";
        default: return L"Stopped";
        }
    }

    CameraClient::CameraClient(int outWidth, int outHeight) :
        _outWidth(outWidth),
        _outHeight(outHeight)
    {
        _stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        _latest.Resize(outWidth, outHeight);
        _render.Resize(outWidth, outHeight);
    }

    CameraClient::~CameraClient()
    {
        Stop();
        if (_stopEvent)
            CloseHandle(_stopEvent);
    }

    void CameraClient::Start()
    {
        if (_thread)
            return;
        ResetEvent(_stopEvent);
        _thread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
        if (!_thread)
            LogHr(L"CreateThread", HRESULT_FROM_WIN32(GetLastError()));
    }

    void CameraClient::Stop()
    {
        if (!_thread)
            return;
        SetEvent(_stopEvent);
        if (WaitForSingleObject(_thread, 5000) != WAIT_OBJECT_0)
            Log(L"CameraClient: worker did not stop in time");
        CloseHandle(_thread);
        _thread = nullptr;
        SetState(CameraState::Stopped);
    }

    uint64_t CameraClient::CopyLatest(Image& out)
    {
        AcquireSRWLockShared(&_lock);
        if (out.width != _latest.width || out.height != _latest.height)
            out.Resize(_latest.width, _latest.height);
        memcpy(out.pixels.data(), _latest.pixels.data(), _latest.pixels.size() * 4);
        uint64_t seq = _latestSeq;
        ReleaseSRWLockShared(&_lock);
        return seq;
    }

    uint64_t CameraClient::WaitJpeg(uint64_t lastSeq, std::vector<uint8_t>& out, DWORD timeoutMs)
    {
        AcquireSRWLockExclusive(&_lock);
        const ULONGLONG deadline = Now() + timeoutMs;
        while (_jpegSeq == lastSeq)
        {
            const ULONGLONG now = Now();
            if (now >= deadline || !SleepConditionVariableSRW(&_jpegReady, &_lock, (DWORD)(deadline - now), 0))
                break;
        }
        const uint64_t seq = _jpegSeq;
        if (seq != lastSeq)
            out = _latestJpeg;
        ReleaseSRWLockExclusive(&_lock);
        return seq;
    }

    CameraClient::Status CameraClient::GetStatus()
    {
        Status status{};
        status.state = State();
        status.frames = _framesDecoded;
        AcquireSRWLockShared(&_lock);
        status.haveAngle = _publishedHaveAngle;
        status.angle = _publishedAngle;
        status.cameraName = _publishedName;
        ReleaseSRWLockShared(&_lock);
        return status;
    }

    void CameraClient::SetState(CameraState state)
    {
        LONG previous = InterlockedExchange(&_state, (LONG)state);
        if (previous != (LONG)state)
        {
            Log(L"CameraClient: state %s -> %s", StateName((CameraState)previous), StateName(state));
            if (state != CameraState::Streaming && state != CameraState::Stopped)
                PublishStatus();
        }
    }

    bool CameraClient::WaitStop(DWORD ms) const
    {
        return WaitForSingleObject(_stopEvent, ms) == WAIT_OBJECT_0;
    }

    DWORD WINAPI CameraClient::ThreadProc(LPVOID param)
    {
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        WSADATA wsa{};
        int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (wsaErr == 0)
        {
            static_cast<CameraClient*>(param)->Run();
            WSACleanup();
        }
        else
        {
            Log(L"WSAStartup failed: %d", wsaErr);
        }
        if (SUCCEEDED(hrCo))
            CoUninitialize();
        return 0;
    }

    void CameraClient::Run()
    {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&_wic));
        if (FAILED(hr))
        {
            LogHr(L"CoCreateInstance(WICImagingFactory)", hr);
            return;
        }

        _haveAngle = false;
        _angle = 0;
        SetState(CameraState::Searching);
        PublishStatus();

        while (!WaitStop(0))
        {
            SOCKET sock = INVALID_SOCKET;
            sockaddr_in camera{};
            if (Discover(sock, camera))
            {
                Stream(sock, camera);
                closesocket(sock);
            }
            else if (_autoConnectWifi && Now() - _lastJoinAttempt >= JoinRetryMs)
            {
                // no camera on the current network: join a camera network if one is visible
                _lastJoinAttempt = Now();
                std::wstring joined = _wifi.JoinCameraNetwork(_stopEvent, [this](const std::wstring& ssid)
                    {
                        _joiningSsid = ssid;
                        SetState(CameraState::JoiningWifi);
                    });
                if (!joined.empty())
                    continue; // discover right away (Discover retries until DHCP is done)
            }
            if (WaitStop(RetryDelayMs))
                break;
        }

        if (_autoConnectWifi)
            _wifi.Release(); // back to the usual Wi-Fi network if we switched it
        _wic->Release();
        _wic = nullptr;
    }

    std::vector<CameraClient::WifiAddress> CameraClient::GetWifiAddresses()
    {
        std::vector<WifiAddress> result;
        ULONG size = 16 * 1024;
        std::vector<uint8_t> buffer(size);
        ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        ULONG err = GetAdaptersAddresses(AF_INET, flags, nullptr, (PIP_ADAPTER_ADDRESSES)buffer.data(), &size);
        if (err == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(size);
            err = GetAdaptersAddresses(AF_INET, flags, nullptr, (PIP_ADAPTER_ADDRESSES)buffer.data(), &size);
        }
        if (err != NO_ERROR)
            return result;

        for (auto a = (PIP_ADAPTER_ADDRESSES)buffer.data(); a; a = a->Next)
        {
            if (a->IfType != IF_TYPE_IEEE80211 || a->OperStatus != IfOperStatusUp)
                continue;
            for (auto u = a->FirstUnicastAddress; u; u = u->Next)
            {
                if (u->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                WifiAddress w{};
                w.local = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;
                ULONG mask = 0;
                ConvertLengthToIpv4Mask(u->OnLinkPrefixLength, &mask);
                w.broadcast.s_addr = w.local.s_addr | ~mask;
                result.push_back(w);
            }
        }
        return result;
    }

    bool CameraClient::Discover(SOCKET& sockOut, sockaddr_in& cameraOut)
    {
        auto addresses = GetWifiAddresses();
        if (addresses.empty())
        {
            SetState(CameraState::NoWifi);
            return false;
        }
        if (State() != CameraState::Searching)
            SetState(CameraState::Searching);

        for (const auto& wifi : addresses)
        {
            SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s == INVALID_SOCKET)
                continue;

            BOOL yes = TRUE;
            setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
            int rcvBuf = 1 << 20;
            setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvBuf, sizeof(rcvBuf));

            // bind to the Wi-Fi address: nothing ever leaves through another interface
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_addr = wifi.local;
            if (bind(s, (sockaddr*)&local, sizeof(local)) != 0)
            {
                closesocket(s);
                continue;
            }

            sockaddr_in bcast{};
            bcast.sin_family = AF_INET;
            bcast.sin_port = htons(DiscoveryPort);
            bcast.sin_addr = wifi.broadcast;
            sendto(s, (const char*)CmdDiscover, sizeof(CmdDiscover), 0, (sockaddr*)&bcast, sizeof(bcast));

            ULONGLONG deadline = Now() + DiscoveryReplyMs;
            while (Now() < deadline)
            {
                DWORD timeout = 200;
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
                char buf[2048];
                sockaddr_in from{};
                int fromLen = sizeof(from);
                int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
                if (WaitStop(0))
                {
                    closesocket(s);
                    return false;
                }
                if (n <= 0 || ntohs(from.sin_port) != DiscoveryPort)
                    continue;

                buf[n] = 0;
                std::string json(buf, n);
                std::wstring model = JsonValue(json, "model");
                std::wstring brand = JsonValue(json, "brand");
                if (brand.find(L"HND") == std::wstring::npos && model.find(L"EAR_CAMERA") == std::wstring::npos)
                {
                    Log(L"Discovery: ignoring device brand='%s' model='%s'", brand.c_str(), model.c_str());
                    continue;
                }

                wchar_t ip[32] = {};
                InetNtopW(AF_INET, &from.sin_addr, ip, ARRAYSIZE(ip));
                _cameraName = brand + L" " + model;
                const std::wstring ssid = _wifi.CurrentSsid();
                AcquireSRWLockExclusive(&_lock);
                _publishedName = ssid.empty() ? _cameraName : _cameraName + L" (" + ssid + L")";
                ReleaseSRWLockExclusive(&_lock);
                Log(L"Discovery: found %s at %s (soc=%s fw=%s mac=%s)", _cameraName.c_str(), ip,
                    JsonValue(json, "soc").c_str(), JsonValue(json, "firmware").c_str(), JsonValue(json, "macid").c_str());

                cameraOut = from;
                sockOut = s;
                return true;
            }
            closesocket(s);
        }
        return false;
    }

    void CameraClient::Stream(SOCKET sock, const sockaddr_in& camera)
    {
        sockaddr_in imuAddr = camera;
        imuAddr.sin_port = htons(ImuPort);
        sockaddr_in videoAddr = camera;
        videoAddr.sin_port = htons(VideoPort);

        auto sendStart = [&]()
        {
            sendto(sock, (const char*)CmdImuOn, sizeof(CmdImuOn), 0, (sockaddr*)&imuAddr, sizeof(imuAddr));
            sendto(sock, (const char*)CmdVideoOn, sizeof(CmdVideoOn), 0, (sockaddr*)&videoAddr, sizeof(videoAddr));
        };

        for (auto& p : _partials)
        {
            p.frameNumber = -1;
            p.chunks.clear();
        }

        SetState(CameraState::Connecting);
        _wifi.SetStreamingMode(true);
        sendStart();
        ULONGLONG started = Now();
        ULONGLONG lastStartSent = started;
        _lastFrameTick = 0;

        DWORD timeout = 200;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        std::vector<uint8_t> buf(2048);

        while (!WaitStop(0))
        {
            sockaddr_in from{};
            int fromLen = sizeof(from);
            int n = recvfrom(sock, (char*)buf.data(), (int)buf.size(), 0, (sockaddr*)&from, &fromLen);
            if (n > 0 && from.sin_addr.s_addr == camera.sin_addr.s_addr)
            {
                u_short port = ntohs(from.sin_port);
                if (port == VideoPort)
                    OnVideoPacket(buf.data(), n);
                else if (port == ImuPort)
                    OnImuPacket(buf.data(), n);
            }

            ULONGLONG now = Now();
            ULONGLONG lastActivity = _lastFrameTick ? _lastFrameTick : started;
            if (now - lastActivity > ReconnectMs)
            {
                Log(L"CameraClient: no video for %llu ms, reconnecting", now - lastActivity);
                break;
            }
            if (now - lastActivity > ResendStartMs && now - lastStartSent > ResendStartMs)
            {
                // short stall: keep showing the last picture, just ask for the video again
                Log(L"CameraClient: no video for %llu ms, resending start commands", now - lastActivity);
                sendStart();
                lastStartSent = now;
            }
        }

        sendto(sock, (const char*)CmdVideoOff, sizeof(CmdVideoOff), 0, (sockaddr*)&videoAddr, sizeof(videoAddr));
        _wifi.SetStreamingMode(false);
        if (!WaitStop(0))
            SetState(CameraState::Searching);
    }

    void CameraClient::OnVideoPacket(const uint8_t* data, int size)
    {
        // header: [0] frame number, [1] flags (bit0 = last chunk), [2] chunk number (1-based), [3] unknown
        // the last chunk carries a 5-byte trailer after the JPEG data
        if (size < 5)
            return;
        const int frameNumber = data[0];
        const bool last = (data[1] & 1) != 0;
        const int chunk = data[2];
        if (chunk < 1)
            return;
        int payload = size - 4 - (last ? 5 : 0);
        if (payload <= 0)
            return;

        ULONGLONG now = Now();
        PartialFrame* slot = nullptr;
        PartialFrame* oldest = &_partials[0];
        for (auto& p : _partials)
        {
            if (p.frameNumber >= 0 && now - p.lastUpdate > PartialExpiryMs)
                p.frameNumber = -1;
            if (p.frameNumber == frameNumber)
                slot = &p;
            if (p.lastUpdate < oldest->lastUpdate || p.frameNumber < 0)
                oldest = &p;
        }
        if (!slot)
        {
            slot = oldest;
            slot->frameNumber = frameNumber;
            slot->totalChunks = 0;
            slot->received = 0;
            slot->chunks.clear();
        }

        slot->lastUpdate = now;
        if ((size_t)chunk > slot->chunks.size())
            slot->chunks.resize(chunk);
        auto& c = slot->chunks[chunk - 1];
        if (c.empty())
            slot->received++;
        c.assign(data + 4, data + 4 + payload);
        if (last)
            slot->totalChunks = chunk;

        if (slot->totalChunks > 0 && slot->received == (size_t)slot->totalChunks)
        {
            std::vector<uint8_t> jpeg;
            for (int i = 0; i < slot->totalChunks; i++)
                jpeg.insert(jpeg.end(), slot->chunks[i].begin(), slot->chunks[i].end());
            slot->frameNumber = -1;
            slot->chunks.clear();
            OnJpeg(jpeg);
        }
    }

    void CameraClient::OnImuPacket(const uint8_t* data, int size)
    {
        if (size < 20)
            return;
        const double ax = Accel12(data);
        const double ay = Accel12(data + 2);
        const double az = Accel12(data + 4);
        const double g = std::sqrt(ax * ax + ay * ay + az * az);
        const double rollPlane = std::sqrt(ay * ay + az * az);
        if (g < 1 || rollPlane < MinRollPlaneG * g)
            return; // pointing up or down: keep the previous orientation

        const double roll = std::atan2(ay, -az) * 180.0 / 3.14159265358979323846;
        const double target = std::fmod(RotationBiasDeg - roll + 720.0, 360.0);
        if (!_haveAngle)
        {
            _angle = target;
            _haveAngle = true;
        }
        else
        {
            // low-pass filter on the shortest angular path
            double delta = std::fmod(target - _angle + 540.0, 360.0) - 180.0;
            _angle = std::fmod(_angle + delta * SmoothingFactor + 360.0, 360.0);
        }

        AcquireSRWLockExclusive(&_lock);
        _publishedAngle = _angle;
        _publishedHaveAngle = true;
        ReleaseSRWLockExclusive(&_lock);
    }

    void CameraClient::OnJpeg(const std::vector<uint8_t>& jpeg)
    {
        if (jpeg.size() < 4 || jpeg[0] != 0xFF || jpeg[1] != 0xD8)
            return;

        // raw JPEG for the browser viewer
        AcquireSRWLockExclusive(&_lock);
        _latestJpeg = jpeg;
        _jpegSeq++;
        ReleaseSRWLockExclusive(&_lock);
        WakeAllConditionVariable(&_jpegReady);

        if (_renderOutput)
        {
            HRESULT hr = DecodeJpeg(_wic, jpeg.data(), jpeg.size(), _decoded);
            if (FAILED(hr))
            {
                Log(L"CameraClient: JPEG decode failed 0x%08X (%zu bytes)", (unsigned)hr, jpeg.size());
                return;
            }
        }

        _framesDecoded++;
        _lastFrameTick = Now();
        if (State() != CameraState::Streaming)
            SetState(CameraState::Streaming);
        if (!_renderOutput)
            return;

        const bool rotate = InterlockedCompareExchange(&_autoRotate, 0, 0) != 0;
        RenderRotated(_decoded, (rotate && _haveAngle ? _angle : 0.0) + _rotationOffset, rotate, _render);
        Publish(_render);
    }

    void CameraClient::PublishStatus()
    {
        if (!_wic)
            return;

        const wchar_t* title = L"HND Ear Camera";
        std::wstring message;
        switch (State())
        {
        case CameraState::NoWifi:
            message = L"Wi-Fi is not connected.\nTurn the camera on: this PC joins its Wi-Fi network (HNDEC_...) automatically.";
            break;
        case CameraState::Searching:
            message = L"Searching for the camera...\nTurn the camera on: this PC joins its Wi-Fi network (HNDEC_...) automatically.\nThe camera must not be connected to a phone at the same time.";
            break;
        case CameraState::JoiningWifi:
            message = L"Connecting this PC's Wi-Fi to the camera network\n" + _joiningSsid + L"...";
            break;
        case CameraState::Connecting:
            message = L"Camera found, starting video...";
            break;
        default:
            return;
        }

        Image status;
        status.Resize(_outWidth, _outHeight);
        HRESULT hr = RenderStatus(_wic, title, message.c_str(), status);
        if (FAILED(hr))
            LogHr(L"RenderStatus", hr);
        Publish(status);
    }

    void CameraClient::Publish(const Image& image)
    {
        AcquireSRWLockExclusive(&_lock);
        memcpy(_latest.pixels.data(), image.pixels.data(), std::min(_latest.pixels.size(), image.pixels.size()) * 4);
        _latestSeq++;
        ReleaseSRWLockExclusive(&_lock);
    }
}
