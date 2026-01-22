#include "WindowManager.h"
#include <windows.h>
#include <string>
#include <algorithm>

std::vector<WindowInfo> WindowManager::GetTopLevelWindows() {
    std::vector<WindowInfo> windows;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        std::vector<WindowInfo> *pWindows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);
        if (!IsAppWindow(hwnd)) return TRUE;

        wchar_t buffer[256];
        GetWindowText(hwnd, buffer, 256);
        RECT rect;
        GetWindowRect(hwnd, &rect);
        pWindows->push_back({ hwnd, buffer, rect });
        return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

bool WindowManager::IsAppWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return false;

    LONG exStyle = (LONG)GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return false;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    wchar_t className[256];
    GetClassName(hwnd, className, 256);
    if (wcscmp(className, L"Shell_TrayWnd") == 0) return false;
    if (wcscmp(className, L"Progman") == 0) return false;
    if (wcscmp(className, L"ApplicationFrameWindow") == 0) return false;

    wchar_t title[256];
    GetWindowText(hwnd, title, 256);
    if (wcsstr(title, L"Settings") != nullptr) return false;
    if (wcsstr(title, L"Windows Input Experience") != nullptr) return false;

    return true;
}