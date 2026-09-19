#include "balcony/windows/WindowsIntegration.h"

#include <windowsx.h>

namespace balcony::windows
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"BalconyWindow";
    }

    LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        Window* self = nullptr;

        if (msg == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<Window*>(createStruct->lpCreateParams);
            // CreateWindowExW dispatches WM_NCCREATE/WM_CREATE before it
            // returns, so _hwnd must be bound here rather than after.
            self->_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        else
        {
            self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self)
        {
            return self->HandleMessage(msg, wparam, lparam);
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    LRESULT Window::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam)
    {
        switch (msg)
        {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED)
            {
                _width = LOWORD(lparam);
                _height = HIWORD(lparam);
                if (_onResize)
                {
                    _onResize(_width, _height);
                }
            }
            return 0;

        case WM_ERASEBKGND:
            // D3D12 repaints every pixel every frame; skip GDI's default
            // background fill to avoid a flash/tear on show and resize.
            return 1;

        case WM_RBUTTONUP:
            if (_onRightClick)
            {
                _onRightClick(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            }
            return 0;

        case WM_LBUTTONUP:
            if (_onLeftClick)
            {
                _onLeftClick(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(_hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(_hwnd, msg, wparam, lparam);
        }
    }

    bool Window::Create(std::wstring_view title, uint32_t width, uint32_t height, int iconResourceId)
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const HICON icon = iconResourceId ? LoadIconW(instance, MAKEINTRESOURCEW(iconResourceId)) : nullptr;

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(WNDCLASSEXW);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &Window::WndProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = icon;
        windowClass.hIconSm = icon;
        windowClass.lpszClassName = kWindowClassName;

        static bool classRegistered = false;
        if (!classRegistered)
        {
            if (!RegisterClassExW(&windowClass))
            {
                return false;
            }
            classRegistered = true;
        }

        // Borderless (no title bar/frame) and excluded from the taskbar and
        // alt-tab: DE surfaces are not regular application windows.
        RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        constexpr DWORD style = WS_POPUP;
        constexpr DWORD exStyle = WS_EX_TOOLWINDOW;
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        _hwnd = CreateWindowExW(
            exStyle, kWindowClassName, title.data(), style,
            0, 0,
            rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, instance, this);

        if (!_hwnd)
        {
            return false;
        }

        _width = width;
        _height = height;
        return true;
    }

    void Window::Show()
    {
        // A DE surface is a desktop-level background, not a foreground
        // app: show it without stealing focus/activation, and keep it at
        // the very bottom of the z-order -- the same contract Explorer's
        // own desktop window follows.
        SetWindowPos(_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
    }

    bool Window::PumpMessages()
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return false;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        return true;
    }
}
