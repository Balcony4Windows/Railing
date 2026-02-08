#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <string>

class AppBarManager {
public:
    bool isUpdating = false;

    static AppBarManager &Get() {
        static AppBarManager instance;
        return instance;
    }

    void RegisterBar(APPBARDATA *abd) { HandleNew(abd); }
    void UnregisterBar(APPBARDATA *abd) { HandleRemove(abd); }
    void SetBarPos(APPBARDATA *abd) { HandleSetPos(abd); }

    LRESULT HandleAppBarMessage(WPARAM wParam, LPARAM lParam) {
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lParam;
        if (!cds || cds->cbData < sizeof(APPBARDATA)) return FALSE;
        APPBARDATA *abd = (APPBARDATA *)cds->lpData;
        DWORD msg = (DWORD)cds->dwData;

        switch (msg) {
        case ABM_NEW:            return HandleNew(abd);
        case ABM_REMOVE:         return HandleRemove(abd);
        case ABM_QUERYPOS:       return HandleQueryPos(abd);
        case ABM_SETPOS:         return HandleSetPos(abd);
        case ABM_ACTIVATE:       return TRUE;
        }
        return FALSE;
    }

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdc, LPRECT lprcMonitor, LPARAM dwData) {
        AppBarManager *self = (AppBarManager *)dwData;
        self->UpdateWorkAreaForMonitor(hMon);
        return TRUE;
    }

    void RecalculateWorkArea() {
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)this);
    }

    void UpdateWorkAreaForMonitor(HMONITOR hMon) {
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (!GetMonitorInfo(hMon, &mi)) return;

        RECT oldWork = mi.rcWork;
        RECT newWork = mi.rcMonitor;

        // 1. Calculate desired Work Area
        for (const auto &bar : bars) {
            RECT intersection;
            if (IntersectRect(&intersection, &mi.rcMonitor, &bar.rc)) {
                int width = bar.rc.right - bar.rc.left;
                int height = bar.rc.bottom - bar.rc.top;

                if (bar.uEdge == ABE_LEFT) newWork.left += width;
                else if (bar.uEdge == ABE_RIGHT) newWork.right -= width;
                else if (bar.uEdge == ABE_TOP) newWork.top += height;
                else if (bar.uEdge == ABE_BOTTOM) newWork.bottom -= height;
            }
        }

        // 2. Apply System Setting
        isUpdating = true;
        bool sizeChanged = !EqualRect(&oldWork, &newWork);

        if (sizeChanged) {
            SystemParametersInfo(SPI_SETWORKAREA, 0, &newWork,
                SPIF_SENDCHANGE | SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);

            PostMessage(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, (LPARAM)L"WorkArea");
        }

        // 3. FORCE NUDGE
        // This ensures windows snap to the new edges even if the OS thinks they are fine.
        NudgeParams params;
        params.hMon = hMon;
        params.newWorkArea = newWork;
        EnumWindows(NudgeWindowCallback, (LPARAM)&params);

        isUpdating = false;
    }

private:
    struct NudgeParams {
        HMONITOR hMon;
        RECT newWorkArea;
    };
    std::vector<APPBARDATA> bars;

    void RemoveBarInternal(HWND hwnd) {
        auto it = std::remove_if(bars.begin(), bars.end(),
            [hwnd](const APPBARDATA &b) { return b.hWnd == hwnd; });
        if (it != bars.end()) bars.erase(it, bars.end());
    }

    static BOOL CALLBACK NudgeWindowCallback(HWND hwnd, LPARAM lParam) {
        NudgeParams *params = (NudgeParams *)lParam;

        if (!IsWindowVisible(hwnd)) return TRUE;

        HMONITOR hWinMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
        if (hWinMon != params->hMon) return TRUE;

        // Skip our own bars and system windows
        wchar_t className[256];
        GetClassName(hwnd, className, 256);
        if (wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"Progman") == 0 ||
            wcscmp(className, L"WorkerW") == 0) return TRUE;

        // --- CASE A: TRULY MAXIMIZED WINDOWS ---
        if (IsZoomed(hwnd)) {
            WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
            GetWindowPlacement(hwnd, &wp);

            // Brief toggle triggers the OS to fetch the new Work Area
            wp.showCmd = SW_SHOWNORMAL;
            SetWindowPlacement(hwnd, &wp);

            wp.showCmd = SW_SHOWMAXIMIZED;
            SetWindowPlacement(hwnd, &wp);
        }
        // --- CASE B: RESTORED / SNAPPED / GAME WINDOWS ---
        else {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            RECT wa = params->newWorkArea;

            bool modified = false;

            // Clamp Bottom
            if (rc.bottom > wa.bottom) { rc.bottom = wa.bottom; modified = true; }
            // Clamp Top
            if (rc.top < wa.top) { rc.top = wa.top; modified = true; }
            // Clamp Left
            if (rc.left < wa.left) { rc.left = wa.left; modified = true; }
            // Clamp Right
            if (rc.right > wa.right) { rc.right = wa.right; modified = true; }

            if (modified) {
                // Resize the window to fit the new bounds
                SetWindowPos(hwnd, NULL, rc.left, rc.top,
                    rc.right - rc.left, rc.bottom - rc.top,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }
        return TRUE;
    }

    LRESULT HandleNew(APPBARDATA *abd) {
        RemoveBarInternal(abd->hWnd);
        bars.push_back(*abd);
        return TRUE;
    }

    LRESULT HandleRemove(APPBARDATA *abd) {
        RemoveBarInternal(abd->hWnd);
        RecalculateWorkArea();
        return TRUE;
    }

    LRESULT HandleQueryPos(APPBARDATA *abd) { return TRUE; }

    LRESULT HandleSetPos(APPBARDATA *abd) {
        bool changed = false;
        bool found = false;
        for (auto &b : bars) {
            if (b.hWnd == abd->hWnd) {
                found = true;
                if (!EqualRect(&b.rc, &abd->rc) || b.uEdge != abd->uEdge) {
                    b.rc = abd->rc;
                    b.uEdge = abd->uEdge;
                    changed = true;
                }
                break;
            }
        }
        if (!found) {
            bars.push_back(*abd);
            changed = true;
        }
        if (changed) RecalculateWorkArea();
        return TRUE;
    }
};