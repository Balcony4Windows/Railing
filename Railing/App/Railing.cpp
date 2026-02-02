#include <WinSock2.h> // Must be first
#include "Railing.h"
#include "InputManager.h"
#include "WindowMonitor.h"
#include "ThemeLoader.h"
#include "ModulesConcrete.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <cmath>
#include "TrayBackend.h"
#include "TrayFlyout.h"

#pragma comment(lib, "dwmapi.lib") 
#pragma comment(lib, "d2d1")
#pragma comment(lib, "shell32.lib")

Railing *Railing::instance = nullptr;
UINT WM_SHELLHOOKMESSAGE = RegisterWindowMessageW(L"SHELLHOOK");
UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

Railing::Railing()
    : renderer(nullptr), networkFlyout(nullptr), trayFlyout(nullptr),
    lastCpuUpdate(0), lastRamUpdate(0), lastGpuUpdate(0),
    cachedCpuUsage(0), cachedRamUsage(0), cachedGpuTemp(0),
    cachedVolume(0.0f), cachedMute(false),
    showProgress(0.0f), isHidden(false), lastInteractionTime(0),
    needsWindowRefresh(false), isTrackingMouse(false)
{
}

Railing::~Railing() {
    if (pDropTarget) {
        RevokeDragDrop(hwndBar);
        pDropTarget->Release();
    }
    OleUninitialize();
    if (renderer) delete renderer;
}

void Railing::CheckForConfigUpdate()
{
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    path = path.substr(0, path.find_last_of(L"\\/"));
    std::wstring fullPath = path + L"\\config.json";

    if (GetFileAttributesExW(fullPath.c_str(), GetFileExInfoStandard, &fileInfo)) {
        if (lastConfigWriteTime.dwLowDateTime == 0 && lastConfigWriteTime.dwHighDateTime == 0) {
            lastConfigWriteTime = fileInfo.ftLastWriteTime;
            return;
        }
        if (CompareFileTime(&lastConfigWriteTime, &fileInfo.ftLastWriteTime) != 0) {
            lastConfigWriteTime = fileInfo.ftLastWriteTime;
            cachedConfig = ThemeLoader::Load("config.json");

            if (renderer) {
                renderer->Reload();
                renderer->Resize();
            }

            UnregisterAppBar(hwndBar);
            RegisterAppBar(hwndBar);
            UpdateAppBarPosition(hwndBar, cachedConfig);
            InvalidateRect(hwndBar, NULL, FALSE);
        }
    }
}

bool Railing::Initialize(HINSTANCE hInstance)
{
    SetThreadDescription(GetCurrentThread(), L"Railing_MainUI");
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return false;
    TrayBackend::Get();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    instance = this;
    hInst = hInstance;

    cachedConfig = ThemeLoader::Load("config.json");
    this->pinnedApps = cachedConfig.pinnedPaths;

    if (Module::HasType(cachedConfig, "gpu")) gpuStats.Initialize();
    if (Module::HasType(cachedConfig, "visualizer")) visualizerBackend.Start();

    stats.GetCpuUsage();
    stats.GetRamUsage();

    hwndBar = CreateBarWindow(hInstance, cachedConfig);
    if (!hwndBar) return false;

    ChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
	ChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);
	ChangeWindowMessageFilter(0x0049, MSGFLT_ADD); // COPYGLOBALDATA
	ChangeWindowMessageFilter(WM_RAILING_APPBAR, MSGFLT_ADD);
    ChangeWindowMessageFilter(WM_TASKBARCREATED, MSGFLT_ADD);
	SendNotifyMessage(HWND_BROADCAST, WM_TASKBARCREATED, 0, 0);

    RegisterAppBar(hwndBar);
    UpdateAppBarPosition(hwndBar, cachedConfig);

    // Init Renderer
    if (!renderer) {
        renderer = new RailingRenderer(hwndBar, cachedConfig);
        renderer->pWorkspaceManager = &workspaces;
        renderer->Resize();
    }

    // Init Managers
    tooltips.Initialize(hwndBar);
    inputManager = std::make_unique<InputManager>(this, renderer, &tooltips);

    WindowMonitor::GetTopLevelWindows(allWindows, pinnedApps, hwndBar);

    for (const auto &win : allWindows) workspaces.AddWindow(win.hwnd);
    for (int i = 0; i < 5; i++) RegisterHotKey(hwndBar, 100 + i, MOD_ALT | MOD_NOREPEAT, 0x31 + i);

    ShowWindow(hwndBar, SW_SHOWNOACTIVATE);
    UpdateWindow(hwndBar);

    // Animation / Show
    RECT targetRect; GetWindowRect(hwndBar, &targetRect);
    int finalW = targetRect.right - targetRect.left;
    int finalH = targetRect.bottom - targetRect.top;
    int centerX = targetRect.left + (finalW / 2);
    int centerY = targetRect.top + (finalH / 2);

    SendMessage(hwndBar, WM_PAINT, 0, 0); // Force paint to cache module positions

    auto &anim = cachedConfig.global.animation;
    if (anim.enabled && anim.duration > 0) {
        int frameDelay = 1000 / anim.fps;
        int totalSteps = anim.duration / frameDelay;
        if (totalSteps < 1) totalSteps = 1;

        for (int i = 1; i <= totalSteps; i++) {
            float progress = (float)i / totalSteps;
            float ease = 1.0f - pow(1.0f - progress, 3.0f);
            float currentScale = anim.startScale + ((1.0f - anim.startScale) * ease);

            int cW = (int)(finalW * currentScale);
            int cH = (int)(finalH * currentScale);
            if (cW < 10) cW = 10; if (cH < 2) cH = 2;
            int cX = centerX - (cW / 2);
            int cY = centerY - (cH / 2);

            SetWindowPos(hwndBar, HWND_TOPMOST, cX, cY, cW, cH, SWP_NOACTIVATE | SWP_NOZORDER);
            UpdateWindow(hwndBar);
            Sleep(frameDelay);
        }
    }

    if (renderer) renderer->Resize();
    ShowWindow(hwndBar, SW_SHOW);
    UpdateWindow(hwndBar);

    RegisterShellHookWindow(hwndBar);
    shellMsgId = RegisterWindowMessage(L"SHELLHOOK");
    RegisterHotKey(hwndBar, HOTKEY_KILL_THIS, MOD_CONTROL | MOD_SHIFT, 0x51);

    CheckForConfigUpdate();
    networkBackend.GetCurrentStatus(cachedWifiState, cachedWifiSignal);

    titleHook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
        nullptr, Railing::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    focusHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
		nullptr, Railing::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    windowLifecycleHook = SetWinEventHook(
        EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE,
        nullptr, Railing::WinEventProc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    SetTimer(hwndBar, 1, 1000, NULL); // Stats (1s is optimal for CPU calculation)
    SetTimer(hwndBar, 2, 16, NULL); // Animation

    if (Module::HasType(cachedConfig, "audio")) {
        flyout = new VolumeFlyout(hInstance, renderer->GetFactory(), renderer->GetWriteFactory(), renderer->GetTextFormat(), renderer->theme);
        flyout->audio.EnsureInitialized(hwndBar);
    }
    if (Module::HasType(cachedConfig, "tray")) {
        trayFlyout = new TrayFlyout(hInstance, renderer->GetFactory(), renderer->GetWICFactory(), inputManager->tooltips, renderer->theme);
    }
    if (Module::HasType(cachedConfig, "dock")) {
        HRESULT hr = OleInitialize(NULL);
        if (FAILED(hr)) {
            OutputDebugString(L"Failed to initialize OLE.");
        }
        pDropTarget = new DropTarget([this](const std::wstring &path) {
            Module *m = renderer->GetModule("dock");
            if (m) {
                std::wstring name = L"";
                std::wstring realPath = path;
                std::wstring iconPath = L"";
                int iconIndex = 0;

                // 1. Check if Shortcut
                if (path.length() > 4 && path.substr(path.length() - 4) == L".lnk") {

                    // Capture Name
                    size_t lastSlash = path.find_last_of(L"\\/");
                    std::wstring filename = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
                    size_t lastDot = filename.find_last_of(L".");
                    if (lastDot != std::string::npos) name = filename.substr(0, lastDot);

                    // COM Magic to read Shortcut
                    IShellLink *psl;
                    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID *)&psl))) {
                        IPersistFile *ppf;
                        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (LPVOID *)&ppf))) {
                            if (SUCCEEDED(ppf->Load(path.c_str(), STGM_READ))) {
                                wchar_t target[MAX_PATH];
                                wchar_t iconLoc[MAX_PATH];
                                int iIcon = 0;

                                // Get Target EXE
                                if (SUCCEEDED(psl->GetPath(target, MAX_PATH, NULL, 0))) {
                                    realPath = target;
                                }
                                // Get Icon Location
                                if (SUCCEEDED(psl->GetIconLocation(iconLoc, MAX_PATH, &iIcon))) {
                                    iconPath = iconLoc;
                                    iconIndex = iIcon;
                                }
                            }
                            ppf->Release();
                        }
                        psl->Release();
                    }
                }

                ((DockModule *)m)->PinApp(realPath, name, iconPath, iconIndex);

                WindowMonitor::GetTopLevelWindows(allWindows, pinnedApps, hwndBar);
                InvalidateRect(hwndBar, NULL, FALSE);
            }
            });
        RegisterDragDrop(hwndBar, pDropTarget);
    }

    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    EmptyWorkingSet(GetCurrentProcess());

    return true;
}

HWND Railing::CreateBarWindow(HINSTANCE hInstance, const ThemeConfig &config)
{
    const wchar_t CLASS_NAME[] = L"Shell_TrayWnd";
    WNDCLASS wc = {};
    wc.lpfnWndProc = Railing::WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(hInstance, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClass(&wc)) {
		if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBox(NULL, L"Railing cannot start because 'Shell_TrayWnd' already exists.\nPlease kill explorer.exe via Task Manager.", L"Error", MB_ICONERROR);
            exit(1);
        }
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    HDC hdc = GetDC(NULL);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    float scale = dpiY / 96.0f;

    int thickness = (int)(config.global.height * scale);
    int mLeft = (int)(config.global.margin.left * scale);
    int mRight = (int)(config.global.margin.right * scale);
    int mTop = (int)(config.global.margin.top * scale);
    int mBottom = (int)(config.global.margin.bottom * scale);

    int x = mLeft, y = mTop, w = screenW - mLeft - mRight, h = thickness;
    std::string pos = config.global.position;

    if (pos == "bottom") y = screenH - thickness - mBottom;
    else if (pos == "left") { w = thickness; h = screenH - mTop - mBottom; }
    else if (pos == "right") { x = screenW - thickness - mRight; w = thickness; h = screenH - mTop - mBottom; }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        CLASS_NAME, L"Railing",
        WS_POPUP, x, y, w, h,
        nullptr, nullptr, hInstance, this);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    WNDCLASSEX wcNotify = { sizeof(WNDCLASSEX) };
	wcNotify.lpfnWndProc = DefWindowProc;
	wcNotify.hInstance = hInstance;
	wcNotify.lpszClassName = L"TrayNotifyWnd";
	RegisterClassEx(&wcNotify);

    CreateWindowEx(
        0, L"TrayNotifyWnd", L"",
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0,
        hwnd,
        NULL, hInstance, NULL);

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    return hwnd;
}

void Railing::RunMessageLoop()
{
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}



LRESULT CALLBACK Railing::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Railing *self = (Railing *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        self = (Railing *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        return TRUE;
    }
    else if (uMsg == WM_TASKBARCREATED && self) {
        UnregisterAppBar(hwnd); // Clear old handle just in case
        RegisterAppBar(hwnd);
        UpdateAppBarPosition(hwnd, self->cachedConfig);
        return 0;
    }

    if (!self) return DefWindowProc(hwnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_PAINT:
        self->DrawBar(hwnd);
        return 0;
    case WM_COPYDATA:
        return TrayBackend::Get().HandleCopyData((HWND)wParam, (COPYDATASTRUCT *)lParam);
    case WM_ERASEBKGND:
        return 1;
    case WM_USER+999:
        self->networkBackend.GetCurrentStatus(self->cachedWifiState, self->cachedWifiSignal);
        if (self->renderer) {
            self->renderer->currentStats.isWifiConnected = self->cachedWifiState;
            self->renderer->currentStats.wifiSignal = self->cachedWifiSignal;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
        // --- DELEGATED INPUT ---
    case WM_MOUSEMOVE:
        if (self->inputManager) self->inputManager->HandleMouseMove(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONDOWN:
        if (self->inputManager) self->inputManager->HandleLeftClick(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONUP:
        if (self->inputManager) self->inputManager->HandleRightClick(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        if (self->inputManager) self->inputManager->OnMouseLeave(hwnd);
        return 0;
    case WM_MOUSEWHEEL:
        if (self->inputManager) self->inputManager->HandleScroll(hwnd, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_TIMER:
        if (wParam == 1) {
            self->UpdateSystemStats();
        }
        else if (wParam == 2) {
            // Visualizer Update
            if (self->renderer) {
                bool needsRepaint = false;
                auto UpdateViz = [&](const std::vector<Module *> &list) {
                    for (Module *m : list) {
                        if (m->config.type == "visualizer") { m->Update(); needsRepaint = true; }
                    }
                    };
                UpdateViz(self->renderer->leftModules);
                UpdateViz(self->renderer->centerModules);
                UpdateViz(self->renderer->rightModules);

                if (needsRepaint) InvalidateRect(hwnd, NULL, FALSE);
            }

            // AutoHide Logic
            if (self->cachedConfig.global.autoHide) {
                POINT pt; GetCursorPos(&pt);
                RECT barRect; GetWindowRect(hwnd, &barRect);
                InflateRect(&barRect, 0, 2);
                bool mouseOver = PtInRect(&barRect, pt);
                bool flyoutOpen = (self->flyout && self->flyout->IsVisible()) ||
                    (self->trayFlyout && self->trayFlyout->IsVisible()) ||
                    (self->networkFlyout && self->networkFlyout->IsVisible());

                bool shouldShow = false;
                if (flyoutOpen || mouseOver || self->IsMouseAtEdge()) {
                    shouldShow = true;
                    self->lastInteractionTime = GetTickCount64();
                }
                else {
                    if ((GetTickCount64() - self->lastInteractionTime) < self->cachedConfig.global.autoHideDelay)
                        shouldShow = true;
                }

                float oldProgress = self->showProgress;
                float speed = 0.15f;
                if (shouldShow) { self->showProgress += speed; if (self->showProgress > 1.0f) self->showProgress = 1.0f; }
                else { self->showProgress -= speed; if (self->showProgress < 0.0f) self->showProgress = 0.0f; }

                if (self->showProgress != oldProgress) {
                    self->isHidden = !shouldShow;
                    UpdateAppBarPosition(hwnd, self->cachedConfig); // Update Pos

                    RECT r; GetWindowRect(hwnd, &r);
                    int h = r.bottom - r.top;
                    int y = (self->cachedConfig.global.position == "bottom")
                        ? (GetSystemMetrics(SM_CYSCREEN) - (int)(h * self->showProgress))
                        : ((int)(h * self->showProgress) - h);
                    SetWindowPos(hwnd, HWND_TOPMOST, r.left, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
                }
            }
        }
        return 0;

    case WM_SIZE:
        if (self->renderer) {
            self->renderer->Resize();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMPOWERSTATUSCHANGE) InvalidateRect(hwnd, NULL, FALSE);
        return TRUE;

        // Mouse Cursor (Keeping simplified check)
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && self->renderer) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            float dpi = (float)GetDpiForWindow(hwnd);
            float scale = dpi / 96.0f;
            bool hit = false;
            for (auto const &[id, cfg] : self->renderer->theme.modules) {
                D2D1_RECT_F f = self->renderer->GetModuleRect(id);
                if (f.right == 0.0f) continue;
                RECT localR = { (LONG)(f.left * scale), (LONG)(f.top * scale), (LONG)(f.right * scale), (LONG)(f.bottom * scale) };
                if (PtInRect(&localR, pt)) { hit = true; break; }
            }
            SetCursor(LoadCursor(NULL, hit ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_RAILING_AUDIO_UPDATE:
        self->cachedVolume = (int)wParam / 100.0f;
        self->cachedMute = (bool)lParam;
        if (self->renderer) self->renderer->UpdateAudioStats(self->cachedVolume, self->cachedMute);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_RAILING_APPBAR:
        switch ((UINT)wParam)
        {
        case ABN_POSCHANGED:
            UpdateAppBarPosition(hwnd, self->cachedConfig);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;

        case ABN_FULLSCREENAPP:
            if ((BOOL)lParam) {
                SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            else { // Game closed. WE MUST POP BACK UP.
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                UpdateAppBarPosition(hwnd, self->cachedConfig);
            }
            break;
        }
        return 0;
    case WM_ACTIVATE:
        if (self) {
            APPBARDATA abd = { sizeof(abd) };
            abd.hWnd = hwnd;
            abd.uCallbackMessage = WM_RAILING_APPBAR;
            // Tell shell we are active
            SHAppBarMessage(ABM_ACTIVATE, &abd);
        }
        if (LOWORD(wParam) == WA_INACTIVE) {
            self->inputManager->tooltips->Hide();
        }
        return 0;
    case WM_RAILING_CMD:
        if (wParam == CMD_SWITCH_WORKSPACE && self->renderer) {
            Module *m = self->renderer->GetModule("workspaces");
            if (m) {
                ((WorkspacesModule *)m)->SetActiveIndex((int)lParam);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_KILL_THIS) DestroyWindow(hwnd);
        else if (wParam >= 100 && wParam <= 104) {
            int idx = (int)wParam - 100;
            self->workspaces.SwitchWorkspace(idx);
            Module *m = self->renderer->GetModule("workspaces");
            if (m) ((WorkspacesModule *)m)->SetActiveIndex(idx);
            WindowMonitor::GetTopLevelWindows(self->allWindows, self->pinnedApps, self->hwndBar);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_DESTROY:
        UnregisterAppBar(hwnd);
        if (self->titleHook) UnhookWinEvent(self->titleHook);
        if (self->windowLifecycleHook) UnhookWinEvent(self->windowLifecycleHook);
        if (self->flyout) DestroyWindow(self->flyout->hwnd);
        if (self->trayFlyout) delete self->trayFlyout;
        if (self->networkFlyout) delete self->networkFlyout;
        PostQuitMessage(0);
        return 0;

    default:
        if (uMsg == self->shellMsgId) {
            int code = (int)wParam;
            HWND newWin = (HWND)lParam;

            if (code == HSHELL_WINDOWCREATED) {
                if (IsWindow(newWin) && IsWindowVisible(newWin)) {
                    WindowMonitor::GetTopLevelWindows(self->allWindows, self->pinnedApps, self->hwndBar);
                    self->workspaces.AddWindow(newWin);
                }
                self->needsWindowRefresh = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // FIX: Handle Activation Separately
            else if (code == HSHELL_WINDOWACTIVATED || code == HSHELL_RUDEAPPACTIVATED) {
				WindowMonitor::GetTopLevelWindows(self->allWindows, self->pinnedApps, self->hwndBar);
				self->workspaces.AddWindow(newWin);

                Module *m = self->renderer->GetModule("dock");
                if (m) {
                    // CRITICAL: Kill the 500ms wait timer immediately
                    ((DockModule *)m)->SetOptimisticFocus(newWin);

                    // Clear "Red Dot" attention if we focus the window
                    ((DockModule *)m)->ClearAttention(newWin);
                }
                // Use InvalidateRect (as you requested), but the timer kill above is what removes the delay.
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // FIX: Handle Flash (Notification) Separately
            else if (code == HSHELL_FLASH || uMsg == HSHELL_FLASH) {
                Module *m = self->renderer->GetModule("dock");
                if (m) ((DockModule *)m)->SetAttention(newWin, true);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            else if (code == HSHELL_WINDOWDESTROYED) {
                WindowMonitor::GetTopLevelWindows(self->allWindows, self->pinnedApps, self->hwndBar);

                Module *m = self->renderer->GetModule("dock");
                if (m) {
                    ((DockModule *)m)->MarkDirty();

                    if (newWin == ((DockModule *)m)->optimisticHwnd) {
                        ((DockModule *)m)->SetOptimisticFocus(NULL);
                    }
                }

                InvalidateRect(hwnd, nullptr, FALSE);
            }

            self->needsWindowRefresh = true;
        }
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void Railing::WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (!instance || !instance->hwndBar) return;
    if (idObject != OBJID_WINDOW) return;

    if (event == EVENT_OBJECT_NAMECHANGE && IsWindow(hwnd)) {
        InvalidateRect(instance->hwndBar, nullptr, FALSE);
    }
    else if (event == EVENT_SYSTEM_FOREGROUND) {

        // 1. Refresh Window List Immediately
        WindowMonitor::GetTopLevelWindows(instance->allWindows, instance->pinnedApps, instance->hwndBar);
        instance->workspaces.AddWindow(hwnd);

        // 2. Force Dock Logic
        if (instance->renderer) {
            Module *m = instance->renderer->GetModule("dock");
            if (m) {
                ((DockModule *)m)->MarkDirty();
                ((DockModule *)m)->SetOptimisticFocus(hwnd);
                ((DockModule *)m)->ClearAttention(hwnd);
            }
        }

        // 3. Paint Now
        InvalidateRect(instance->hwndBar, nullptr, FALSE);
    }
    else if (event == EVENT_OBJECT_CREATE ||
            event == EVENT_OBJECT_DESTROY ||
            event == EVENT_OBJECT_SHOW ||
            event == EVENT_OBJECT_HIDE) {
        WindowMonitor::GetTopLevelWindows(instance->allWindows, instance->pinnedApps, instance->hwndBar);
        if (instance->renderer) {
            Module *m = instance->renderer->GetModule("dock");
            if (m) ((DockModule *)m)->MarkDirty();
        }

        InvalidateRect(instance->hwndBar, nullptr, FALSE);
    }
}

ULONGLONG Railing::GetInterval(std::string type, int def) {
    Module *m = renderer->GetModule(type);
    return (m && m->config.interval > 0) ? (ULONGLONG)m->config.interval : (ULONGLONG)def;
}

void Railing::UpdateSystemStats() {
    ULONGLONG now = GetTickCount64();
    bool needsRepaint = false;
    static ULONGLONG lastWifiUpdate = 0;

    if (now - lastCpuUpdate >= GetInterval("cpu", 1000)) {
        cachedCpuUsage = stats.GetCpuUsage();
        lastCpuUpdate = now;
        needsRepaint = true;
    }
    if (now - lastRamUpdate >= GetInterval("ram", 1000)) {
        cachedRamUsage = stats.GetRamUsage();
        lastRamUpdate = now;
        needsRepaint = true;
    }
    if (now - lastGpuUpdate >= GetInterval("gpu", 1000)) {
        cachedGpuTemp = gpuStats.GetGpuTemp();
        lastGpuUpdate = now;
        needsRepaint = true;
    }
    if (now - lastWifiUpdate >= 2000) {
        networkBackend.GetCurrentStatus(cachedWifiState, cachedWifiSignal);
        lastWifiUpdate = now;
        needsRepaint = true;
    }
    if (needsRepaint) InvalidateRect(hwndBar, NULL, FALSE);
}

void Railing::DrawBar(HWND hwnd) {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    if (!renderer) {
        renderer = new RailingRenderer(hwnd, cachedConfig);
        renderer->pWorkspaceManager = &workspaces;
        renderer->Resize();
    }

    RailingRenderer::SystemStatusData statsData;
    statsData.cpuUsage = cachedCpuUsage;
    statsData.ramUsage = cachedRamUsage;
    statsData.gpuTemp = cachedGpuTemp;
    statsData.volume = cachedVolume;
    statsData.isMuted = cachedMute;
    statsData.wifiSignal = cachedWifiSignal;
    statsData.isWifiConnected = cachedWifiState;

    renderer->UpdateStats(statsData);
    renderer->Draw(allWindows, pinnedApps, GetForegroundWindow());
    EndPaint(hwnd, &ps);
}

bool Railing::IsMouseAtEdge() {
    POINT pt; GetCursorPos(&pt);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    std::string pos = cachedConfig.global.position;
    int tolerance = 2;

    if (pos == "bottom") return pt.y >= screenH - tolerance;
    else if (pos == "top") return pt.y <= tolerance;
    else if (pos == "left") return pt.x <= tolerance;
    else if (pos == "right") return pt.x >= screenW - tolerance;
    return false;
}