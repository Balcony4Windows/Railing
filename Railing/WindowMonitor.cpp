#include "WindowMonitor.h"
#include <dwmapi.h>
#include <Psapi.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

void WindowMonitor::GetTopLevelWindows(std::vector<WindowInfo> &outWindows, const std::vector<std::wstring> &pinnedApps, HWND ignoreBar)
{
    struct EnumData { std::vector<WindowInfo> *list; HWND bar; };

    std::vector<WindowInfo> running;
    EnumData data = { &running, ignoreBar };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *ctx = (EnumData *)lParam;
        if (WindowMonitor::IsAppWindow(hwnd, ctx->bar)) {
            wchar_t title[256];
            GetWindowText(hwnd, title, 256);
            std::wstring exe = WindowMonitor::GetWindowExePath(hwnd);

            RECT r; GetWindowRect(hwnd, &r);
            ctx->list->push_back({ hwnd, title, r, exe, false });
        }
        return TRUE;
        }, (LPARAM)&data);

    std::vector<WindowInfo> final; // Merge pinned
    for (const auto &pin : pinnedApps) {
        auto it = std::find_if(running.begin(), running.end(), [&](const WindowInfo &w) { return w.exePath == pin; });
        if (it != running.end()) {
            it->isPinned = true;
            final.push_back(*it);
            running.erase(it);
        }
        else final.push_back({ NULL, L"", {0}, pin, true });
    }
    final.insert(final.end(), running.begin(), running.end());
    outWindows = final;
}

BOOL WindowMonitor::IsAppWindow(HWND hwnd, HWND barWindow)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return FALSE;
    if (hwnd == barWindow) return FALSE;

        int cloakedVal = 0;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloakedVal, sizeof(cloakedVal));
        if (cloakedVal != 0) return FALSE;

        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_APPWINDOW) return TRUE;
        if (exStyle & WS_EX_TOOLWINDOW) return FALSE;
        if (GetWindow(hwnd, GW_OWNER) != NULL) return FALSE;

        RECT rect; GetWindowRect(hwnd, &rect);
        if ((rect.right - rect.left) < 20 || (rect.bottom - rect.top) < 20) return FALSE;

        char className[256];
        GetClassNameA(hwnd, className, 256);
        if (strcmp(className, "Progman") == 0) return FALSE; // Desktop
        if (strcmp(className, "Shell_TrayWnd") == 0) return FALSE; // Native Taskbar

        return TRUE;
}

std::wstring WindowMonitor::GetWindowExePath(HWND hwnd)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess) {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameExW(hProcess, NULL, path, MAX_PATH)) {
            CloseHandle(hProcess);
            return std::wstring(path);
        }
        CloseHandle(hProcess);
    }
    return L"";
}
