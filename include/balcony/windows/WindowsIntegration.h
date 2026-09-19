#pragma once

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <string_view>

// Win32/Windows integration: HWNDs, WndProc, monitors, virtual desktops.
// See CLAUDE.md sections 11 and 12.
namespace balcony::windows
{
    // A single top-level window driving the message pump. Monitor and
    // virtual-desktop integration will live alongside this as the module grows.
    //
    // Created borderless and excluded from the taskbar/alt-tab: this is a DE
    // surface, not a regular application window.
    class Window
    {
    public:
        using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
        using MouseClickCallback = std::function<void(int x, int y)>;

        // iconResourceId, if non-zero, names an ICON resource (RT_ICON group)
        // in the host module to use as the window class icon.
        bool Create(std::wstring_view title, uint32_t width, uint32_t height, int iconResourceId = 0);
        void Show();

        // Drains pending messages without blocking. Returns false once WM_QUIT
        // has been posted, at which point the caller should stop rendering.
        bool PumpMessages();

        HWND Handle() const { return _hwnd; }
        uint32_t Width() const { return _width; }
        uint32_t Height() const { return _height; }

        void SetResizeCallback(ResizeCallback callback) { _onResize = std::move(callback); }

        // Fired on button-up, in client (window-relative) pixel
        // coordinates. Neither hit-tests nor knows about anything above
        // Win32 -- that's for the caller to do with the coordinates.
        void SetRightClickCallback(MouseClickCallback callback) { _onRightClick = std::move(callback); }
        void SetLeftClickCallback(MouseClickCallback callback) { _onLeftClick = std::move(callback); }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
        LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

        HWND _hwnd = nullptr;
        uint32_t _width = 0;
        uint32_t _height = 0;
        ResizeCallback _onResize;
        MouseClickCallback _onRightClick;
        MouseClickCallback _onLeftClick;
    };
}
