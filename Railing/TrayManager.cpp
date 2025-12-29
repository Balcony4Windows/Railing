#include "TrayManager.h"
#include <iostream>

// Undocumented struct used by Windows Tray
struct TRAYDATA {
    HWND hwnd;
    UINT uID;
    UINT uCallbackMessage;
    DWORD reserved[2];
    HICON hIcon;
};

// 64-bit compatible TBBUTTON
struct TBBUTTON_64 {
    int iBitmap;
    int idCommand;
    BYTE fsState;
    BYTE fsStyle;
    BYTE bReserved[6];
    DWORD_PTR dwData;
    INT_PTR iString;
};

TrayManager::TrayManager() {}

TrayManager::~TrayManager() {
    CleanupExplorerAccess();
}

HWND TrayManager::FindTrayToolbarWindow() {
    // 1. Find the Taskbar
    HWND hShellTrayWnd = FindWindow(L"Shell_TrayWnd", NULL);
    if (!hShellTrayWnd) return NULL;
    HWND hTrayNotifyWnd = FindWindowEx(hShellTrayWnd, NULL, L"TrayNotifyWnd", NULL);
    if (!hTrayNotifyWnd) return NULL;
    HWND hSysPager = FindWindowEx(hTrayNotifyWnd, NULL, L"SysPager", NULL);
    if (!hSysPager) return NULL;

    return FindWindowEx(hSysPager, NULL, L"ToolbarWindow32", NULL);
}

void TrayManager::InitExplorerAccess() {
    if (hExplorerProcess) return;

    HWND hTray = FindTrayToolbarWindow();
    if (!hTray) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hTray, &pid);
    hExplorerProcess = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);

    if (hExplorerProcess) {
        remoteBuffer = VirtualAllocEx(hExplorerProcess, NULL, sizeof(TBBUTTON_64), MEM_COMMIT, PAGE_READWRITE);
    }
}

void TrayManager::CleanupExplorerAccess() {
    if (remoteBuffer && hExplorerProcess) {
        VirtualFreeEx(hExplorerProcess, remoteBuffer, 0, MEM_RELEASE);
    }
    if (hExplorerProcess) {
        CloseHandle(hExplorerProcess);
        hExplorerProcess = NULL;
    }
}

std::vector<TrayIconData> TrayManager::GetTrayIcons() {
    std::vector<TrayIconData> icons;
    HWND hTray = FindTrayToolbarWindow();
    if (!hTray) return icons;
    InitExplorerAccess();
    if (!hExplorerProcess || !remoteBuffer) return icons;

    // Get button count
    int count = (int)SendMessage(hTray, TB_BUTTONCOUNT, 0, 0);

    for (int i = 0; i < count; i++) {
        TBBUTTON_64 tbButton = {};
        SIZE_T bytesRead = 0;

        SendMessage(hTray, TB_GETBUTTON, i, (LPARAM)remoteBuffer);

        ReadProcessMemory(hExplorerProcess, remoteBuffer, &tbButton, sizeof(TBBUTTON_64), &bytesRead);

        TRAYDATA trayData = {};
        ReadProcessMemory(hExplorerProcess, (LPCVOID)tbButton.dwData, &trayData, sizeof(TRAYDATA), &bytesRead);

        if (trayData.hwnd) {
            TrayIconData data;
            data.hwndOwner = trayData.hwnd;
            data.callbackId = trayData.uCallbackMessage;
            data.uID = trayData.uID;
            data.hIcon = trayData.hIcon; // NOTE: This is a handle in Explorer's process!

            data.hIcon = CopyIcon(trayData.hIcon);
            if (!(tbButton.fsState & TBSTATE_HIDDEN)) {
                icons.push_back(data);
            }
        }
    }
    return icons;
}

void TrayManager::ClickIcon(const TrayIconData &icon, bool isRightClick) {
    UINT msg = isRightClick ? WM_RBUTTONUP : WM_LBUTTONUP;

    // The standard legacy protocol:
    PostMessage(icon.hwndOwner, icon.callbackId, (WPARAM)icon.uID, msg);

    // Modern apps (uCallbackMessage usually WM_USER + X) often handle logic on UP
    if (isRightClick) {
        // Often needed to trigger the context menu
        PostMessage(icon.hwndOwner, icon.callbackId, (WPARAM)icon.uID, WM_CONTEXTMENU);
    }
}