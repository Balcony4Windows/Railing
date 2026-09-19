#include "balcony/core/IconExtractor.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <utility>

namespace balcony::core
{
    namespace
    {
        struct ScopedIcon
        {
            HICON handle = nullptr;
            ~ScopedIcon() { if (handle) DestroyIcon(handle); }
        };

        struct ScopedBitmap
        {
            HBITMAP handle = nullptr;
            ~ScopedBitmap() { if (handle) DeleteObject(handle); }
        };

        struct ScopedDC
        {
            HDC handle = nullptr;
            ~ScopedDC() { if (handle) ReleaseDC(nullptr, handle); }
        };

        // Does the actual HICON -> RGBA conversion. Does not take
        // ownership of `icon` -- ExtractIconPixels' icon (from
        // SHGetFileInfoW) must be destroyed by its caller, but a
        // window's own class/WM_GETICON icon belongs to that window and
        // must NOT be destroyed here.
        bool ExtractIconPixelsFromHandle(HICON icon, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight)
        {
            if (!icon)
            {
                return false;
            }

            ICONINFO iconInfo{};
            if (!GetIconInfo(icon, &iconInfo))
            {
                return false;
            }
            ScopedBitmap colorBitmap{iconInfo.hbmColor};
            ScopedBitmap maskBitmap{iconInfo.hbmMask};

            if (!colorBitmap.handle)
            {
                // Legacy monochrome-only icons (no color plane, AND+XOR
                // mask stacked in hbmMask) aren't handled -- effectively
                // unseen among real .exe/.lnk/document/window icons on
                // modern Windows.
                return false;
            }

            BITMAP bitmap{};
            if (!GetObject(colorBitmap.handle, sizeof(bitmap), &bitmap))
            {
                return false;
            }

            const uint32_t width = static_cast<uint32_t>(bitmap.bmWidth);
            const uint32_t height = static_cast<uint32_t>(bitmap.bmHeight);

            ScopedDC dc{GetDC(nullptr)};
            if (!dc.handle)
            {
                return false;
            }

            BITMAPINFO colorInfo{};
            colorInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            colorInfo.bmiHeader.biWidth = static_cast<LONG>(width);
            colorInfo.bmiHeader.biHeight = -static_cast<LONG>(height); // negative = top-down
            colorInfo.bmiHeader.biPlanes = 1;
            colorInfo.bmiHeader.biBitCount = 32;
            colorInfo.bmiHeader.biCompression = BI_RGB;

            // 32bpp/BI_RGB rows are always 4-byte aligned (width * 4 is
            // already a multiple of 4) -- no stride padding to account
            // for here, unlike the 1bpp mask below.
            std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
            if (GetDIBits(dc.handle, colorBitmap.handle, 0, height, pixels.data(), &colorInfo, DIB_RGB_COLORS) == 0)
            {
                return false;
            }

            // GetDIBits with 32bpp/BI_RGB writes B,G,R,A per pixel in
            // memory; Texture::CreateFromPixels expects R8G8B8A8
            // (DXGI_FORMAT_R8G8B8A8_UNORM), so swap B and R in place.
            bool anyAlpha = false;
            for (size_t i = 0; i < pixels.size(); i += 4)
            {
                std::swap(pixels[i + 0], pixels[i + 2]);
                anyAlpha |= pixels[i + 3] != 0;
            }

            if (!anyAlpha)
            {
                // Older/non-32bpp-authored icons have no real alpha
                // channel -- every pixel decoded to alpha=0, which would
                // render as fully invisible. Fall back to synthesizing
                // alpha from the icon's 1bpp AND mask (opaque where the
                // mask bit is 0, per the classic AND/XOR icon-draw
                // convention).
                BITMAPINFO maskInfo{};
                maskInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                maskInfo.bmiHeader.biWidth = static_cast<LONG>(width);
                maskInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
                maskInfo.bmiHeader.biPlanes = 1;
                maskInfo.bmiHeader.biBitCount = 1;
                maskInfo.bmiHeader.biCompression = BI_RGB;

                // Unlike the 32bpp buffer above, 1bpp DIB rows ARE
                // padded to 4-byte (DWORD) boundaries -- stride must be
                // computed explicitly here.
                const uint32_t maskStride = ((width + 31) / 32) * 4;
                std::vector<uint8_t> maskBits(static_cast<size_t>(maskStride) * height);
                if (GetDIBits(dc.handle, maskBitmap.handle, 0, height, maskBits.data(), &maskInfo, DIB_RGB_COLORS) != 0)
                {
                    for (uint32_t y = 0; y < height; ++y)
                    {
                        for (uint32_t x = 0; x < width; ++x)
                        {
                            const uint8_t maskByte = maskBits[y * maskStride + x / 8];
                            const bool transparent = (maskByte >> (7 - (x % 8))) & 1;
                            pixels[(static_cast<size_t>(y) * width + x) * 4 + 3] = transparent ? 0 : 255;
                        }
                    }
                }
                else
                {
                    // No usable mask either -- treat as fully opaque
                    // rather than shipping an invisible image.
                    for (size_t i = 3; i < pixels.size(); i += 4)
                    {
                        pixels[i] = 255;
                    }
                }
            }

            outRgba = std::move(pixels);
            outWidth = width;
            outHeight = height;
            return true;
        }
    }

    bool ExtractIconPixels(std::wstring_view path, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight)
    {
        // SHGetFileInfoW's shell path parser -- unlike CreateFile-style
        // APIs -- doesn't accept forward slashes, so normalize them
        // first; otherwise it fails with ERROR_NO_TOKEN, which gives no
        // hint that the path itself was the problem.
        std::wstring pathStr(path);
        std::replace(pathStr.begin(), pathStr.end(), L'/', L'\\');

        SHFILEINFOW shfi{};
        if (!SHGetFileInfoW(pathStr.c_str(), 0, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_LARGEICON))
        {
            return false;
        }
        ScopedIcon icon{shfi.hIcon};

        return ExtractIconPixelsFromHandle(icon.handle, outRgba, outWidth, outHeight);
    }

    bool ExtractWindowIconPixels(uint64_t id, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight)
    {
        const HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
        if (!IsWindow(hwnd))
        {
            return false;
        }

        // Try what the window explicitly set at runtime first (most
        // apps set this, and it's often more specific -- e.g. per-
        // document or per-profile icons); fall back to its window
        // class's icon for windows that never answer WM_GETICON. Using
        // SendMessageTimeoutW rather than a plain SendMessageW: this
        // targets another process's window, and a hung target must not
        // be allowed to block the DE's own message loop.
        HICON icon = nullptr;
        DWORD_PTR result = 0;

        if (SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result) && result)
        {
            icon = reinterpret_cast<HICON>(result);
        }
        if (!icon && SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result) && result)
        {
            icon = reinterpret_cast<HICON>(result);
        }
        if (!icon)
        {
            icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
        }
        if (!icon)
        {
            icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
        }

        // Not owned here -- every one of the above belongs to the
        // window or its class, not us.
        return ExtractIconPixelsFromHandle(icon, outRgba, outWidth, outHeight);
    }
}
