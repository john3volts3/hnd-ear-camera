#pragma once
#include <winsock2.h> // before windows.h
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "ImageOps.h"
#include "WifiConnector.h"

namespace hndcam
{
    enum class CameraState
    {
        Stopped,
        NoWifi,       // no Wi-Fi adapter connected
        Searching,    // discovery broadcast sent, no camera answered
        Connecting,   // camera found, waiting for the first video frame
        Streaming,    // frames are flowing
        JoiningWifi,  // connecting the PC Wi-Fi to a camera network (HNDEC_...)
    };
    const wchar_t* StateName(CameraState state);

    // Talks to an HND-NE3 Wi-Fi ear camera (Beken BK7231 firmware, "EAR_CAMERA" model):
    //   discovery : 66 39 01 01 -> broadcast:46526, JSON reply with model/brand
    //   IMU on    : 86 06 01    -> camera:52219, 24-byte IMU packets back
    //   video on  : 20 36       -> camera:44506, JPEG chunks back (4-byte header)
    //   video off : 20 37       -> camera:44506
    // All traffic is sent from the Wi-Fi interface address only.
    // A worker thread keeps the latest rendered output image (rotated / status screen).
    class CameraClient
    {
    public:
        CameraClient(int outWidth, int outHeight);
        ~CameraClient();

        CameraClient(const CameraClient&) = delete;
        CameraClient& operator=(const CameraClient&) = delete;

        void Start();
        void Stop();

        // Copies the latest output image into 'out' and returns its sequence number.
        uint64_t CopyLatest(Image& out);

        CameraState State() const { return (CameraState)InterlockedCompareExchange(const_cast<volatile LONG*>(&_state), 0, 0); }
        void SetAutoRotate(bool enable) { InterlockedExchange(&_autoRotate, enable ? 1 : 0); }
        // Extra clockwise rotation in degrees, added to the automatic one (fine tuning). Call before Start().
        void SetRotationOffset(double degrees) { _rotationOffset = degrees; }
        // Join any visible camera network (HNDEC_...) when no camera answers; released on Stop(). Call before Start().
        void SetAutoConnectWifi(bool enable) { _autoConnectWifi = enable; }

        // Statistics for diagnostics
        uint64_t FramesDecoded() const { return _framesDecoded; }
        double LastAngle() const { return _angle; }

        // Raw JPEG access (browser viewer). With render output disabled, frames are not decoded at all.
        void SetRenderOutput(bool enable) { _renderOutput = enable; } // call before Start()
        // Waits up to timeoutMs for a frame newer than lastSeq; returns the frame sequence (unchanged on timeout).
        uint64_t WaitJpeg(uint64_t lastSeq, std::vector<uint8_t>& out, DWORD timeoutMs);

        struct Status
        {
            CameraState state;
            uint64_t frames;
            bool haveAngle;
            double angle;       // automatic display rotation, clockwise degrees
            std::wstring cameraName;
        };
        Status GetStatus();

    private:
        struct WifiAddress
        {
            in_addr local;
            in_addr broadcast;
        };

        struct PartialFrame
        {
            int frameNumber = -1;
            int totalChunks = 0;
            ULONGLONG lastUpdate = 0;
            std::vector<std::vector<uint8_t>> chunks; // index = chunk number - 1
            size_t received = 0;
        };

        static DWORD WINAPI ThreadProc(LPVOID param);
        void Run();
        bool Discover(SOCKET& sock, sockaddr_in& camera);
        void Stream(SOCKET sock, const sockaddr_in& camera);
        void OnVideoPacket(const uint8_t* data, int size);
        void OnImuPacket(const uint8_t* data, int size);
        void OnJpeg(const std::vector<uint8_t>& jpeg);
        void PublishStatus();
        void Publish(const Image& image);
        void SetState(CameraState state);
        bool WaitStop(DWORD ms) const;
        static std::vector<WifiAddress> GetWifiAddresses();

        const int _outWidth;
        const int _outHeight;
        HANDLE _thread = nullptr;
        HANDLE _stopEvent = nullptr;
        volatile LONG _state = (LONG)CameraState::Stopped;
        volatile LONG _autoRotate = 1;
        double _rotationOffset = 0;
        bool _renderOutput = true;
        bool _autoConnectWifi = true;

        // worker-thread data
        struct IWICImagingFactory* _wic = nullptr;
        PartialFrame _partials[4];
        Image _decoded;
        Image _render;
        double _angle = 0;          // smoothed display rotation in degrees
        bool _haveAngle = false;
        ULONGLONG _lastFrameTick = 0;
        uint64_t _framesDecoded = 0;
        std::wstring _cameraName;
        WifiConnector _wifi;
        ULONGLONG _lastJoinAttempt = 0;
        std::wstring _joiningSsid;

        // published output
        SRWLOCK _lock = SRWLOCK_INIT;
        Image _latest;
        uint64_t _latestSeq = 0;
        CONDITION_VARIABLE _jpegReady = CONDITION_VARIABLE_INIT;
        std::vector<uint8_t> _latestJpeg;
        uint64_t _jpegSeq = 0;
        bool _publishedHaveAngle = false;
        double _publishedAngle = 0;
        std::wstring _publishedName;
    };
}
