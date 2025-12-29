#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <commctrl.h>

struct TrayIconData {
    HWND hwndOwner; // The hidden window processing the events
    UINT callbackId; // The ID used for messages
    UINT uID; // The internal icon ID
    HICON hIcon; // The visual icon
    std::wstring tooltip;
    RECT rect; // For hit testing
};

class TrayManager {
public:
    TrayManager();
    ~TrayManager();

    // Scans the system tray and returns current icons
    std::vector<TrayIconData> GetTrayIcons();

    // Sends the fake clicks to the original apps
    void ClickIcon(const TrayIconData &icon, bool isRightClick);

private:
    HWND FindTrayToolbarWindow();
    bool IsWindows11();

    // Memory helpers
    HANDLE hExplorerProcess = NULL;
    LPVOID remoteBuffer = NULL;

    void InitExplorerAccess();
    void CleanupExplorerAccess();
};