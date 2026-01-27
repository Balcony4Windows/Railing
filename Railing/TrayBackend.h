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

struct TrayIconData {
    HWND ownerHwnd;
    UINT uID;
    UINT uCallbackMessage;
    HICON hIcon;
    std::wstring tooltip;
    GUID guidItem;
    RECT rect = { 0 };
};

#pragma pack(push, 1)  // Force byte alignment

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

                // FIX IS HERE: Removed SHGFI_USEFILEATTRIBUTES
                // We WANT the shell to inspect the file content to get the specific app icon.
                SHFILEINFOW sfi = { 0 };
                if (SHGetFileInfoW(path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON)) {
                    hIcon = sfi.hIcon;
                }

                // Fallback: Raw extraction
                if (!hIcon) ExtractIconExW(path, 0, NULL, &hIcon, 1);
            }
            CloseHandle(hProcess);
        }

        // Fallbacks
        if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
        if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);

        return hIcon;
    }

    void CreateHostWindow() {
        if (FindWindow(L"Shell_TrayWnd", NULL) != NULL) {
            OutputDebugString(L"[Railing] WARNING: Another Shell_TrayWnd exists!\n");
        }

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

        DWORD dwMessage = 0;
        HWND hWnd = sender;
        UINT uID = 0;
        UINT uFlags = 0;
        UINT uCallbackMsg = 0;
        HICON hRawIcon = NULL;
        std::wstring szTip;

        if (cds->cbData < sizeof(TRAY_NOTIFY_DATA_HEADER)) {
            TRAY_NOTIFY_DATA_HEADER_32 *p32 = (TRAY_NOTIFY_DATA_HEADER_32 *)cds->lpData;

            dwMessage = p32->dwMessage;
            uID = p32->nid.uID;
            uFlags = p32->nid.uFlags;
            uCallbackMsg = p32->nid.uCallbackMessage;
            hRawIcon = (HICON)(ULONG_PTR)p32->nid.hIcon;
            szTip = std::wstring(p32->nid.szTip, wcsnlen(p32->nid.szTip, 128));
        }
        else {
            BYTE *pData = (BYTE *)cds->lpData;

            dwMessage = *((DWORD *)(pData + 4));

            // Use SENDER for HWND - this is the actual window that sent the message!
            hWnd = sender;

            // Use structure cast for the rest
            TRAY_NOTIFY_DATA_HEADER *pHeader = (TRAY_NOTIFY_DATA_HEADER *)cds->lpData;
            uID = pHeader->nid.uID;
            uFlags = pHeader->nid.uFlags;
            uCallbackMsg = pHeader->nid.uCallbackMessage;
            hRawIcon = pHeader->nid.hIcon;

            // szTip at proven working offset
            WCHAR *pSzTip = (WCHAR *)(pData + 32);
            szTip = pSzTip;
        }

        std::lock_guard<std::recursive_mutex> lock(iconMutex);

        auto it = std::find_if(icons.begin(), icons.end(), [&](const TrayIconData &item) {
            return (item.ownerHwnd == hWnd && item.uID == uID);
            });

        if (dwMessage == NIM_DELETE) {
            if (it != icons.end()) {
                if (it->hIcon) DestroyIcon(it->hIcon);
                icons.erase(it);
            }
            return TRUE;
        }

        TrayIconData *target = nullptr;
        if (it != icons.end()) {
            target = &(*it);
        }
        else {
            icons.push_back({});
            target = &icons.back();
            target->ownerHwnd = hWnd;
            target->uID = uID;
        }

        if (uFlags & NIF_MESSAGE) target->uCallbackMessage = uCallbackMsg;
        if (!szTip.empty()) target->tooltip = szTip;

        if (target->tooltip.empty()) {  // Only if completely empty
            DWORD pid = 0;
            GetWindowThreadProcessId(hWnd, &pid);
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProcess) {
                WCHAR path[MAX_PATH];
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                    // Try to get FileDescription from version info first
                    DWORD dummy;
                    DWORD versionInfoSize = GetFileVersionInfoSizeW(path, &dummy);
                    if (versionInfoSize > 0) {
                        std::vector<BYTE> versionInfo(versionInfoSize);
                        if (GetFileVersionInfoW(path, 0, versionInfoSize, versionInfo.data())) {
                            WCHAR *description = nullptr;
                            UINT descLen = 0;
                            // Query FileDescription
                            if (VerQueryValueW(versionInfo.data(), L"\\StringFileInfo\\040904b0\\FileDescription",
                                (LPVOID *)&description, &descLen) && description) {
                                target->tooltip = description;

                                if (target->tooltip.find(L" notification icon") != std::wstring::npos) {
                                    target->tooltip = target->tooltip.substr(0, target->tooltip.find(L" notification icon"));
                                }

                            }
                        }
                    }

                    // Fallback to filename if version info didn't work
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

            // Best quality if it works
            if (hRawIcon) hNew = CopyIcon(hRawIcon);

            // Good for cross-process
            if (!hNew && hRawIcon) hNew = DuplicateIcon(NULL, hRawIcon);

            // 3. Robust Fallback: Get Icon from Process/Shell
            if (!hNew) {
                if (IsWindow(hWnd)) hNew = GetIconFromWindow(hWnd);
            }

            // 4. Final Fallback (System Default)
            if (!hNew && target->hIcon == NULL) target->hIcon = LoadIcon(NULL, IDI_APPLICATION);
            else if (hNew) {
                if (target->hIcon) DestroyIcon(target->hIcon);
                target->hIcon = hNew;
            }
        }
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

    void SendClick(const TrayIconData &icon, bool isRightClick) {
        if (!IsWindow(icon.ownerHwnd)) return;
        UINT mouseMsg = isRightClick ? WM_RBUTTONUP : WM_LBUTTONUP;
        UINT downMsg = isRightClick ? WM_RBUTTONDOWN : WM_LBUTTONDOWN;
        PostMessage(icon.ownerHwnd, icon.uCallbackMessage, (WPARAM)icon.uID, (LPARAM)downMsg);
        PostMessage(icon.ownerHwnd, icon.uCallbackMessage, (WPARAM)icon.uID, (LPARAM)mouseMsg);
        if (isRightClick) {
            POINT pt; GetCursorPos(&pt);
            PostMessage(icon.ownerHwnd, icon.uCallbackMessage, (WPARAM)icon.uID, (LPARAM)WM_CONTEXTMENU);
            SetForegroundWindow(icon.ownerHwnd);
            PostMessage(icon.ownerHwnd, WM_CONTEXTMENU, (WPARAM)icon.ownerHwnd, MAKELPARAM(pt.x, pt.y));
        }
    }
};