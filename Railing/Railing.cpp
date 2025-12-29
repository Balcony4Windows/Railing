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

void UpdateBarPosition(HWND hwnd)
{
    POINT pt = { 0, 0 };
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    GetMonitorInfo(hMon, &mi);

    int monW = mi.rcMonitor.right - mi.rcMonitor.left;
    int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;
    ThemeConfig theme = ThemeLoader::Load("config.json");
    std::string pos = theme.global.position; // "top", "bottom", "left", "right"
    int barThickness = theme.global.height;  // This is "thickness"

    int x = mi.rcMonitor.left;
    int y = mi.rcMonitor.top;
    int w = monW;
    int h = barThickness;

    if (pos == "bottom") {
        y = mi.rcMonitor.bottom - barThickness;
    }
    else if (pos == "left") {
        w = barThickness;
        h = monH;
    }
    else if (pos == "right") {
        x = mi.rcMonitor.right - barThickness;
        w = barThickness;
        h = monH;
    }
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd, NULL, TRUE);
}

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
        WS_POPUP | WS_VISIBLE,
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
                else if (IsHovering("ram", &r)) { newText = L"RAM Usage"; newRectF = r; hitFound = true; }
                else if (IsHovering("tray", &r)) { newText = L"System Tray"; newRectF = r; hitFound = true; }
                else if (IsHovering("notification", &r)) { newText = L"Notifications"; newRectF = r; hitFound = true; }
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
                    self->tooltips.Show(self->lastTooltipText.c_str(), logRect, scale);
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

        bool handled = false;

        RECT iconRect = self->renderer->GetAppIconRect();
        if (PtInRect(&iconRect, pt)) {
            SendMessage(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
            return 0;
        }

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
        if (handled) break;

        std::string pos = self->renderer->theme.global.position;
        auto IsModuleClicked = [&](std::string id) -> bool {
            D2D1_RECT_F f = self->renderer->GetModuleRect(id);
            RECT r = { (LONG)(f.left * scale), (LONG)(f.top * scale), (LONG)(f.right * scale), (LONG)(f.bottom * scale) };
            return PtInRect(&r, pt);
            };

        if (IsModuleClicked("audio")) {
            RECT barRect; GetWindowRect(hwnd, &barRect);
            D2D1_RECT_F volF = self->renderer->GetModuleRect("audio");

            int anchorX = 0;
            int anchorY = 0;

            if (pos == "left") {
                anchorX = barRect.right + 100;
                anchorY = barRect.top + (int)(volF.top * scale);
            }
            else if (pos == "right") {
                anchorX = barRect.left - 300; // Subtract approx width of flyout
                anchorY = barRect.top + (int)(volF.top * scale);
            }
            else if (pos == "bottom") {
                anchorX = barRect.left + (int)(volF.left * scale);
                anchorY = barRect.top - 300; // Subtract height
            }
            else {
                anchorX = barRect.left + (int)(volF.left * scale);
                anchorY = barRect.bottom;
            }

            if (!self->flyout) self->flyout = new VolumeFlyout(GetModuleHandle(NULL));
            self->flyout->Toggle(anchorX, anchorY);
            handled = true;
        }
        else if (IsModuleClicked("tray")) { // Tray Click
            RECT barRect; GetWindowRect(hwnd, &barRect);
            D2D1_RECT_F arrowF = self->renderer->GetModuleRect("tray");
            int anchorX = 0;
            int anchorY = 0;

            if (pos == "left") {
                anchorX = barRect.right;
                anchorY = barRect.top + (int)(arrowF.top * scale);
            }
            else if (pos == "right") {
                anchorX = barRect.left - 300; // Subtract approx width of flyout
                anchorY = barRect.top + (int)(arrowF.top * scale);
            }
            else if (pos == "bottom") {
                anchorX = barRect.left + (int)(arrowF.left * scale);
                anchorY = barRect.top - 300; // Subtract height
            }
            else {
                anchorX = barRect.left + (int)(arrowF.left * scale);
                anchorY = barRect.bottom;
            }
            if (self->trayFlyout) self->trayFlyout->Toggle(anchorX, anchorY);
            handled = true;
        }
        else if (IsModuleClicked("network")) { // Network Click
            ShellExecute(NULL, L"open", L"ms-settings:network", NULL, NULL, SW_SHOWNORMAL);
            handled = true;
        }
        else if (IsModuleClicked("workspaces")) { // Workspaces Click
            Module *m = self->renderer->GetModule("workspaces");
            if (m) {
                WorkspacesModule *ws = (WorkspacesModule *)m;
                D2D1_RECT_F rectF = ws->cachedRect;

                float fullItemSize = ws->itemWidth + ws->itemPadding;
                int index = 0;

                // FIX: Check Orientation
                if (pos == "left" || pos == "right") {
                    // Vertical: Calculate offset using Y
                    float localY = ((float)pt.y / scale) - rectF.top;
                    index = (int)(localY / fullItemSize);
                }
                else {
                    // Horizontal: Calculate offset using X
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
        else if (IsModuleClicked("notification")) {
            INPUT inputs[4] = {};
            inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_LWIN;
            inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'N';
            inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'N'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_LWIN; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, inputs, sizeof(INPUT));
            handled = true;
        }
        else if (IsModuleClicked("battery")) {
            ShellExecute(NULL, L"open", L"ms-settings:batterysaver", NULL, NULL, SW_SHOWNORMAL);
            handled = true;
        }
        else if (IsModuleClicked("app_icon")) {
            SendMessage(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
            handled = true;
        }
    }
    return 0;
    case WM_TIMER:
        if (wParam == 1) if (self) self->UpdateSystemStats();
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
    case WM_DESTROY:
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