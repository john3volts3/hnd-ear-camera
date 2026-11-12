#pragma once
#include <windows.h>
#include <wincodec.h>
#include <cstdint>
#include <vector>

namespace hndcam
{
    // 32-bit BGRA image, top-down rows, stride = width * 4.
    struct Image
    {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;

        void Resize(int w, int h)
        {
            width = w;
            height = h;
            pixels.assign((size_t)w * h, 0xFF000000);
        }
    };

    // Decodes a JPEG buffer to BGRA using WIC (the caller owns the factory; COM must be initialized).
    HRESULT DecodeJpeg(IWICImagingFactory* factory, const uint8_t* data, size_t size, Image& out);

    // Draws src into dst (cleared to black), centered, rotated by angleDeg (clockwise, screen space).
    // When circleMask is true, only the inscribed circle of the source is drawn.
    void RenderRotated(const Image& src, double angleDeg, bool circleMask, Image& dst);

    // Renders a dark status screen with a title and message lines (Direct2D/DirectWrite on a WIC bitmap).
    HRESULT RenderStatus(IWICImagingFactory* factory, const wchar_t* title, const wchar_t* message, Image& dst);

    // Converts BGRA to NV12 (BT.601, limited range). dstY/dstUV may use any pitch.
    void BgraToNv12(const Image& src, uint8_t* dstY, LONG pitchY, uint8_t* dstUV, LONG pitchUV);

    // Copies BGRA into an RGB32 buffer with the given pitch (pitch may be negative for bottom-up buffers).
    void BgraToRgb32(const Image& src, uint8_t* scanline0, LONG pitch);
}
