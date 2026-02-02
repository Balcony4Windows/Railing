#include "AppBarRegistration.h"
#include <shellapi.h>
#include <stdio.h> 
#include <string>
#include <windows.h>
#include <ShlObj_core.h>

// Ensure message ID is defined
#ifndef WM_RAILING_APPBAR
#define WM_RAILING_APPBAR (WM_USER + 20)
#endif

// Global flag to track if we are allowed to use Shell API
static bool g_IsRegistered = false;

struct NudgeParams {
    HWND hBar;
    HMONITOR hBarMonitor;
};

// --- HELPER: BRUTE FORCE WORK AREA ---
// Used when ABM fails (Admin mode mismatch or Explorer dead)
void ForceWorkArea(HWND hwnd, int thickness, const std::string &position) {
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    // 1. Start with the full monitor dimensions
    RECT newWorkArea = mi.rcMonitor;

    // 2. Subtract our bar's space
    if (position == "left") newWorkArea.left += thickness;
    else if (position == "right") newWorkArea.right -= thickness;
    else if (position == "top") newWorkArea.top += thickness;
    else newWorkArea.bottom -= thickness;

    // 3. Force the Kernel to update the Work Area
    // This bypasses the Shell "negotiation" and just sets the rule.
    BOOL result = SystemParametersInfo(SPI_SETWORKAREA, 0, &newWorkArea, SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);

    if (result) {
        OutputDebugString(L"[Railing] Fallback: SPI_SETWORKAREA Enforced successfully.\n");
    }
    else {
        OutputDebugString(L"[Railing] Fallback: SPI_SETWORKAREA Failed.\n");
    }

    // 4. Manually Broadcast the change so apps (Chrome/Discord) know to resize
    SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA,
        (LPARAM)L"WorkArea", SMTO_ABORTIFHUNG, 100, NULL);
}

// --- CALLBACK FOR NUDGING WINDOWS ---
BOOL CALLBACK NudgeMaximizedWindows(HWND hwnd, LPARAM lParam) {
    NudgeParams *params = (NudgeParams *)lParam;
    if (hwnd == params->hBar || !IsWindowVisible(hwnd)) return TRUE;

    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (GetWindowPlacement(hwnd, &wp) && wp.showCmd == SW_SHOWMAXIMIZED) {
        HMONITOR hWndMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
        if (hWndMonitor == params->hBarMonitor) {
            PostMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
            PostMessage(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        }
    }
    return TRUE;
}

// Add to includes:
#include <strsafe.h> // For StringCchPrintf

void RegisterAppBar(HWND hwnd) {
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;
    abd.uCallbackMessage = WM_RAILING_APPBAR;

    SHAppBarMessage(ABM_REMOVE, &abd);

    BOOL result = (BOOL)SHAppBarMessage(ABM_NEW, &abd);

    if (result) {
        g_IsRegistered = true;
        OutputDebugString(L"[Railing] SUCCESS: ABM_NEW accepted.\n");
    }
    else {
        g_IsRegistered = false;
    }
}

void UnregisterAppBar(HWND hwnd) {
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
    g_IsRegistered = false;
}

void UpdateAppBarPosition(HWND hwnd, ThemeConfig &config)
{
    // 1. Setup Data
    APPBARDATA abd = { sizeof(abd) };
    abd.hWnd = hwnd;
    abd.uCallbackMessage = WM_RAILING_APPBAR;

    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);
    abd.rc = mi.rcMonitor;

    float dpi = (float)GetDpiForWindow(hwnd);
    int thickness = (int)(config.global.height * (dpi / 96.0f));

    if (config.global.position == "left") {
        abd.uEdge = ABE_LEFT; abd.rc.right = abd.rc.left + thickness;
    }
    else if (config.global.position == "right") {
        abd.uEdge = ABE_RIGHT; abd.rc.left = abd.rc.right - thickness;
    }
    else if (config.global.position == "top") {
        abd.uEdge = ABE_TOP; abd.rc.bottom = abd.rc.top + thickness;
    }
    else {
        abd.uEdge = ABE_BOTTOM; abd.rc.top = abd.rc.bottom - thickness;
    }

    // 2. Register with Backend (For Z-Order/Notifications)
    if (!config.global.autoHide) {
        SHAppBarMessage(ABM_SETPOS, &abd);
        SHAppBarMessage(ABM_WINDOWPOSCHANGED, &abd);
    }

    // --- FIX: ACTUALLY CALL THE FALLBACK ---
    // This is the line that was missing. It forces the screen space reserved.
    if (!config.global.autoHide) {
        ForceWorkArea(hwnd, thickness, config.global.position);
    }
    // ---------------------------------------

    // 3. Move Window
    SetWindowPos(hwnd, HWND_TOPMOST,
        abd.rc.left, abd.rc.top,
        abd.rc.right - abd.rc.left, abd.rc.bottom - abd.rc.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

    if (!config.global.autoHide) {
        NudgeParams params = { hwnd, hMon };
        EnumWindows(NudgeMaximizedWindows, (LPARAM)&params);
    }
}