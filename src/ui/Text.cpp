#include "balcony/ui/Text.h"

#include "balcony/renderer/GraphicsDevice.h"
#include "balcony/renderer/CommandQueue.h"

#include <Windows.h>

#include <algorithm>
#include <vector>

namespace balcony::ui
{
    namespace
    {
        struct GdiTextBitmap
        {
            std::vector<uint8_t> rgba;
            int width = 0;
            int height = 0;
        };

        // Renders white text on black via GDI, then folds the resulting
        // luminance into the alpha channel of an opaque-white RGBA buffer --
        // the standard trick for turning GDI's non-alpha-aware anti-aliasing
        // into a properly blendable texture.
        bool RasterizeText(std::wstring_view text, const wchar_t* fontFamily, int fontHeightPx, GdiTextBitmap& out)
        {
            HDC screenDC = GetDC(nullptr);
            HDC measureDC = CreateCompatibleDC(screenDC);
            ReleaseDC(nullptr, screenDC);
            if (!measureDC)
                return false;

            HFONT font = CreateFontW(
                -fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, fontFamily);
            if (!font)
            {
                DeleteDC(measureDC);
                return false;
            }

            HGDIOBJ oldMeasureFont = SelectObject(measureDC, font);

            RECT measureRect{0, 0, 0, 0};
            DrawTextW(measureDC, text.data(), static_cast<int>(text.size()), &measureRect, DT_LEFT | DT_NOPREFIX | DT_CALCRECT);

            const int width = std::max<LONG>(1, measureRect.right - measureRect.left);
            const int height = std::max<LONG>(1, measureRect.bottom - measureRect.top);

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height; // Top-down.
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HDC dibDC = CreateCompatibleDC(measureDC);
            HBITMAP bitmap = dibDC ? CreateDIBSection(dibDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
            if (!bitmap || !bits)
            {
                if (dibDC) DeleteDC(dibDC);
                SelectObject(measureDC, oldMeasureFont);
                DeleteObject(font);
                DeleteDC(measureDC);
                return false;
            }

            HGDIOBJ oldBitmap = SelectObject(dibDC, bitmap);
            HGDIOBJ oldDibFont = SelectObject(dibDC, font);

            RECT fillRect{0, 0, width, height};
            FillRect(dibDC, &fillRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            SetTextColor(dibDC, RGB(255, 255, 255));
            SetBkMode(dibDC, TRANSPARENT);
            DrawTextW(dibDC, text.data(), static_cast<int>(text.size()), &fillRect, DT_LEFT | DT_NOPREFIX);

            GdiFlush();

            out.width = width;
            out.height = height;
            out.rgba.resize(static_cast<size_t>(width) * height * 4);

            const uint8_t* src = static_cast<const uint8_t*>(bits);
            for (int i = 0; i < width * height; ++i)
            {
                const uint8_t luminance = src[i * 4 + 2]; // B,G,R,X -- text is grayscale, any channel works.
                out.rgba[i * 4 + 0] = 255;
                out.rgba[i * 4 + 1] = 255;
                out.rgba[i * 4 + 2] = 255;
                out.rgba[i * 4 + 3] = luminance;
            }

            SelectObject(dibDC, oldDibFont);
            SelectObject(dibDC, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(dibDC);
            SelectObject(measureDC, oldMeasureFont);
            DeleteObject(font);
            DeleteDC(measureDC);
            return true;
        }
    }

    bool Text::SetText(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                        balcony::renderer::PrimitiveRenderer& renderer, std::wstring_view text)
    {
        GdiTextBitmap bitmap;
        if (!RasterizeText(text, _fontFamily.c_str(), _fontHeightPx, bitmap))
            return false;

        _texture.CreateFromPixels(device, queue, renderer,
                                   static_cast<uint32_t>(bitmap.width), static_cast<uint32_t>(bitmap.height),
                                   bitmap.rgba.data());
        SetSize(static_cast<float>(bitmap.width), static_cast<float>(bitmap.height));
        return true;
    }

    void Text::Draw(balcony::renderer::PrimitiveRenderer& renderer) const
    {
        VisualComponent::Draw(renderer);

        if (!_texture.IsValid())
        {
            return;
        }

        renderer.DrawQuad(Bounds(), _texture.SrvIndex(), {0.0f, 0.0f, 1.0f, 1.0f}, _color);
    }
}
