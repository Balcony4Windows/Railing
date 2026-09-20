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

        // Re-asserts bottom-of-z-order placement. A one-time SetWindowPos
        // in Show() isn't enough to stay there: Windows reshuffles
        // z-order on ordinary desktop activity (another window opening,
        // closing, or being activated), and nothing keeps re-pinning this
        // surface to the back afterward. Cheap (SWP_NOMOVE/NOSIZE/
        // NOACTIVATE, no repaint) -- call once per frame from the main
        // loop.
        void KeepAtBottom();

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

        // The three extra pieces a drag gesture needs on top of the
        // plain click above: where the button first went down, where
        // the cursor moves while it's held, and a signal if mouse
        // capture is stolen (e.g. alt-tab) before a button-up ever
        // arrives -- see HandleMessage's WM_LBUTTONDOWN/WM_MOUSEMOVE/
        // WM_CAPTURECHANGED handling. Distinguishing an ordinary click
        // from a drag is the caller's job (DesktopEnvironment); this
        // class just forwards the raw Win32 events.
        void SetLeftButtonDownCallback(MouseClickCallback callback) { _onLeftButtonDown = std::move(callback); }
        void SetMouseMoveCallback(MouseClickCallback callback) { _onMouseMove = std::move(callback); }
        void SetCaptureLostCallback(std::function<void()> callback) { _onCaptureLost = std::move(callback); }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
        LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

        HWND _hwnd = nullptr;
        uint32_t _width = 0;
        uint32_t _height = 0;
        ResizeCallback _onResize;
        MouseClickCallback _onRightClick;
        MouseClickCallback _onLeftClick;
        MouseClickCallback _onLeftButtonDown;
        MouseClickCallback _onMouseMove;
        std::function<void()> _onCaptureLost;
    };
}
