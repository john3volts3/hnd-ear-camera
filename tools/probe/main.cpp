// HndCamProbe: console test tool for the camera client (no admin rights, no virtual camera).
// Usage: HndCamProbe [seconds=10] [outputDir=.] [norotate]
// Prints the connection state once per second and saves the rendered output as PNG every 2 seconds.
#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include "CameraClient.h"
#include "HndCam.h"
#include "Log.h"

using Microsoft::WRL::ComPtr;

static HRESULT SavePng(IWICImagingFactory* factory, const hndcam::Image& image, const wchar_t* path)
{
    ComPtr<IWICStream> stream;
    HRESULT hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path, GENERIC_WRITE);

    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
    if (SUCCEEDED(hr)) hr = frame->SetSize(image.width, image.height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WritePixels(image.height, image.width * 4, (UINT)(image.pixels.size() * 4),
        reinterpret_cast<BYTE*>(const_cast<uint32_t*>(image.pixels.data())));
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    return hr;
}

int wmain(int argc, wchar_t** argv)
{
    const int seconds = argc > 1 ? _wtoi(argv[1]) : 10;
    const wchar_t* outDir = argc > 2 ? argv[2] : L".";
    const bool rotate = !(argc > 3 && _wcsicmp(argv[3], L"norotate") == 0);

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
        return 1;

    hndcam::LogSetConsole(true);
    wprintf(L"HndCamProbe %s - %d s, output '%s', rotation %s\n", HNDCAM_VERSION_WSTR, seconds, outDir, rotate ? L"on" : L"off");

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
        return 2;

    int saved = 0;
    {
        hndcam::CameraClient camera(HNDCAM_OUT_WIDTH, HNDCAM_OUT_HEIGHT);
        camera.SetAutoRotate(rotate);
        camera.Start();

        hndcam::Image image;
        uint64_t lastFrames = 0;
        for (int s = 1; s <= seconds; s++)
        {
            Sleep(1000);
            uint64_t frames = camera.FramesDecoded();
            wprintf(L"t=%3ds state=%-10s fps=%2llu angle=%6.1f\n", s, hndcam::StateName(camera.State()), frames - lastFrames, camera.LastAngle());
            lastFrames = frames;

            if (s % 2 == 0)
            {
                camera.CopyLatest(image);
                wchar_t path[MAX_PATH];
                swprintf_s(path, L"%s\\probe_%03d.png", outDir, s);
                HRESULT hr = SavePng(wic.Get(), image, path);
                if (SUCCEEDED(hr))
                    saved++;
                else
                    wprintf(L"save %s failed 0x%08X\n", path, (unsigned)hr);
            }
        }
        camera.Stop();
    }

    wprintf(L"done, %d image(s) saved\n", saved);
    wic.Reset();
    CoUninitialize();
    return 0;
}
