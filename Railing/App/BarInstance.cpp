#include "BarInstance.h"
#include <PinnedAppsIO.h>
#include <WindowMonitor.h>
#include "InputManager.h"
#include <windowsx.h>
#include <VolumeFlyout.h>
#include <AppBarManager.h>
#include <wchar.h>
#define STATS_TIMER_ID 1

UINT WM_SHELLHOOKMESSAGE = RegisterWindowMessageW(L"SHELLHOOK");
UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
UINT WM_APPBAR_CALL = RegisterWindowMessageW(L"AppBarMessage");

BarInstance::BarInstance(const std::string &configFile) : configFileName(configFile)
{
	config = ThemeLoader::Load(configFile.c_str());
}
BarInstance::~BarInstance() {
    if (hwnd) {
        KillTimer(hwnd, STATS_TIMER_ID); // Kill stats timer
        KillTimer(hwnd, ANIMATION_TIMER_ID); // Kill anim timer
        UnregisterHotKey(hwnd, 900);
        if (isPrimary) {
            for (int i = 0; i < 5; i++) UnregisterHotKey(hwnd, 100 + i);
            UnregisterHotKey(hwnd, HOTKEY_KILL_THIS);
            UnregisterAppBar(hwnd);
        }
        DestroyWindow(hwnd);
    }

    if (renderer) {
        delete renderer;
        renderer = nullptr;
    }
    if (flyout) {
        delete flyout;
        flyout = nullptr;
    }
    if (trayFlyout) {
        delete trayFlyout;
        trayFlyout = nullptr;
    }
    if (networkFlyout) {
        delete networkFlyout;
        networkFlyout = nullptr;
    }
    if (pDropTarget) {
        pDropTarget->Release();
        pDropTarget = nullptr;
    }
    // Note: inputManager is a std::unique_ptr, it cleans itself up.
}

bool BarInstance::Initialize(HINSTANCE hInstance, bool makePrimary) {
    this->isPrimary = makePrimary;
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    hwnd = CreateBarWindow(hInstance, makePrimary);
    if (!hwnd) return false;
    if (makePrimary && config.global.autoHide) {
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;  // Generic desktop
        rid.usUsage = 0x02;      // Mouse
        rid.dwFlags = RIDEV_INPUTSINK; // Receive input even when not in foreground
        rid.hwndTarget = hwnd;

        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            OutputDebugStringW(L"[RAILING] Failed to register raw input!\n");
        }
    }
    if (isPrimary) {
		RegisterAppBar(hwnd);
        if (!config.global.autoHide) UpdateAppBarPosition(hwnd, config);
        else {
            HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);
            RECT rcMon = mi.rcMonitor;
            float dpi = (float)GetDpiForWindow(hwnd);
            int height = (int)(config.global.height * (dpi / 96.0f));
            int width = rcMon.right - rcMon.left;
            int x = rcMon.left;
            int y = rcMon.bottom - 1;
            if (config.global.position == "top") y = rcMon.top;
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    RegisterHotKey(hwnd, 900, MOD_CONTROL | MOD_SHIFT, 0x51);
    if (makePrimary) {
        RegisterShellHookWindow(hwnd);

        ChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
        ChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);
        ChangeWindowMessageFilter(0x0049, MSGFLT_ADD); // WM_COPYGLOBALDATA - Vital for Tray!
        ChangeWindowMessageFilter(WM_TASKBARCREATED, MSGFLT_ADD);

        SendNotifyMessage(HWND_BROADCAST, RegisterWindowMessage(L"TaskbarCreated"), 0, 0);

        for (int i = 0; i < 5; i++) RegisterHotKey(hwnd, 100 + i, MOD_ALT | MOD_NOREPEAT, 0x31 + i);
        RegisterHotKey(hwnd, HOTKEY_KILL_THIS, MOD_CONTROL | MOD_SHIFT, 0x51);
    }

    renderer = new RailingRenderer(hwnd, config);
    renderer->pWorkspaceManager = &workspaces;
    renderer->Resize();

    if (Module::HasType(config, "audio")) {
        flyout = new VolumeFlyout(this, hInstance, renderer->GetFactory(), renderer->GetWriteFactory(), renderer->GetTextFormat(), renderer->theme);
        flyout->audio.EnsureInitialized(hwnd);
    }

    if (Module::HasType(config, "tray")) {
        trayFlyout = new TrayFlyout(this, hInstance, renderer->GetFactory(), renderer->GetWICFactory(), &tooltips, config);
    }

    if (Module::HasType(config, "network")) {
        networkFlyout = new NetworkFlyout(this, hInstance, renderer->GetFactory(), renderer->GetWriteFactory(), renderer->GetTextFormat(), renderer->GetIconFormat(), renderer->theme);
    }

    tooltips.Initialize(hwnd);
    inputManager = std::make_unique<InputManager>(this, renderer, &tooltips);

    std::vector<WindowInfo> windows;
    WindowMonitor::GetTopLevelWindows(windows, config.pinnedPaths, hwnd);
    for (const auto &win : windows) workspaces.AddWindow(win.hwnd);

    if (Railing::instance) {
        Railing::instance->networkBackend.GetCurrentStatus(
            Railing::instance->cachedWifiState,
            Railing::instance->cachedWifiSignal
        );
        Railing::instance->UpdateGlobalStats();
    }

    if (renderer && Railing::instance) {
        SystemStatusData d;
        d.cpuUsage = Railing::instance->cachedCpuUsage;
        d.ramUsage = Railing::instance->cachedRamUsage;
        d.gpuTemp = Railing::instance->cachedGpuTemp;
        d.wifiSignal = Railing::instance->cachedWifiSignal;
        d.isWifiConnected = Railing::instance->cachedWifiState;
        d.volume = Railing::instance->cachedVolume;
        d.isMuted = Railing::instance->cachedMute;

        renderer->UpdateStats(d);
    }

    if (flyout) {
        flyout->audio.EnsureInitialized(hwnd);
        // Force a re-send of the volume to run the handler we fixed in Step 1
        // (Assuming your AudioCapture class has a method to re-broadcast, 
        //  otherwise it happens automatically on first hook)
    }

    if (Module::HasType(config, "visualizer")) {
        if (!Railing::instance->visualizerBackend)
            Railing::instance->visualizerBackend = new AudioCapture();
    }

    if (Module::HasType(config, "gpu")) {
        if (!Railing::instance->gpuStats.IsInitialized()) {
            Railing::instance->gpuStats.Initialize();
        }
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    SetTimer(hwnd, STATS_TIMER_ID, 1000, NULL);
    SetTimer(hwnd, ANIMATION_TIMER_ID, 16, NULL);

    return true;
}

HWND BarInstance::CreateBarWindow(HINSTANCE hInstance, bool makePrimary) {
	const wchar_t *className = makePrimary ? L"Shell_TrayWnd" : L"RailingBar";

	WNDCLASSEX wc = {sizeof(WNDCLASSEX)};
	wc.lpfnWndProc = BarWndProc;
	wc.hInstance = hInstance;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
	wc.lpszClassName = className;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	ATOM atom = RegisterClassEx(&wc);
    if (atom == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_CLASS_ALREADY_EXISTS);
        else /* TODO */;
    }
    else {
        int x = 0, y = 0, w = 800, h = 50;
        HWND hBar = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            className, L"Railing",
            WS_POPUP, x, y, w, h,
            nullptr, nullptr, hInstance, this);
        if (!hBar) return NULL;

        if (makePrimary) {
            WNDCLASSEX wcT = { sizeof(WNDCLASSEX) };
            wcT.lpfnWndProc = DefWindowProc;
            wcT.hInstance = hInstance;
            wcT.lpszClassName = L"TrayNotifyWnd";
            RegisterClassEx(&wcT);

            CreateWindowEx(0, L"TrayNotifyWnd", L"", WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, hBar, NULL, hInstance, NULL);
        }

        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(hBar, &margins);
        return hBar;
    }
    return NULL;
}

void BarInstance::Reposition() {
	if (!hwnd) return;
	UpdateAppBarPosition(hwnd, config);
}

void BarInstance::ReloadConfig() {
	config = ThemeLoader::Load(configFileName.c_str());
	if (renderer) {
		renderer->Reload(configFileName.c_str());
		renderer->Resize();
	}
	Reposition();
	InvalidateRect(hwnd, NULL, FALSE);
}

void BarInstance::SaveState() { ThemeLoader::Save(configFileName.c_str(), config); }

void BarInstance::UpdateStats(const SystemStatusData &stats) {
	if (renderer) {
		renderer->UpdateStats(stats);
		InvalidateRect(hwnd, NULL, FALSE);
	}
}

LRESULT CALLBACK BarInstance::BarWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

    // Retrieve the instance pointer
    BarInstance *self = (BarInstance *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        self = (BarInstance *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        return TRUE;
    }

    if (!self) return DefWindowProc(hwnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (self->renderer) {
            std::vector<WindowInfo> windows;
            WindowMonitor::GetTopLevelWindows(windows, self->config.pinnedPaths, hwnd);
            self->renderer->Draw(windows, self->config.pinnedPaths, GetForegroundWindow());
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;
    case WM_INPUT: {
        if (self->config.global.autoHide) {
            // Force autohide check on every mouse input
            static DWORD lastCheck = 0;
            DWORD now = GetTickCount64();
            if (now - lastCheck > 16) { // Throttle to ~60fps
                self->OnTimerTick();
                lastCheck = now;
            }
        }
        return 0;
    }
        // --- FIX 1: Restore Cursor Hand Logic ---
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && self->renderer) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            float dpi = (float)GetDpiForWindow(hwnd);
            float scale = dpi / 96.0f;
            bool hit = false;

            // Loop modules just like Old Railing.cpp did
            auto Check = [&](const std::vector<Module *> &list) {
                for (auto *m : list) {
                    if (m->cachedRect.right == 0.0f) continue;
                    // Simple hit check
                    if (pt.x >= m->cachedRect.left * scale && pt.x <= m->cachedRect.right * scale &&
                        pt.y >= m->cachedRect.top * scale && pt.y <= m->cachedRect.bottom * scale) return true;
                }
                return false;
                };

            if (Check(self->renderer->leftModules) ||
                Check(self->renderer->centerModules) ||
                Check(self->renderer->rightModules)) {
                hit = true;
            }
            SetCursor(LoadCursor(NULL, hit ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_RAILING_AUDIO_UPDATE:
        if (self->renderer) {
            float vol = (float)wParam / 100.0f;
            bool mute = (bool)lParam;
            self->renderer->UpdateAudioStats(vol, mute);
            if (Railing::instance) {
                Railing::instance->cachedVolume = vol;
                Railing::instance->cachedMute = mute;
            }

            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_USER + 999: {
        Railing::instance->networkBackend.GetCurrentStatus(
            Railing::instance->cachedWifiState,
            Railing::instance->cachedWifiSignal
        );

        if (self->renderer) {
            SystemStatusData d;
            d.cpuUsage = Railing::instance->cachedCpuUsage;
            d.ramUsage = Railing::instance->cachedRamUsage;
            d.gpuTemp = Railing::instance->cachedGpuTemp;
            d.volume = Railing::instance->cachedVolume;
            d.isMuted = Railing::instance->cachedMute;

            d.isWifiConnected = Railing::instance->cachedWifiState;
            d.wifiSignal = Railing::instance->cachedWifiSignal;
            self->renderer->UpdateStats(d);

            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (AppBarManager::Get().isUpdating) return 0;

        if (wParam == SPI_SETWORKAREA) return 0;
        if (lParam != 0 && _wcsicmp((wchar_t *)lParam, L"WorkArea") == 0) return 0;

        self->ReloadConfig();
        break;
    case WM_NCHITTEST: {
        if (self->interactionMode == InteractionMode::None)
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

        if (self->interactionMode == InteractionMode::Move) return HTCAPTION;

        if (self->interactionMode == InteractionMode::Resize) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc; GetWindowRect(hwnd, &rc);
            const int BORDER = 15;

            if (self->config.global.position == "bottom") {
                if (pt.y >= rc.top && pt.y < rc.top + BORDER) return HTTOP;
            }
            else if (self->config.global.position == "top") {
                if (pt.y >= rc.bottom - BORDER && pt.y <= rc.bottom) return HTBOTTOM;
            }
            else if (self->config.global.position == "left") {
                if (pt.x >= rc.right - BORDER && pt.x <= rc.right) return HTRIGHT;
            }
            else if (self->config.global.position == "right") {
                if (pt.x >= rc.left && pt.x < rc.left + BORDER) return HTLEFT;
            }
            return HTCLIENT;
        }
        return HTCLIENT;
    }
    case WM_COPYDATA: {
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lParam;
        if (!cds || !cds->lpData) return FALSE;

        // 1. Peek at the first DWORD (cbSize)
        // Both APPBARDATA and NOTIFYICONDATA start with a 'cbSize' member.
        DWORD dataSize = *(DWORD *)cds->lpData;
        const DWORD APPBARDATA_32_SIZE = 36;

        if (dataSize == sizeof(APPBARDATA) || dataSize == APPBARDATA_32_SIZE) {
            return AppBarManager::Get().HandleAppBarMessage(wParam, lParam);
        }
        else {
            // It is likely a Tray Icon (NOTIFYICONDATA is much larger)
            return TrayBackend::Get().HandleCopyData((HWND)wParam, cds);
        }
    }
    case WM_COMMAND:
		MainMenu::HandleMenuCmd(hwnd, LOWORD(wParam));
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMaxTrackSize.x = GetSystemMetrics(SM_CXVIRTUALSCREEN) + 100;
        mmi->ptMaxTrackSize.y = GetSystemMetrics(SM_CYVIRTUALSCREEN) + 100;
        return 0;
    }
    case WM_EXITSIZEMOVE:
        if (self->interactionMode != InteractionMode::None)
            SendMessage(hwnd, WM_COMMAND, MainMenu::MenuCommand::CMD_INTERACT_SNAP, 0);
        break;
        // Input Handling
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
    case WM_HOTKEY: {
        if (wParam == 900) {
            PostQuitMessage(0);
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_TIMER:
        if (wParam == ANIMATION_TIMER_ID) {
            self->OnTimerTick(); // Handles animations
        }
        else if (wParam == STATS_TIMER_ID) {
            if (Railing::instance) {
                Railing::instance->UpdateGlobalStats();

                Railing::instance->networkBackend.GetCurrentStatus(
                    Railing::instance->cachedWifiState,
                    Railing::instance->cachedWifiSignal
                );
            }

            if (self->renderer && Railing::instance) {
                SystemStatusData d;
                d.cpuUsage = Railing::instance->cachedCpuUsage;
                d.ramUsage = Railing::instance->cachedRamUsage;
                d.gpuTemp = Railing::instance->cachedGpuTemp;
                d.wifiSignal = Railing::instance->cachedWifiSignal;
                d.isWifiConnected = Railing::instance->cachedWifiState;
                d.volume = Railing::instance->cachedVolume;
                d.isMuted = Railing::instance->cachedMute;

                self->renderer->UpdateStats(d);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
        // Shell Hooks
    default:
        if (uMsg == WM_SHELLHOOKMESSAGE) {
            int code = (int)wParam;
            HWND targetWin = (HWND)lParam;
            if (code == HSHELL_WINDOWCREATED || code == HSHELL_WINDOWACTIVATED || code == HSHELL_RUDEAPPACTIVATED) {
                self->workspaces.AddWindow(targetWin);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            else if (code == HSHELL_WINDOWDESTROYED) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

bool IsFullscreenWindowActive() {
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == GetDesktopWindow() || foreground == GetShellWindow())
        return false;

    RECT windowRect;
    if (GetWindowRect(foreground, &windowRect)) {
        HMONITOR hMon = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfo(hMon, &mi)) {
            // Check if the window matches or exceeds the monitor dimensions
            return (windowRect.left <= mi.rcMonitor.left &&
                windowRect.top <= mi.rcMonitor.top &&
                windowRect.right >= mi.rcMonitor.right &&
                windowRect.bottom >= mi.rcMonitor.bottom);
        }
    }
    return false;
}

void BarInstance::OnTimerTick() {
    tickCount++;

    if (tickCount % 60 == 0) {
        if (Railing::instance) {
            Railing::instance->UpdateGlobalStats();
            Railing::instance->networkBackend.GetCurrentStatus(Railing::instance->cachedWifiState, Railing::instance->cachedWifiSignal);
        }
        if (renderer && Railing::instance) {
            SystemStatusData d;
            d.cpuUsage = Railing::instance->cachedCpuUsage;
            d.gpuTemp = Railing::instance->cachedGpuTemp;
            d.ramUsage = Railing::instance->cachedRamUsage;
            d.wifiSignal = Railing::instance->cachedWifiSignal;
            d.isWifiConnected = Railing::instance->cachedWifiState;
            d.volume = Railing::instance->cachedVolume;
            d.isMuted = Railing::instance->cachedMute;

            renderer->UpdateStats(d);
        }
    }

    if (config.global.autoHide) {
        POINT pt; GetCursorPos(&pt);

        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);

        RECT rcWindow;
        GetWindowRect(hwnd, &rcWindow);

        const int TRIGGER_ZONE = 10;
        bool mouseAtEdge = false;

        if (config.global.position == "bottom") {
            mouseAtEdge = (pt.y >= mi.rcMonitor.bottom - TRIGGER_ZONE);
        }
        else if (config.global.position == "top") {
            mouseAtEdge = (pt.y <= mi.rcMonitor.top + TRIGGER_ZONE);
        }
        bool inZone = PtInRect(&rcWindow, pt);
        bool shouldShow = mouseAtEdge || inZone;

        int currentH = rcWindow.bottom - rcWindow.top;
        int targetW = mi.rcMonitor.right - mi.rcMonitor.left;
        int currentW = rcWindow.right - rcWindow.left;
        if (currentW != targetW)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, targetW, currentH, SWP_NOMOVE | SWP_NOACTIVATE);

        int targetY = rcWindow.top;
        int targetX = mi.rcMonitor.left;
        const int PEEK = 1;

        if (config.global.position == "bottom") {
            int shownY = mi.rcMonitor.bottom - currentH;
            int hiddenY = mi.rcMonitor.bottom - PEEK;
            targetY = shouldShow ? shownY : hiddenY;
        }
        else if (config.global.position == "top") {
            int shownY = mi.rcMonitor.top;
            int hiddenY = mi.rcMonitor.top - currentH + PEEK;
            targetY = shouldShow ? shownY : hiddenY;
        }

        // --- APPLY ---
        int nextY = rcWindow.top;

        if (rcWindow.top != targetY) {
            int delta = targetY - rcWindow.top;
            if (abs(delta) < 2) nextY = targetY;
            else nextY = rcWindow.top + (int)(delta * 0.3);

            SetWindowPos(hwnd, HWND_TOPMOST, targetX, nextY, 0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE);
        }
        else {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}