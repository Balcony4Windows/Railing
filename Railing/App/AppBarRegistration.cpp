#include "AppBarRegistration.h"
#include <shellapi.h>
#include <string>
#include <windows.h>
#include "AppBarManager.h"
#include "AppBarRegistration.h"

#ifndef WM_RAILING_APPBAR
#define WM_RAILING_APPBAR (WM_USER + 200)
#endif

static bool g_IsRegistered = false;

void RegisterAppBar(HWND hwnd) {
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;
    abd.uCallbackMessage = WM_RAILING_APPBAR;
    AppBarManager::Get().RegisterBar(&abd);

    g_IsRegistered = true;
}

void UnregisterAppBar(HWND hwnd) {
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;

    AppBarManager::Get().UnregisterBar(&abd);
    g_IsRegistered = false;
}

void UpdateAppBarPosition(HWND hwnd, const ThemeConfig &config)
{
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;
    abd.uCallbackMessage = WM_RAILING_APPBAR;

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);
    abd.rc = mi.rcMonitor;

    UINT dpi = GetDpiForWindow(hwnd);
    int thickness = MulDiv(config.global.height, dpi, 96);

    if (config.global.position == "left") {
        abd.uEdge = ABE_LEFT;   abd.rc.right = abd.rc.left + thickness;
    }
    else if (config.global.position == "right") {
        abd.uEdge = ABE_RIGHT;  abd.rc.left = abd.rc.right - thickness;
    }
    else if (config.global.position == "top") {
        abd.uEdge = ABE_TOP;    abd.rc.bottom = abd.rc.top + thickness;
    }
    else {
        abd.uEdge = ABE_BOTTOM; abd.rc.top = abd.rc.bottom - thickness;
    }

    AppBarManager::Get().SetBarPos(&abd);

    SetWindowPos(hwnd, HWND_TOPMOST,
        abd.rc.left, abd.rc.top,
        abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}