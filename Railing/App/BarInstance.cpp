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

    hwnd = CreateBarWindow(hInstance, makePrimary);
    if (!hwnd) return false;
    if (isPrimary) {
		RegisterAppBar(hwnd);
		UpdateAppBarPosition(hwnd, config);
    }
    RegisterHotKey(hwnd, 900, MOD_CONTROL | MOD_SHIFT, 0x51);
    if (makePrimary) {
        RegisterShellHookWindow(hwnd);

        // --- FIX: Restore ALL Message Filters from Old Railing.cpp ---
        // Critical for Drag/Drop and Tray Icons crossing process boundaries
        ChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
        ChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);
        ChangeWindowMessageFilter(0x0049, MSGFLT_ADD); // WM_COPYGLOBALDATA - Vital for Tray!
        ChangeWindowMessageFilter(WM_TASKBARCREATED, MSGFLT_ADD);

        // --- FIX: Tell the world we exist so they send us icons ---
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
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
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
        if (wParam == 900) PostQuitMessage(0);
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

void BarInstance::OnTimerTick() {
    tickCount++;

    // --- 1. STATS UPDATE (Keep existing logic) ---
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
            // Don't InvalidateRect here, we do it at the end of the function now
        }
    }

    // --- 2. ROBUST AUTO-HIDE LOGIC ---
    if (config.global.autoHide) {

        // A. Get Mouse & Geometry
        POINT pt; GetCursorPos(&pt);
        RECT rcWindow; GetWindowRect(hwnd, &rcWindow);

        // B. Identify Monitor based on MOUSE (More reliable than Window when hidden)
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);

        // C. Determine "Intention" (Show or Hide?)
        // We use a simple distance check. If mouse is within 10px of the edge, SHOW.
        // If mouse is inside the bar (while open), KEEP SHOWING.

        bool shouldShow = false;
        const int TRIGGER_DIST = 4; // Easy hit

        // We also check if the mouse is hovering the CURRENT window rect
        // (This keeps it open while you use it)
        bool isHoveringWindow = PtInRect(&rcWindow, pt);

        if (config.global.position == "bottom") {
            bool atEdge = pt.y >= mi.rcMonitor.bottom - TRIGGER_DIST;
            shouldShow = atEdge || isHoveringWindow;
        }
        else if (config.global.position == "top") {
            bool atEdge = pt.y <= mi.rcMonitor.top + TRIGGER_DIST;
            shouldShow = atEdge || isHoveringWindow;
        }
        else if (config.global.position == "left") {
            bool atEdge = pt.x <= mi.rcMonitor.left + TRIGGER_DIST;
            shouldShow = atEdge || isHoveringWindow;
        }
        else if (config.global.position == "right") {
            bool atEdge = pt.x >= mi.rcMonitor.right - TRIGGER_DIST;
            shouldShow = atEdge || isHoveringWindow;
        }

        // D. Calculate Target
        int currentW = rcWindow.right - rcWindow.left;
        int currentH = rcWindow.bottom - rcWindow.top;
        int targetX = rcWindow.left;
        int targetY = rcWindow.top;
        const int PEEK = 2; // The 2px sliver

        if (config.global.position == "bottom") {
            int visibleY = mi.rcMonitor.bottom - currentH;
            int hiddenY = mi.rcMonitor.bottom - PEEK;
            targetY = shouldShow ? visibleY : hiddenY;
        }
        else if (config.global.position == "top") {
            int visibleY = mi.rcMonitor.top;
            int hiddenY = mi.rcMonitor.top - currentH + PEEK;
            targetY = shouldShow ? visibleY : hiddenY;
        }
        else if (config.global.position == "left") {
            int visibleX = mi.rcMonitor.left;
            int hiddenX = mi.rcMonitor.left - currentW + PEEK;
            targetX = shouldShow ? visibleX : hiddenX;
        }
        else if (config.global.position == "right") {
            int visibleX = mi.rcMonitor.right - currentW;
            int hiddenX = mi.rcMonitor.right - PEEK;
            targetX = shouldShow ? visibleX : hiddenX;
        }

        // E. Apply Movement
        int currentX = rcWindow.left;
        int currentY = rcWindow.top;

        if (currentX != targetX || currentY != targetY) {
            // Lerp (Smooth Slide)
            int stepX = (targetX - currentX) / 3; // Fast slide
            int stepY = (targetY - currentY) / 3;

            // Snap when close to avoid jitter
            if (abs(targetX - currentX) < 2) stepX = targetX - currentX;
            else if (stepX == 0) stepX = (targetX > currentX) ? 1 : -1;

            if (abs(targetY - currentY) < 2) stepY = targetY - currentY;
            else if (stepY == 0) stepY = (targetY > currentY) ? 1 : -1;

            // [CRITICAL FIX]
            // 1. HWND_TOPMOST: Forces it above maximized windows
            // 2. SWP_NOACTIVATE: Prevents stealing focus from games/IDE
            // 3. Flags: We DO NOT use SWP_ASYNCWINDOWPOS to ensure it happens now.

            SetWindowPos(hwnd, HWND_TOPMOST,
                currentX + stepX,
                currentY + stepY,
                0, 0,
                SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);

            // Force visual refresh immediately
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateWindow(hwnd);
        }
    }
}