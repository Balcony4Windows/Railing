#include "AppBarRegistration.h"
#include "Railing.h"
#include <string>
#include <sstream>

void RegisterAppBar(HWND hwnd) {
    // We no longer register the visual HWND directly. 
    // The logic is moved to UpdateAppBarPosition to target the Backend window.
}

void UnregisterAppBar(HWND hwndVisual) // Pass the visual HWND to find the monitor
{
    // 1. Unregister the Backend
    HWND hBackend = FindWindow(L"Shell_TrayWnd", NULL);
    if (hBackend) {
        APPBARDATA abd = { sizeof(abd) };
        abd.hWnd = hBackend;
        SHAppBarMessage(ABM_REMOVE, &abd);
    }

    // 2. FORCE RESTORE WORK AREA (The Fix)
    // We calculate the full monitor size and tell Windows "This is the new work area."
    HMONITOR hMon = MonitorFromWindow(hwndVisual, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    mi.cbSize = sizeof(mi);

    if (GetMonitorInfo(hMon, &mi)) {
        RECT fullScreen = mi.rcMonitor;

        // Reset the system work area to the full screen bounds
        SystemParametersInfo(SPI_SETWORKAREA, 0, &fullScreen, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);

        // Broadcast the good news so windows expand
        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0, SMTO_ABORTIFHUNG, 100, NULL);
        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 100, NULL);
    }

    OutputDebugStringW(L"[AppBar] Unregistered & WorkArea Restored.\n");
}

void UpdateAppBarPosition(HWND hwndVisual, ThemeConfig &theme)
{
    static bool isUpdating = false;
    if (isUpdating) return;
    isUpdating = true;

    // 1. Find the REAL authority (The Backend Window)
    HWND hBackend = FindWindow(L"Shell_TrayWnd", NULL);

    // If backend isn't ready yet, we can't reserve space properly. 
    // Just move the visual window and bail.
    if (!hBackend) {
        OutputDebugStringW(L"[AppBar] Critical: Shell_TrayWnd backend not found yet.\n");
        isUpdating = false;
        return;
    }

    // 2. Get Monitor & Scale
    HMONITOR hMon = MonitorFromWindow(hwndVisual, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    float dpi = (float)GetDpiForWindow(hwndVisual);
    float scale = dpi / 96.0f;
    int thickness = (int)(theme.global.height * scale);
    int mLeft = (int)(theme.global.margin.left * scale);
    int mRight = (int)(theme.global.margin.right * scale);
    int mTop = (int)(theme.global.margin.top * scale);
    int mBottom = (int)(theme.global.margin.bottom * scale);

    // 3. Register the BACKEND as the AppBar
    // Windows respects 'Shell_TrayWnd' implicitly.
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hBackend;
    abd.uCallbackMessage = WM_RAILING_APPBAR;

    // Ensure it is registered (Safe to call multiple times, fails silently if exists)
    SHAppBarMessage(ABM_NEW, &abd);

    // 4. Calculate Reservation (Edge)
    if (theme.global.position == "bottom") abd.uEdge = ABE_BOTTOM;
    else if (theme.global.position == "left") abd.uEdge = ABE_LEFT;
    else if (theme.global.position == "right") abd.uEdge = ABE_RIGHT;
    else abd.uEdge = ABE_TOP;

    // Total reserved thickness includes margins
    int reservedThickness = thickness;
    if (abd.uEdge == ABE_TOP || abd.uEdge == ABE_BOTTOM) reservedThickness += mTop + mBottom;
    else reservedThickness += mLeft + mRight;

    // 5. Query & Set (Using the Backend HWND)
    abd.rc = mi.rcMonitor;
    SHAppBarMessage(ABM_QUERYPOS, &abd);

    switch (abd.uEdge) {
    case ABE_LEFT:   abd.rc.right = abd.rc.left + reservedThickness; break;
    case ABE_RIGHT:  abd.rc.left = abd.rc.right - reservedThickness; break;
    case ABE_TOP:    abd.rc.bottom = abd.rc.top + reservedThickness; break;
    case ABE_BOTTOM: abd.rc.top = abd.rc.bottom - reservedThickness; break;
    }

    // Apply Reservation
    if (theme.global.autoHide) {
        APPBARDATA hidden = abd;
        hidden.rc = { 0,0,0,0 };
        SHAppBarMessage(ABM_SETPOS, &hidden);
    }
    else {
        SHAppBarMessage(ABM_SETPOS, &abd);
    }

    // 6. Resize the Backend Window to MATCH the reservation
    // This is the magic step. The "Tray Window" must visibly (to the OS) occupy the space.
    if (!theme.global.autoHide) {
        SetWindowPos(hBackend, NULL,
            abd.rc.left, abd.rc.top,
            abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    // 7. Move the VISUAL Window (RailingBar)
    // It floats over the area we just reserved using the backend.
    int finalX, finalY, finalW, finalH;

    if (abd.uEdge == ABE_TOP) {
        finalX = mi.rcMonitor.left + mLeft;
        finalY = mi.rcMonitor.top + mTop;
        finalW = (mi.rcMonitor.right - mi.rcMonitor.left) - mLeft - mRight;
        finalH = thickness;
    }
    else if (abd.uEdge == ABE_BOTTOM) {
        finalX = mi.rcMonitor.left + mLeft;
        finalY = mi.rcMonitor.bottom - mBottom - thickness;
        finalW = (mi.rcMonitor.right - mi.rcMonitor.left) - mLeft - mRight;
        finalH = thickness;
    }
    else if (abd.uEdge == ABE_LEFT) {
        finalX = mi.rcMonitor.left + mLeft;
        finalY = mi.rcMonitor.top + mTop;
        finalW = thickness;
        finalH = (mi.rcMonitor.bottom - mi.rcMonitor.top) - mTop - mBottom;
    }
    else {
        finalX = mi.rcMonitor.right - mRight - thickness;
        finalY = mi.rcMonitor.top + mTop;
        finalW = thickness;
        finalH = (mi.rcMonitor.bottom - mi.rcMonitor.top) - mTop - mBottom;
    }

    MoveWindow(hwndVisual, finalX, finalY, finalW, finalH, TRUE);

    // Visual bar stays on top of everything (including the backend window)
    SetWindowPos(hwndVisual, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // 8. Force Broadcast
    // Tell Windows to re-read the environment (Work Area)
    SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0, SMTO_ABORTIFHUNG, 100, NULL);

    isUpdating = false;
}