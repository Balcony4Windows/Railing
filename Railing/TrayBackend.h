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

struct TrayIconData {
    HWND ownerHwnd;
    UINT uID;
    UINT uCallbackMessage;
    HICON hIcon;
    std::wstring tooltip;
    GUID guidItem;
    RECT rect = { 0 };
    UINT uVersion = 0;
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
private:
    static TrayBackend *instance;
    HWND hTrayHost = NULL;
    std::vector<TrayIconData> icons;
    std::recursive_mutex iconMutex;
    UINT msgTaskbarCreated = 0;
    std::function<void()> onIconsChanged;

    TrayBackend() {
        msgTaskbarCreated = RegisterWindowMessage(L"TaskbarCreated");
        CreateHostWindow();
    }

    HICON GetIconFromWindow(HWND hwnd) {
        if (!IsWindow(hwnd)) return NULL;

        HICON hIcon = NULL;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            WCHAR path[MAX_PATH];
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {

                // 1. Try SHGetFileInfo with LARGEICON (usually 32x32)
                SHFILEINFOW sfi = { 0 };
                if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
                    hIcon = sfi.hIcon;
                }
                if (!hIcon) ExtractIconExW(path, 0, &hIcon, NULL, 1);
            }
            CloseHandle(hProcess);
        }

        if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
        if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
        if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);

        return hIcon;
    }

    void CreateHostWindow() {
        if (FindWindow(L"Shell_TrayWnd", NULL) != NULL)
            OutputDebugString(L"[Railing] WARNING: Another Shell_TrayWnd exists!\n");

        WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
        wc.lpfnWndProc = HostProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"Shell_TrayWnd";
        wc.style = CS_DBLCLKS;
        RegisterClassEx(&wc);

        hTrayHost = CreateWindowEx(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            L"Shell_TrayWnd", L"", WS_POPUP, 0, 0, 0, 0,
            NULL, NULL, wc.hInstance, this
        );

        if (hTrayHost) {
            CHANGEFILTERSTRUCT cfs = { sizeof(CHANGEFILTERSTRUCT) };
            ChangeWindowMessageFilterEx(hTrayHost, WM_COPYDATA, MSGFLT_ALLOW, &cfs);
            ChangeWindowMessageFilterEx(hTrayHost, msgTaskbarCreated, MSGFLT_ALLOW, &cfs);
            SendNotifyMessage(HWND_BROADCAST, msgTaskbarCreated, 0, 0);
        }
    }

    static LRESULT CALLBACK HostProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        TrayBackend *self = (TrayBackend *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (self && uMsg == WM_COPYDATA) {
            return self->HandleCopyData((HWND)wParam, (COPYDATASTRUCT *)lParam);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }


    LRESULT HandleCopyData(HWND sender, COPYDATASTRUCT *cds) {
        if (cds->dwData != 1) return FALSE;

        TRAY_NOTIFY_DATA_HEADER_32 *pData = (TRAY_NOTIFY_DATA_HEADER_32 *)cds->lpData;
        if (pData->dwSignature != 0x34753423) return FALSE;

        DWORD dwMessage = pData->dwMessage;
        HWND hWnd = (HWND)(ULONG_PTR)pData->nid.hWnd;
        UINT uID = pData->nid.uID;
        UINT uFlags = pData->nid.uFlags;
        UINT uCallbackMsg = pData->nid.uCallbackMessage; // Offset 16 - CORRECT
        HICON hRawIcon = (HICON)(ULONG_PTR)pData->nid.hIcon;

        std::wstring szTip = std::wstring(pData->nid.szTip, wcsnlen(pData->nid.szTip, 128));
        GUID parsedGuid = pData->nid.guidItem;
        bool hasGuid = (uFlags & NIF_GUID) && !IsEqualGUID(parsedGuid, GUID_NULL);

        // --- LOGGING TO CONFIRM ---
        // You should now see valid messages like 0x400, 0x404, etc.
        /*
        wchar_t dbg[256];
        swprintf(dbg, 256, L"[Tray] FIXED -> ID:%u | Msg:0x%X | Tip: %.10s\n",
            uID, uCallbackMsg, szTip.c_str());
        OutputDebugString(dbg);
        */

        std::lock_guard<std::recursive_mutex> lock(iconMutex);

        auto it = std::find_if(icons.begin(), icons.end(), [&](const TrayIconData &item) {
            if (hasGuid && !IsEqualGUID(item.guidItem, GUID_NULL)) {
                return IsEqualGUID(item.guidItem, parsedGuid);
            }
            return (item.ownerHwnd == hWnd && item.uID == uID ? TRUE : FALSE);
            });

        if (dwMessage == NIM_DELETE) {
            if (it != icons.end()) {
                if (it->hIcon) DestroyIcon(it->hIcon);
                icons.erase(it);
            }
            NotifyIconsChanged();
            return TRUE;
        }

        if (dwMessage == NIM_SETVERSION) {
            if (it != icons.end()) it->uVersion = pData->nid.uTimeout;
            return TRUE;
        }

        TrayIconData *target = nullptr;
        if (it != icons.end()) {
            target = &(*it);
        }
        else {
			if (dwMessage == NIM_MODIFY) return FALSE; // Can't modify non-existing icon
            icons.push_back({});
            target = &icons.back();
            target->ownerHwnd = hWnd;
            target->uID = uID;
        }
        if (hasGuid) target->guidItem = parsedGuid;

        if ((uFlags & NIF_MESSAGE) || (target->uCallbackMessage == 0 && uCallbackMsg != 0)) {
            target->uCallbackMessage = uCallbackMsg;
        }

        if (!szTip.empty()) target->tooltip = szTip;
        if (target->tooltip.empty()) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hWnd, &pid);
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProcess) {
                WCHAR path[MAX_PATH];
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                    DWORD dummy;
                    DWORD versionInfoSize = GetFileVersionInfoSizeW(path, &dummy);
                    if (versionInfoSize > 0) {
                        std::vector<BYTE> versionInfo(versionInfoSize);
                        if (GetFileVersionInfoW(path, 0, versionInfoSize, versionInfo.data())) {
                            WCHAR *description = nullptr;
                            UINT descLen = 0;
                            if (VerQueryValueW(versionInfo.data(), L"\\StringFileInfo\\040904b0\\FileDescription",
                                (LPVOID *)&description, &descLen) && description) {
                                target->tooltip = description;
                                if (target->tooltip.find(L" notification icon") != std::wstring::npos) {
                                    target->tooltip = target->tooltip.substr(0, target->tooltip.find(L" notification icon"));
                                }
                            }
                        }
                    }
                    if (target->tooltip.empty()) {
                        WCHAR *filename = wcsrchr(path, L'\\');
                        if (filename) {
                            filename++;
                            std::wstring appName = filename;
                            size_t dotPos = appName.find_last_of(L'.');
                            if (dotPos != std::wstring::npos) {
                                appName = appName.substr(0, dotPos);
                            }
                            target->tooltip = appName;
                        }
                    }
                }
                CloseHandle(hProcess);
            }
        }

        if ((uFlags & NIF_ICON) || target->hIcon == NULL) {
            HICON hNew = NULL;
            if (hRawIcon) hNew = CopyIcon(hRawIcon);
            if (!hNew && hRawIcon) hNew = DuplicateIcon(NULL, hRawIcon);
            if (!hNew) {
                if (IsWindow(hWnd)) hNew = GetIconFromWindow(hWnd);
            }
            if (!hNew && target->hIcon == NULL) target->hIcon = LoadIcon(NULL, IDI_APPLICATION);
            else if (hNew) {
                if (target->hIcon) DestroyIcon(target->hIcon);
                target->hIcon = hNew;
            }
        }
        NotifyIconsChanged();
        return TRUE;
    }

public:
    static TrayBackend &Get() {
        static TrayBackend instance;
        return instance;
    }

    std::vector<TrayIconData> GetIcons() {
        std::lock_guard<std::recursive_mutex> lock(iconMutex);
        return icons;
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
            wParam = MAKEWPARAM(pt.x, pt.y);
            lParam = MAKELPARAM(message, (WORD)icon.uID);
        }
        else {
            // LEGACY BEHAVIOR:
            // wParam:  Icon ID
            // lParam:  Message ID (or Mouse Coordinates in rare ancient cases)
            wParam = icon.uID;
            lParam = message;
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