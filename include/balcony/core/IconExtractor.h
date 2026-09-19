#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// Extracts the shell-associated icon for a file/shortcut/executable and
// converts it to a top-down, tightly-packed RGBA8 buffer -- pure
// Win32/GDI, no D3D12 here (see balcony::ui::LuaBindings.cpp's
// Image::SetSystemIcon for the texture-upload half, which does need a
// GraphicsDevice and therefore cannot live in balcony_core).
namespace balcony::core
{
    // Resolves .lnk shortcuts and per-file-type icon associations the
    // same way Explorer does (via SHGetFileInfoW), so callers don't need
    // to special-case file types themselves. Returns false (leaving the
    // out-parameters untouched) if no icon could be extracted.
    bool ExtractIconPixels(std::wstring_view path, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight);

    // Same conversion, but for a live window's own icon rather than a
    // file's -- `id` is a HWND as produced by
    // balcony::core::EnumerateRunningWindows. Tries WM_GETICON first
    // (what the window explicitly set at runtime), then falls back to
    // its window class's icon for windows that never answer it. Returns
    // false if the window no longer exists or has no icon at all.
    bool ExtractWindowIconPixels(uint64_t id, std::vector<uint8_t>& outRgba, uint32_t& outWidth, uint32_t& outHeight);
}
