#define WIN32_LEAN_AND_MEAN
#include <Winsock2.h>
#include "Railing.h"
#include <windowsx.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <processthreadsapi.h>
#include <Psapi.h>
#include "ModulesConcrete.h"
#include "ThemeLoader.h"

#pragma comment(lib, "dwmapi.lib") 
#pragma comment(lib, "d2d1")
#pragma comment(lib, "shell32.lib")

Railing::Railing()
{}
Railing::~Railing() = default;

Railing *Railing::instance = nullptr;

bool Railing::Initialize(HINSTANCE hInstance)
{
    SetThreadDescription(GetCurrentThread(), L"Railing_MainUI");
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return false;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    instance = this;
    hInst = hInstance;
    stats.Initialize();
    gpuStats.Initialize();

    ThemeConfig tempConfig = ThemeLoader::Load("config.json");
	if (tempConfig.global.blur) tempConfig.global.radius = 8.0f; // This is fixed by Windows :(
    hwndBar = CreateBarWindow(hInstance, tempConfig);
    if (!hwndBar) return false;
    tooltips.Initialize(hwndBar);
    RegisterAppBar(hwndBar);
    UpdateAppBarPosition(hwndBar);

    RECT targetRect;
    GetWindowRect(hwndBar, &targetRect); // UpdateAppBarPosition calculated this for us
    int finalW = targetRect.right - targetRect.left;
    int finalH = targetRect.bottom - targetRect.top;
    int centerX = targetRect.left + (finalW / 2);
    int centerY = targetRect.top + (finalH / 2);
    SendMessage(hwndBar, WM_PAINT, 0, 0);
    ShowWindow(hwndBar, SW_SHOW);
	auto &anim = tempConfig.global.animation;
    if (anim.enabled && anim.duration > 0) { // Calculate timing based on FPS
        int frameDelay = 1000 / anim.fps;
        int totalSteps = anim.duration / frameDelay;
        if (totalSteps < 1) totalSteps = 1;

        for (int i = 1; i <= totalSteps; i++) {
            float progress = (float)i / totalSteps;
            float ease = 1.0f - pow(1.0f - progress, 3.0f);
            float currentScale = anim.startScale + ((1.0f - anim.startScale) * ease);
            int currentW = (int)(finalW * currentScale);
            int currentH = (int)(finalH * currentScale);
            if (currentW % 2 != 0) currentW++;
            if (currentH % 2 != 0) currentH++;
            if (currentW < 10) currentW = 10;
            if (currentH < 2) currentH = 2;
            int currentX = centerX - (currentW / 2);
            int currentY = centerY - (currentH / 2);
            SetWindowPos(hwndBar, HWND_TOPMOST,
                currentX, currentY, currentW, currentH,
                SWP_NOACTIVATE | SWP_NOZORDER);

            UpdateWindow(hwndBar);
            Sleep(frameDelay);
        }
    }
    UpdateAppBarPosition(hwndBar);

    // Register shell hook
    RegisterShellHookWindow(hwndBar);
    UINT shellHookMsg = RegisterWindowMessage(L"SHELLHOOK");
    shellMsgId = shellHookMsg;

	RegisterHotKey(hwndBar, HOTKEY_KILL_THIS, MOD_CONTROL | MOD_SHIFT, 0x51); // Ctrl + Shft + Q to kill explorer (for testing)

    // Listen for name changes
    titleHook = SetWinEventHook(
        EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
        nullptr, Railing::WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    SetTimer(hwndBar, 1, 500, NULL);

    flyout = new VolumeFlyout(hInstance);
    flyout->audio.EnsureInitialized(hwndBar);
    trayFlyout = new TrayFlyout(hInstance);


    ShowWindow(hwndBar, SW_SHOW);
    UpdateWindow(hwndBar);
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    EmptyWorkingSet(GetCurrentProcess());
    return true;
}

HWND Railing::CreateBarWindow(HINSTANCE hInstance, const ThemeConfig &config)
{
    const wchar_t CLASS_NAME[] = L"RailingBar";
    WNDCLASS wc = {};
    wc.lpfnWndProc = Railing::WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(hInstance, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClass(&wc);

    // Get Monitor Dimensions
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // Scale Logic
    HDC hdc = GetDC(NULL);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    float scale = dpiY / 96.0f;

    // Dimensions
    int thickness = (int)(config.global.height * scale);
    int mLeft = (int)(config.global.margin.left * scale);
    int mRight = (int)(config.global.margin.right * scale);
    int mTop = (int)(config.global.margin.top * scale);
    int mBottom = (int)(config.global.margin.bottom * scale);

    int x = 0, y = 0, w = 0, h = 0;
    std::string pos = config.global.position;

    // FIX 2: Explicit Geometry Logic
    if (pos == "bottom") {
        x = mLeft;
        y = screenH - thickness - mBottom;
        w = screenW - mLeft - mRight;
        h = thickness;
    }
    else if (pos == "left") {
        x = mLeft;
        y = mTop;
        w = thickness;
        h = screenH - mTop - mBottom;
    }
    else if (pos == "right") {
        x = screenW - thickness - mRight;
        y = mTop;
        w = thickness;
        h = screenH - mTop - mBottom;
    }
    else { // Default to "top"
        x = mLeft;
        y = mTop;
        w = screenW - mLeft - mRight;
        h = thickness;
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, // Layered is critical for transparency
        CLASS_NAME, L"Railing",
        WS_POPUP,
        x, y, w, h,
        nullptr, nullptr, hInstance, this
    );

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    return hwnd;
}

void Railing::RunMessageLoop()
{
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK Railing::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Railing *self = nullptr;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *cs = reinterpret_cast<CREATESTRUCT *>(lParam);
        self = reinterpret_cast<Railing *>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else {
        self = reinterpret_cast<Railing *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (self) {
            // ONLY refresh if the shell hook triggered it
            if (self->needsWindowRefresh) {
                self->GetTopLevelWindows(self->allWindows);
                self->needsWindowRefresh = false;
            }
            self->DrawBar(hwnd);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // No ugly rectangle backgrounds
    case WM_HOTKEY:
        if (wParam == HOTKEY_KILL_THIS && self) {
            DestroyWindow(hwnd);
        }
		return 0;
    case WM_RAILING_CMD:
        if (wParam == CMD_SWITCH_WORKSPACE && self && self->renderer) {
            Module *m = self->renderer->GetModule("workspaces");
            if (m) { // We know 'workspaces' ID maps to a WorkspacesModule so cast is safe
                WorkspacesModule *ws = static_cast<WorkspacesModule *>(m);
                ws->SetActiveIndex((int)lParam);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self && self->renderer) {
            if (!self->isTrackingMouse) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                self->isTrackingMouse = true;
            }
            float dpi = (float)GetDpiForWindow(hwnd);
            float scale = dpi / 96.0f;
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            POINT pt = { mx, my };

            std::wstring newText = L"";
            D2D1_RECT_F newRectF = { 0 };
            bool hitFound = false;

            // Helper: Checks if mouse is over a module ID
            auto IsHovering = [&](const char *id, D2D1_RECT_F *outRect) -> bool {
                D2D1_RECT_F r = self->renderer->GetModuleRect(id);
                if (r.right == 0.0f && r.bottom == 0.0f) return false;
                RECT phys = {
                    (LONG)(r.left * scale), (LONG)(r.top * scale),
                    (LONG)(r.right * scale), (LONG)(r.bottom * scale)
                };
                if (PtInRect(&phys, pt)) {
                    *outRect = r;
                    return true;
                }
                return false;
                };
            for (const auto &target : self->windowTargets) {
                if (PtInRect(&target.rect, pt)) {
                    wchar_t title[256];
                    GetWindowTextW(target.hwnd, title, 256);
                    newText = title;
                    newRectF = D2D1::RectF(
                        (float)target.rect.left / scale, (float)target.rect.top / scale,
                        (float)target.rect.right / scale, (float)target.rect.bottom / scale
                    );
                    hitFound = true;
                    break;
                }
            }
            if (!hitFound) {
                D2D1_RECT_F r;
                if (IsHovering("audio", &r)) { newText = L"Volume"; newRectF = r; hitFound = true; }
                else if (IsHovering("network", &r)) { newText = L"Network"; newRectF = r; hitFound = true; }
                else if (IsHovering("battery", &r)) { newText = L"Battery"; newRectF = r; hitFound = true; }
                else if (IsHovering("cpu", &r)) { newText = L"CPU Usage"; newRectF = r; hitFound = true; }
                else if (IsHovering("gpu", &r)) { newText = L"GPU Temperature"; newRectF = r; hitFound = true; }
                else if (IsHovering("ram", &r)) { newText = L"RAM Usage"; newRectF = r; hitFound = true; }
                else if (IsHovering("tray", &r)) { newText = L"System Tray"; newRectF = r; hitFound = true; }
                else if (IsHovering("notification", &r)) { newText = L"Notifications"; newRectF = r; hitFound = true; }
                else if (IsHovering("ping", &r)) {
                    Module *m = self->renderer->GetModule("ping");
                    PingModule *pm = static_cast<PingModule *>(m);

                    if (pm) {
                        std::string ip = pm->targetIP;
                        std::wstring w_ip(ip.begin(), ip.end());
                        newText = L"Ping Target: " + w_ip + L"\nLatency: " + std::to_wstring(pm->lastPing) + L"ms";
                    }
                    else newText = L"Latency";

                    newRectF = r;
                    hitFound = true;
                }
                else if (IsHovering("clock", &r)) {
                    SYSTEMTIME st; GetLocalTime(&st);
                    wchar_t buf[64];
                    swprintf_s(buf, L"%02d/%02d/%d", st.wMonth, st.wDay, st.wYear);
                    newText = buf;
                    newRectF = r;
                    hitFound = true;
                }
            }

            if (hitFound) {
                if (self->lastTooltipText != newText) {
                    self->lastTooltipText = newText;
                    RECT logRect = { (LONG)newRectF.left, (LONG)newRectF.top, (LONG)newRectF.right, (LONG)newRectF.bottom };
                    self->tooltips.Show(self->lastTooltipText.c_str(), logRect, self->renderer->theme.global.position, scale);
                }
            }
            else {
                if (!self->lastTooltipText.empty()) {
                    self->tooltips.Hide();
                    self->lastTooltipText.clear();
                }
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (self) {
            self->tooltips.Hide();
            self->lastTooltipText.clear();
            self->isTrackingMouse = false;
        }
        break;
    case WM_LBUTTONDOWN:
    {
        if (!self || !self->renderer) break;

        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        POINT pt = { mx, my };
        float dpi = (float)GetDpiForWindow(hwnd);
        float scale = dpi / 96.0f;
        RECT barRect;
        GetWindowRect(hwnd, &barRect);
        auto GetModuleScreenRect = [&](std::string id, RECT *outScreenRect) -> bool {
            D2D1_RECT_F f = self->renderer->GetModuleRect(id);
            if (f.right == 0.0f) return false;
            RECT localR = {
                (LONG)(f.left * scale),
                (LONG)(f.top * scale),
                (LONG)(f.right * scale),
                (LONG)(f.bottom * scale)
            };

            if (outScreenRect) {
                outScreenRect->left = barRect.left + localR.left;
                outScreenRect->top = barRect.top + localR.top;
                outScreenRect->right = barRect.left + localR.right;
                outScreenRect->bottom = barRect.top + localR.bottom;
            }
            return PtInRect(&localR, pt);
            };

        bool handled = false;
        RECT targetRect;

        if (GetModuleScreenRect("app_icon", &targetRect)) {
            SendMessage(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
            handled = true;
        }
        // Workspaces
        else if (GetModuleScreenRect("workspaces", &targetRect)) {
            Module *m = self->renderer->GetModule("workspaces");
            if (m) {
                WorkspacesModule *ws = (WorkspacesModule *)m;
                D2D1_RECT_F rectF = ws->cachedRect;
                float fullItemSize = ws->itemWidth + ws->itemPadding;

                int index = 0;
                std::string pos = self->renderer->theme.global.position;
                if (pos == "left" || pos == "right") {
                    float localY = ((float)pt.y / scale) - rectF.top;
                    index = (int)(localY / fullItemSize);
                }
                else {
                    float localX = ((float)pt.x / scale) - rectF.left;
                    index = (int)(localX / fullItemSize);
                }

                if (index >= 0 && index < ws->count) {
                    ws->SetActiveIndex(index);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            handled = true;
        }
        else if (GetModuleScreenRect("audio", &targetRect)) {
            if (self->flyout) self->flyout->Toggle(targetRect);
            handled = true;
        }
        else if (GetModuleScreenRect("tray", &targetRect)) {
            if (!self->trayFlyout) self->trayFlyout = new TrayFlyout(GetModuleHandle(NULL));
            self->trayFlyout->Toggle(targetRect);
            handled = true;
        }
        else if (GetModuleScreenRect("network", &targetRect)) {
            ShellExecute(NULL, L"open", L"ms-settings:network", NULL, NULL, SW_SHOWNORMAL);
            handled = true;
        }
		else if (GetModuleScreenRect("app_icon", &targetRect)) {
            SendMessage(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
            handled = true;
        }
        else if (GetModuleScreenRect("battery", &targetRect)) {
            ShellExecute(NULL, L"open", L"ms-settings:batterysaver", NULL, NULL, SW_SHOWNORMAL);
            handled = true;
        }
        else if (GetModuleScreenRect("notification", &targetRect)) {
            INPUT inputs[4] = {};
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_LWIN;
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'N';
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'N'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_LWIN; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, inputs, sizeof(INPUT));
            handled = true;
        }

        if (!handled) {
            for (const auto &target : self->windowTargets) {
                if (PtInRect(&target.rect, pt)) {
                    if (IsWindow(target.hwnd)) {
                        if (IsIconic(target.hwnd)) ShowWindow(target.hwnd, SW_RESTORE);
                        else ShowWindow(target.hwnd, SW_MINIMIZE);
                        SetForegroundWindow(target.hwnd);
                    }
                    handled = true;
                    break;
                }
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == 1) if (self) self->UpdateSystemStats();
        return 0;
    case WM_SIZE:
        if (self && self->renderer) {
            self->renderer->Resize();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMPOWERSTATUSCHANGE) InvalidateRect(hwnd, NULL, FALSE);
        return TRUE;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && self && self->renderer) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);

            if (self->renderer->HitTest(pt, self->windowTargets)) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_RAILING_AUDIO_UPDATE:
        if (self) { // Check self, not renderer yet
            int volPercent = (int)wParam;
            bool isMuted = (bool)lParam;
            self->cachedVolume = volPercent / 100.0f;
            self->cachedMute = isMuted;

            InvalidateRect(hwnd, NULL, FALSE);
            if (self->flyout && self->flyout->IsVisible())
                InvalidateRect(self->flyout->hwnd, NULL, FALSE);
        }
        return 0;
    case WM_RAILING_APPBAR: // Re-check size
		if (wParam == ABN_POSCHANGED) UpdateAppBarPosition(hwnd);
        return 0;
    case WM_DESTROY:
        UnregisterAppBar(hwnd);
        if (self && self->titleHook) {
            UnhookWinEvent(self->titleHook);
            self->titleHook = nullptr;
        }
        if (self->flyout) delete self->flyout;
        if (self->trayFlyout) delete self->trayFlyout;
		UnregisterHotKey(hwnd, HOTKEY_KILL_THIS);
        PostQuitMessage(0);
        return 0;
    default:
        if (uMsg == self->shellMsgId) {
            self->needsWindowRefresh = true; // Flag for next paint
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void Railing::WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (event == EVENT_OBJECT_NAMECHANGE && IsWindow(hwnd) && instance && instance->hwndBar)
        InvalidateRect(instance->hwndBar, nullptr, FALSE);
}

ULONGLONG Railing::GetInterval(std::string type, int def)
{
    Module *m = renderer->GetModule(type);
	return (m && m->config.interval > 0) ? (ULONGLONG)m->config.interval : (ULONGLONG)def;
}

void Railing::UpdateSystemStats() {
    ULONGLONG now = GetTickCount64();
    bool needsRepaint = false;

    if (now - lastCpuUpdate >= GetInterval("cpu", 1000)) { cachedCpuUsage = stats.GetCpuUsage(); lastCpuUpdate = now; needsRepaint = true; }
    if (now - lastRamUpdate >= GetInterval("ram", 1000)) { cachedRamUsage = stats.GetRamUsage(); lastRamUpdate = now; needsRepaint = true; }
	if (now - lastGpuUpdate >= GetInterval("gpu", 1000)) { cachedGpuTemp = gpuStats.GetGpuTemp(); lastGpuUpdate = now; needsRepaint = true; }

    // In V2 we actually just trigger te renderer to update its modules!
    // For now, simple repaint is enough.

    if (needsRepaint) InvalidateRect(hwndBar, NULL, FALSE);
}

void Railing::DrawBar(HWND hwnd) {
    HWND foreground = GetForegroundWindow();
    windowTargets.clear();

    if (!renderer) renderer = new RailingRenderer(hwnd);
    RECT rc; GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    RailingRenderer::SystemStatusData statsData;
    statsData.cpuUsage = cachedCpuUsage;
    statsData.ramUsage = cachedRamUsage;
    statsData.gpuTemp = cachedGpuTemp;
    statsData.volume = cachedVolume;
    statsData.isMuted = cachedMute;

    renderer->UpdateStats(statsData);
    renderer->Draw(allWindows, foreground, windowTargets);
}

void Railing::GetTopLevelWindows(std::vector<WindowInfo> &outWindows)
{
    outWindows.clear();
    std::pair<Railing *, std::vector<WindowInfo> *> params(this, &outWindows);

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *ctx = reinterpret_cast<std::pair<Railing *, std::vector<WindowInfo> *> *>(lParam);
        Railing *self = ctx->first;
        auto *out = ctx->second;


        wchar_t title[256];
        GetWindowText(hwnd, title, 256);
        if (title[0] == L'\0') return TRUE;

        if (!self->IsAppWindow(hwnd)) return TRUE;

        RECT rect;
        GetWindowRect(hwnd, &rect);

        out->push_back({ hwnd, title, rect });
        return TRUE;
        }, reinterpret_cast<LPARAM>(&params));
}

BOOL Railing::IsAppWindow(HWND hwnd)         
{
    if (!IsWindowVisible(hwnd)) return FALSE;
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (style & WS_EX_TOOLWINDOW) return FALSE;

    wchar_t className[256];
    GetClassName(hwnd, className, 256);

    int cloakedVal = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloakedVal, sizeof(cloakedVal));
    if (SUCCEEDED(hr) && cloakedVal != 0) return FALSE;

    if (GetWindow(hwnd, GW_OWNER) != NULL && !(style & WS_EX_APPWINDOW)) return FALSE;

    if (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"Shell_TrayWnd") == 0) return FALSE;

    return TRUE;
}