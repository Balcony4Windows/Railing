#pragma once
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <psapi.h> 

#pragma comment(lib, "psapi.lib")

#ifndef NIN_SELECT
#define NIN_SELECT      (WM_USER + 0)
#define NIN_KEYSELECT   (WM_USER + 1)
#define NIN_BALLOONSHOW (WM_USER + 2)
#define NIN_BALLOONHIDE (WM_USER + 3)
#define NIN_POPUPOPEN   (WM_USER + 6)
#define NIN_POPUPCLOSE  (WM_USER + 7)
#endif

struct AppBarEntry {
    HWND hwnd;
    UINT uEdge;      // ABE_LEFT, ABE_TOP, etc.
    RECT rc;         // The screen space they want
    HMONITOR hMon;   // Which monitor they are on
};

struct TrayIconData {
    HWND ownerHwnd;
    UINT uID;
    UINT uCallbackMessage;
    HICON hIcon;
    std::wstring tooltip;
    GUID guidItem;
    RECT rect = { 0 };
	UINT uVersion = 0; // 0=legacy, 3=Win2k, 4=Vista+
};

#pragma pack(push, 1)

struct NOTIFYICONDATA32 {
    DWORD cbSize;
    DWORD hWnd;
    DWORD uID;
    DWORD uFlags;
    DWORD uCallbackMessage;
    DWORD hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    DWORD uTimeout;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID  guidItem;
    DWORD hBalloonIcon;
};

struct TRAY_NOTIFY_DATA_HEADER_32 {
    DWORD dwSignature;
    DWORD dwMessage;
    NOTIFYICONDATA32 nid;
};

struct TRAY_NOTIFY_DATA_HEADER {
    DWORD dwSignature;
    DWORD dwMessage;
    NOTIFYICONDATAW nid;
};

#pragma pack(pop)

struct NOTIFYICONDATA_V3 {
    DWORD cbSize;
    HWND hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    HICON hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    union {
        UINT uTimeout;
        UINT uVersion;
    } DUMMYUNIONNAME;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
};

struct TRAY_NOTIFY_DATA_HEADER_V3 {
    DWORD dwSignature;
    DWORD dwMessage;
    NOTIFYICONDATA_V3 nid;
};

class TrayBackend {

    void DebugLog(const std::wstring &msg) {
        std::wstring output = L"[Balcony DEBUG] " + msg + L"\n";
        OutputDebugStringW(output.c_str());
    }

    void DebugLog(const std::string &msg) {
        std::string output = "[Balcony DEBUG] " + msg + "\n";
        OutputDebugStringA(output.c_str());
    }
private:
    static TrayBackend *instance;
    HWND hTrayHost = NULL;
    std::vector<TrayIconData> icons;
    std::recursive_mutex iconMutex;

    std::vector<AppBarEntry> registeredBars;
    std::recursive_mutex barMutex;

    UINT msgTaskbarCreated = 0;
    std::function<void()> onIconsChanged;

    TrayBackend() { }

    HICON GetIconFromWindow(HWND hwnd) {
        if (!IsWindow(hwnd)) return NULL;
        HICON hIcon = NULL;
        DWORD_PTR result = 0;
        if (SendMessageTimeout(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result))
            hIcon = (HICON)result;
        if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
        return hIcon;
    }

public:
    LRESULT HandleCopyData(HWND sender, COPYDATASTRUCT *cds) {
        if (cds->dwData != 1 || cds->cbData < 8) return FALSE;

        BYTE *pData = (BYTE *)cds->lpData;
        DWORD dwMessage = *((DWORD *)(pData + 4));
        DWORD cbSize = *((DWORD *)(pData + 8));

        HWND hWnd = NULL;
        UINT uID = 0;
        UINT uFlags = 0;
        UINT uCallbackMsg = 0;
        HICON hRawIcon = NULL;
        WCHAR szTipBuf[128] = { 0 };
        GUID guidItem = { 0 };
        UINT uVersion = 0;

        // --- ARCHITECTURE DETECTION ---
        bool is32Bit = (cbSize == 956 || cbSize == 936 || cbSize == 504 || cbSize == 488 || cbSize == 484);

        if (is32Bit) {
            NOTIFYICONDATA32 *p32 = (NOTIFYICONDATA32 *)(pData + 8);
            hWnd = (HWND)(UINT_PTR)p32->hWnd;
            uID = p32->uID;
            uFlags = p32->uFlags;
            uCallbackMsg = p32->uCallbackMessage;
            hRawIcon = (HICON)(UINT_PTR)p32->hIcon;
            wcsncpy_s(szTipBuf, p32->szTip, 127);

            // FIX: Extract Version from the 32-bit struct
            // The uTimeout field is a union with uVersion.
            // We only read this if the struct is large enough (offsets > 800)
            if (cbSize >= 504) {
                uVersion = p32->uTimeout;
            }

            if (cbSize >= 936) guidItem = p32->guidItem;
        }
        else {
            NOTIFYICONDATAW *pNid = (NOTIFYICONDATAW *)(pData + 8);
            hWnd = pNid->hWnd;
            uID = pNid->uID;
            uFlags = pNid->uFlags;
            uCallbackMsg = pNid->uCallbackMessage;
            hRawIcon = pNid->hIcon;
            wcsncpy_s(szTipBuf, pNid->szTip, 127);
            guidItem = pNid->guidItem;
            uVersion = pNid->uVersion;
        }

        // SANITY CHECK
        if (!hWnd && dwMessage != NIM_SETVERSION) return FALSE;

        std::lock_guard<std::recursive_mutex> lock(iconMutex);

        // --- GARBAGE COLLECTION ---
        auto deadIt = std::remove_if(icons.begin(), icons.end(), [](const TrayIconData &item) {
            return !IsWindow(item.ownerHwnd);
            });
        if (deadIt != icons.end()) {
            for (auto it = deadIt; it != icons.end(); ++it) if (it->hIcon) DestroyIcon(it->hIcon);
            icons.erase(deadIt, icons.end());
        }

        // --- MATCHING ---
        auto it = std::find_if(icons.begin(), icons.end(), [&](const TrayIconData &item) {
            return (item.ownerHwnd == hWnd && item.uID == uID);
            });

        if (dwMessage == NIM_DELETE) {
            if (it != icons.end()) {
                if (it->hIcon) DestroyIcon(it->hIcon);
                icons.erase(it);
            }
            NotifyIconsChanged();
            return TRUE;
        }

        TrayIconData *target = nullptr;
        if (it != icons.end()) target = &(*it);
        else {
            icons.push_back({});
            target = &icons.back();
            target->ownerHwnd = hWnd;
            target->uID = uID;
            // Important: New icons default to Version 0 until the app sends NIM_SETVERSION
            target->uVersion = 0;
        }

        // --- APPLY DATA ---
        if (uFlags & NIF_MESSAGE) target->uCallbackMessage = uCallbackMsg;

        std::wstring szTip = szTipBuf;
        if (!szTip.empty()) target->tooltip = szTip;

        if (uFlags & NIF_GUID) target->guidItem = guidItem;

        // FIX: Apply version if the message is setting it
        if (dwMessage == NIM_SETVERSION) {
            target->uVersion = uVersion;
        }

        // --- ICON LOGIC ---
        if (uFlags & NIF_ICON) {
            if (target->hIcon) DestroyIcon(target->hIcon);
            target->hIcon = CopyIcon(hRawIcon);
            if (!target->hIcon && IsWindow(target->ownerHwnd)) {
                target->hIcon = (HICON)SendMessage(target->ownerHwnd, WM_GETICON, ICON_SMALL, 0);
                if (!target->hIcon) target->hIcon = (HICON)SendMessage(target->ownerHwnd, WM_GETICON, ICON_BIG, 0);
                if (!target->hIcon) target->hIcon = GetIconFromWindow(target->ownerHwnd);
            }
        }

        NotifyIconsChanged();
        return TRUE;
    }

    inline static bool MultipleTrays = false;

    static TrayBackend &Get() {
        static TrayBackend instance;
        return instance;
    }

    std::vector<TrayIconData> GetIcons() {
        std::lock_guard<std::recursive_mutex> lock(iconMutex);
        return icons;
    }

    void ResetWorkAreaToMonitor(HMONITOR hMon)
    {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);

        SystemParametersInfo(SPI_SETWORKAREA, 0, &mi.rcMonitor,
            SPIF_SENDCHANGE | SPIF_UPDATEINIFILE);

        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE,
            SPI_SETWORKAREA, (LPARAM)L"WorkArea",
            SMTO_ABORTIFHUNG, 100, NULL);
    }

    void SetIconChangeCallback(std::function<void()> callback) {
        onIconsChanged = callback;
    }

    void NotifyIconsChanged() {
        if (onIconsChanged) onIconsChanged();
    }

    void SendLeftClick(const TrayIconData &icon)
    {
        SendTrayMessage(icon, WM_MOUSEMOVE, false);
        SendTrayMessage(icon, WM_LBUTTONDOWN, false);
        SendTrayMessage(icon, WM_LBUTTONUP, false);

        // Select (Vista+): Tells the app "The user focused this icon"
        SendTrayMessage(icon, NIN_SELECT, false);
    }

    void SendDoubleClick(const TrayIconData &icon)
    {
        SendTrayMessage(icon, WM_LBUTTONDBLCLK, true);
        SendTrayMessage(icon, WM_LBUTTONUP, true);
        SendTrayMessage(icon, NIN_KEYSELECT, true);
    }

    void SendTrayMessage(const TrayIconData &icon, UINT message, bool forceFocus)
    {
        if (!IsWindow(icon.ownerHwnd) || icon.uCallbackMessage == 0) return;

        WPARAM wParam;
        LPARAM lParam;
        POINT pt; GetCursorPos(&pt);

        if (icon.uVersion >= 4) {
            // VERSION 4 BEHAVIOR:
            // wParam:  Screen Coordinates (X | Y)
            // lParam:  LOWORD = Message ID (e.g. WM_CONTEXTMENU)
            //          HIWORD = Icon ID
            wParam = MAKELPARAM(pt.x, pt.y);
            lParam = MAKELPARAM(message, icon.uID);
        }
        else {
            // LEGACY BEHAVIOR:
            // wParam:  Icon ID
            // lParam:  Message ID (or Mouse Coordinates in rare ancient cases)
            wParam = icon.uID;
            if (message == WM_CONTEXTMENU) lParam = MAKELPARAM(pt.x, pt.y);
            else lParam = message;
        }

        if (forceFocus) {
            DWORD pid = 0;
            GetWindowThreadProcessId(icon.ownerHwnd, &pid);
            AllowSetForegroundWindow(pid);
            SetForegroundWindow(icon.ownerHwnd);
        }

        PostMessage(icon.ownerHwnd, icon.uCallbackMessage, wParam, lParam);
    }

    void SendRightClick(const TrayIconData &icon)
    {
        SendTrayMessage(icon, WM_RBUTTONDOWN, true);
        SendTrayMessage(icon, WM_RBUTTONUP, true);
        SendTrayMessage(icon, WM_CONTEXTMENU, true);
        // Only needed for V4, but safe to send to legacy usually.
        if (icon.uVersion >= 4)
            SendTrayMessage(icon, NIN_POPUPOPEN, true);
    }
};