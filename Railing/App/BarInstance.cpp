#include "BarInstance.h"
#include <PinnedAppsIO.h>
#include <WindowMonitor.h>
#include "InputManager.h"
#include <windowsx.h>
#include <VolumeFlyout.h>

UINT WM_SHELLHOOKMESSAGE = RegisterWindowMessageW(L"SHELLHOOK");
UINT WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

BarInstance::BarInstance(const std::string &configFile) : configFileName(configFile)
{
	config = ThemeLoader::Load(configFile.c_str());
}
BarInstance::~BarInstance() {
    if (hwnd) {
        KillTimer(hwnd, 1); // Kill stats timer
        KillTimer(hwnd, ANIMATION_TIMER_ID); // Kill anim timer
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

    // Note: inputManager is a std::unique_ptr, it cleans itself up.
}

bool BarInstance::Initialize(HINSTANCE hInstance, bool makePrimary) {
    this->isPrimary = makePrimary;

    hwnd = CreateBarWindow(hInstance, makePrimary);
    if (!hwnd) return false;
    RegisterHotKey(hwnd, 900, MOD_CONTROL | MOD_SHIFT, 0x51);
    if (makePrimary) {
        RegisterAppBar(hwnd);
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

    Reposition();

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
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, 1, 1000, NULL);
    SetTimer(hwnd, ANIMATION_TIMER_ID, 16, NULL);

    return true;
}

HWND BarInstance::CreateBarWindow(HINSTANCE hInstance, bool makePrimary) {
	const wchar_t *className = makePrimary ? L"Shell_TrayWnd" : L"RailingBar";

	WNDCLASSEX wc = {sizeof(WNDCLASSEX)};
	wc.lpfnWndProc = BarWndProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = className;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
	RegisterClassEx(&wc);

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

void BarInstance::UpdateStats(const SystemStatusData &stats) {
	if (renderer) {
		renderer->UpdateStats(stats);
		InvalidateRect(hwnd, NULL, FALSE);
	}
}

LRESULT CALLBACK BarInstance::BarWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static UINT WM_SHELLHOOKMESSAGE = RegisterWindowMessageW(L"SHELLHOOK");

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
    case WM_COPYDATA:
        return TrayBackend::Get().HandleCopyData((HWND)wParam, (COPYDATASTRUCT *)lParam);
    case WM_COMMAND:
        MainMenu::HandleMenuCmd(hwnd, LOWORD(wParam));
        return 0;
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
    case WM_HOTKEY:
        if (wParam == 900) {
            PostQuitMessage(0);
            exit(0);
        }
        break;
    case WM_TIMER:
        if (wParam == ANIMATION_TIMER_ID) {
            self->OnTimerTick(); // Handles animations
        }
        else if (wParam == 1) {
            // --- FIX: Handle the 1-second Stats Timer ---

            // 1. Force Global Backend Update
            if (Railing::instance) {
                Railing::instance->UpdateGlobalStats();

                // POLLING: Check Wifi status explicitly here
                Railing::instance->networkBackend.GetCurrentStatus(
                    Railing::instance->cachedWifiState,
                    Railing::instance->cachedWifiSignal
                );
            }

            // 2. Sync to Renderer
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

                // 3. Force Repaint
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
    static int tickCount = 0;
    tickCount++;

    // Update stats every ~1 second (1000ms)
    // 16ms * 60 ticks ~= 960ms
    if (tickCount % 60 == 0) {

        // 1. Trigger Global Updates (CPU, RAM, GPU)
        if (Railing::instance) {
            Railing::instance->UpdateGlobalStats();

            // --- FIX 1: RESTORE WIFI POLLING ---
            // The old code polled this every 2s. We do it here every 1s.
            // This updates the cached variables in the global instance.
            Railing::instance->networkBackend.GetCurrentStatus(
                Railing::instance->cachedWifiState,
                Railing::instance->cachedWifiSignal
            );
        }

        // 2. Sync Global Data -> This Bar
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
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }

    // Auto-hide logic (Keep your existing)
    if (config.global.autoHide) {
        if (showProgress != 1.0f) {
            showProgress = 1.0f;
            isHidden = false;
            Reposition();
        }
        return;
    }
}