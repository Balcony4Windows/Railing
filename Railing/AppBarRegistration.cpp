#include "AppBarRegistration.h"

// Register the window as an AppBar
void RegisterAppBar(HWND hwnd)
{
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;
    abd.uCallbackMessage = WM_RAILING_APPBAR;
    // ABM_NEW tells Windows "I am a new toolbar"
    SHAppBarMessage(ABM_NEW, &abd);
}

// Unregister (Critical for cleanup)
void UnregisterAppBar(HWND hwnd)
{
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
}

// Calculate position and reserve screen space
void UpdateAppBarPosition(HWND hwnd)
{
    ThemeConfig theme = ThemeLoader::Load("config.json");

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;

    // 1. Calculate Margins & Dimensions
    int thickness = (int)(theme.global.height * scale);
    int mLeft = (int)(theme.global.margin.left * scale);
    int mRight = (int)(theme.global.margin.right * scale);
    int mTop = (int)(theme.global.margin.top * scale);
    int mBottom = (int)(theme.global.margin.bottom * scale);

    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;

    if (theme.global.position == "bottom") abd.uEdge = ABE_BOTTOM;
    else if (theme.global.position == "left") abd.uEdge = ABE_LEFT;
    else if (theme.global.position == "right") abd.uEdge = ABE_RIGHT;
    else abd.uEdge = ABE_TOP;
    int reservedThickness = thickness;
    if (abd.uEdge == ABE_TOP || abd.uEdge == ABE_BOTTOM) reservedThickness += mTop + mBottom;
    else reservedThickness += mLeft + mRight;

    if (abd.uEdge == ABE_LEFT || abd.uEdge == ABE_RIGHT) {
        abd.rc.top = mi.rcMonitor.top;
        abd.rc.bottom = mi.rcMonitor.bottom;
        abd.rc.left = (abd.uEdge == ABE_LEFT) ? mi.rcMonitor.left : mi.rcMonitor.right - reservedThickness;
        abd.rc.right = abd.rc.left + reservedThickness;
    }
    else {
        abd.rc.left = mi.rcMonitor.left;
        abd.rc.right = mi.rcMonitor.right;
        abd.rc.top = (abd.uEdge == ABE_TOP) ? mi.rcMonitor.top : mi.rcMonitor.bottom - reservedThickness;
        abd.rc.bottom = abd.rc.top + reservedThickness;
    }

    SHAppBarMessage(ABM_QUERYPOS, &abd);

    if (abd.uEdge == ABE_LEFT) abd.rc.right = abd.rc.left + reservedThickness;
    else if (abd.uEdge == ABE_RIGHT) abd.rc.left = abd.rc.right - reservedThickness;
    else if (abd.uEdge == ABE_TOP) abd.rc.bottom = abd.rc.top + reservedThickness;
    else if (abd.uEdge == ABE_BOTTOM) abd.rc.top = abd.rc.bottom - reservedThickness;

    SHAppBarMessage(ABM_SETPOS, &abd);

    int finalX, finalY, finalW, finalH;

    if (abd.uEdge == ABE_TOP) {
        finalX = abd.rc.left + mLeft;
        finalY = abd.rc.top + mTop;
        finalW = (abd.rc.right - abd.rc.left) - mLeft - mRight;
        finalH = thickness; // Use strict height
    }
    else if (abd.uEdge == ABE_BOTTOM) {
        finalX = abd.rc.left + mLeft;
        finalY = abd.rc.bottom - mBottom - thickness;
        finalW = (abd.rc.right - abd.rc.left) - mLeft - mRight;
        finalH = thickness;
    }
    else if (abd.uEdge == ABE_LEFT) {
        finalX = abd.rc.left + mLeft;
        finalY = abd.rc.top + mTop;
        finalW = thickness;
        finalH = (abd.rc.bottom - abd.rc.top) - mTop - mBottom;
    }
    else { // Right
        finalX = abd.rc.right - mRight - thickness;
        finalY = abd.rc.top + mTop;
        finalW = thickness;
        finalH = (abd.rc.bottom - abd.rc.top) - mTop - mBottom;
    }

    MoveWindow(hwnd, finalX, finalY, finalW, finalH, TRUE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}