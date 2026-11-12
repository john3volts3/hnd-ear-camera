#include "ImageOps.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace hndcam
{
    HRESULT DecodeJpeg(IWICImagingFactory* factory, const uint8_t* data, size_t size, Image& out)
    {
        if (!factory || !data || size < 4)
            return E_INVALIDARG;

        ComPtr<IWICStream> stream;
        HRESULT hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(const_cast<BYTE*>(data), (DWORD)size);

        ComPtr<IWICBitmapDecoder> decoder;
        if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(stream.Get(), &GUID_ContainerFormatJpeg, WICDecodeMetadataCacheOnDemand, &decoder);

        ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);

        ComPtr<IWICFormatConverter> converter;
        if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);

        UINT w = 0, h = 0;
        if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);
        if (SUCCEEDED(hr) && (w == 0 || h == 0 || w > 4096 || h > 4096))
            hr = E_UNEXPECTED;

        if (SUCCEEDED(hr))
        {
            if (out.width != (int)w || out.height != (int)h)
                out.Resize((int)w, (int)h);
            hr = converter->CopyPixels(nullptr, w * 4, (UINT)(out.pixels.size() * 4), reinterpret_cast<BYTE*>(out.pixels.data()));
        }
        return hr;
    }

    void RenderRotated(const Image& src, double angleDeg, bool circleMask, Image& dst)
    {
        std::fill(dst.pixels.begin(), dst.pixels.end(), 0xFF000000);
        if (src.width <= 1 || src.height <= 1)
            return;

        const double srcCx = (src.width - 1) / 2.0;
        const double srcCy = (src.height - 1) / 2.0;
        const double dstCx = (dst.width - 1) / 2.0;
        const double dstCy = (dst.height - 1) / 2.0;

        // scale the source so its short side fits the destination height
        const double srcSide = (double)std::min(src.width, src.height);
        const double scale = srcSide / (double)std::min(dst.height, dst.width); // source pixels per destination pixel
        const double radius = srcSide / 2.0;
        const double radius2 = radius * radius;

        // inverse mapping: for each destination pixel, rotate back into source space
        const double rad = -angleDeg * 3.14159265358979323846 / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);

        for (int y = 0; y < dst.height; y++)
        {
            uint32_t* row = dst.pixels.data() + (size_t)y * dst.width;
            const double dy = (y - dstCy) * scale;
            for (int x = 0; x < dst.width; x++)
            {
                const double dx = (x - dstCx) * scale;
                if (circleMask && dx * dx + dy * dy > radius2)
                    continue;

                const double sx = c * dx - s * dy + srcCx;
                const double sy = s * dx + c * dy + srcCy;
                if (sx < 0 || sy < 0 || sx > src.width - 1 || sy > src.height - 1)
                    continue;

                // bilinear sampling
                const int x0 = (int)sx;
                const int y0 = (int)sy;
                const int x1 = std::min(x0 + 1, src.width - 1);
                const int y1 = std::min(y0 + 1, src.height - 1);
                const double fx = sx - x0;
                const double fy = sy - y0;
                const uint32_t p00 = src.pixels[(size_t)y0 * src.width + x0];
                const uint32_t p10 = src.pixels[(size_t)y0 * src.width + x1];
                const uint32_t p01 = src.pixels[(size_t)y1 * src.width + x0];
                const uint32_t p11 = src.pixels[(size_t)y1 * src.width + x1];

                uint32_t outPixel = 0xFF000000;
                for (int shift = 0; shift < 24; shift += 8)
                {
                    const double top = ((p00 >> shift) & 0xFF) * (1 - fx) + ((p10 >> shift) & 0xFF) * fx;
                    const double bottom = ((p01 >> shift) & 0xFF) * (1 - fx) + ((p11 >> shift) & 0xFF) * fx;
                    const uint32_t v = (uint32_t)(top * (1 - fy) + bottom * fy + 0.5);
                    outPixel |= (std::min(v, 255u) << shift);
                }
                row[x] = outPixel;
            }
        }
    }

    HRESULT RenderStatus(IWICImagingFactory* factory, const wchar_t* title, const wchar_t* message, Image& dst)
    {
        if (!factory)
            return E_INVALIDARG;

        ComPtr<IWICBitmap> bitmap;
        HRESULT hr = factory->CreateBitmap(dst.width, dst.height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);

        ComPtr<ID2D1Factory> d2d;
        if (SUCCEEDED(hr)) hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());

        ComPtr<ID2D1RenderTarget> target;
        if (SUCCEEDED(hr))
        {
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            hr = d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &target);
        }

        ComPtr<IDWriteFactory> dwrite;
        if (SUCCEEDED(hr)) hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));

        ComPtr<IDWriteTextFormat> titleFormat, textFormat;
        if (SUCCEEDED(hr)) hr = dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 30.0f, L"en-us", &titleFormat);
        if (SUCCEEDED(hr)) hr = dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 19.0f, L"en-us", &textFormat);
        if (SUCCEEDED(hr))
        {
            titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }

        ComPtr<ID2D1SolidColorBrush> white, grey, accent;
        if (SUCCEEDED(hr)) hr = target->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.95f), &white);
        if (SUCCEEDED(hr)) hr = target->CreateSolidColorBrush(D2D1::ColorF(0.75f, 0.78f, 0.82f), &grey);
        if (SUCCEEDED(hr)) hr = target->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.55f, 0.95f), &accent);

        if (SUCCEEDED(hr))
        {
            const float w = (float)dst.width;
            const float h = (float)dst.height;
            target->BeginDraw();
            target->Clear(D2D1::ColorF(0.09f, 0.10f, 0.12f));
            target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(w / 2, h * 0.27f), 42, 42), accent.Get(), 5);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(w / 2, h * 0.27f), 16, 16), accent.Get());
            target->DrawTextW(title, (UINT32)wcslen(title), titleFormat.Get(), D2D1::RectF(20, h * 0.42f, w - 20, h * 0.52f), white.Get());
            target->DrawTextW(message, (UINT32)wcslen(message), textFormat.Get(), D2D1::RectF(40, h * 0.55f, w - 40, h - 20), grey.Get());
            hr = target->EndDraw();
        }

        if (SUCCEEDED(hr))
        {
            target.Reset();
            hr = bitmap->CopyPixels(nullptr, dst.width * 4, (UINT)(dst.pixels.size() * 4), reinterpret_cast<BYTE*>(dst.pixels.data()));
        }
        return hr;
    }

    void BgraToNv12(const Image& src, uint8_t* dstY, LONG pitchY, uint8_t* dstUV, LONG pitchUV)
    {
        const int w = src.width & ~1;
        const int h = src.height & ~1;
        for (int y = 0; y < h; y += 2)
        {
            const uint32_t* r0 = src.pixels.data() + (size_t)y * src.width;
            const uint32_t* r1 = r0 + src.width;
            uint8_t* y0 = dstY + (ptrdiff_t)y * pitchY;
            uint8_t* y1 = y0 + pitchY;
            uint8_t* uv = dstUV + (ptrdiff_t)(y / 2) * pitchUV;
            for (int x = 0; x < w; x += 2)
            {
                int sumR = 0, sumG = 0, sumB = 0;
                const uint32_t px[4] = { r0[x], r0[x + 1], r1[x], r1[x + 1] };
                uint8_t* yOut[4] = { y0 + x, y0 + x + 1, y1 + x, y1 + x + 1 };
                for (int i = 0; i < 4; i++)
                {
                    const int b = px[i] & 0xFF;
                    const int g = (px[i] >> 8) & 0xFF;
                    const int r = (px[i] >> 16) & 0xFF;
                    *yOut[i] = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                    sumR += r; sumG += g; sumB += b;
                }
                const int r = sumR / 4, g = sumG / 4, b = sumB / 4;
                uv[x] = (uint8_t)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                uv[x + 1] = (uint8_t)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
            }
        }
    }

    void BgraToRgb32(const Image& src, uint8_t* scanline0, LONG pitch)
    {
        for (int y = 0; y < src.height; y++)
        {
            memcpy(scanline0 + (ptrdiff_t)y * pitch, src.pixels.data() + (size_t)y * src.width, (size_t)src.width * 4);
        }
    }
}
