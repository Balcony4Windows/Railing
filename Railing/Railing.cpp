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
#include "CommandExecutor.h"
#include <algorithm>

#pragma comment(lib, "dwmapi.lib") 
#pragma comment(lib, "d2d1")
#pragma comment(lib, "shell32.lib")

Railing::Railing() {}
Railing::~Railing() = default;
Railing *Railing::instance = nullptr;
UINT WM_SHELLHOOKMESSAGE = RegisterWindowMessageW(L"SHELLHOOK");

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
            if (renderer) renderer->Reload();

            UnregisterAppBar(hwndBar); // Re-register for size/position changes
            RegisterAppBar(hwndBar);
            UpdateAppBarPosition(hwndBar, cachedConfig);
            InvalidateRect(hwndBar, NULL, FALSE);
        }
    }
}

bool Railing::Initialize(HINSTANCE hInstance)
{
    SetThreadDescription(GetCurrentThread(), L"Railing_MainUI");
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return false;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    instance = this;
    hInst = hInstance;
    stats.Initialize();
    gpuStats.Initialize();

    cachedConfig = ThemeLoader::Load("config.json");
    this->pinnedApps = cachedConfig.pinnedPaths;
    if (cachedConfig.global.blur) cachedConfig.global.radius = 1.0f; // This is fixed by Windows :(
    hwndBar = CreateBarWindow(hInstance, cachedConfig);
    if (!hwndBar) return false;
    tooltips.Initialize(hwndBar);
    CheckForConfigUpdate(); // Initial load
    GetTopLevelWindows(allWindows);

    for (const auto &win : allWindows) workspaces.AddWindow(win.hwnd);
    for (int i = 0; i < 5; i++) RegisterHotKey(hwndBar, 100 + i, MOD_ALT | MOD_NOREPEAT, 0x31 + i);

    RegisterAppBar(hwndBar);
    UpdateAppBarPosition(hwndBar, cachedConfig);

    RECT targetRect;
    GetWindowRect(hwndBar, &targetRect); // UpdateAppBarPosition calculated this for us
    int finalW = targetRect.right - targetRect.left;
    int finalH = targetRect.bottom - targetRect.top;
    int centerX = targetRect.left + (finalW / 2);
    int centerY = targetRect.top + (finalH / 2);
    SendMessage(hwndBar, WM_PAINT, 0, 0);
    ShowWindow(hwndBar, SW_SHOW);
    auto &anim = cachedConfig.global.animation;
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
    UpdateAppBarPosition(hwndBar, cachedConfig);

    // Register shell hooks
    RegisterShellHookWindow(hwndBar);
    UINT shellHookMsg = RegisterWindowMessage(L"SHELLHOOK");
    shellMsgId = shellHookMsg;

    RegisterHotKey(hwndBar, HOTKEY_KILL_THIS, MOD_CONTROL | MOD_SHIFT, 0x51); // Ctrl + Shft + Q to kill explorer (for testing)
    titleHook = SetWinEventHook(
        EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE,
        nullptr, Railing::WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    SetTimer(hwndBar, 1, 500, NULL);
    SetTimer(hwndBar, 2, 16, NULL); // Animation/autohide (60 fps)

    flyout = new VolumeFlyout(hInstance,
        renderer->GetFactory(),
        renderer->GetWriteFactory(),
        renderer->theme);
    flyout->audio.EnsureInitialized(hwndBar);
    trayFlyout = new TrayFlyout(hInstance, renderer->GetFactory(), renderer->GetWICFactory(), renderer->theme);


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
        else if (wParam >= 100 && wParam <= 104 && self) {
            int idx = (int)wParam - 100;
            self->workspaces.SwitchWorkspace(idx);
            Module *m = self->renderer->GetModule("workspaces");
            if (m) static_cast<WorkspacesModule *>(m)->SetActiveIndex(idx);
            self->GetTopLevelWindows(self->allWindows);
            InvalidateRect(hwnd, NULL, FALSE);
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
    case WM_MOUSEWHEEL:
        if (self && self->renderer) {
            if (GetKeyState(VK_MENU) & 0x8000) {

                Module *m = self->renderer->GetModule("workspaces");
                if (m) {
                    WorkspacesModule *ws = (WorkspacesModule *)m;

                    short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                    int current = self->workspaces.currentWorkspace;
                    int count = ws->count;
                    int next = current;
                    if (delta > 0) next--; else next++;
                    if (next < 0) next = count - 1;
                    if (next >= count) next = 0;

                    if (next != current) {
                        self->workspaces.SwitchWorkspace(next);
                        ws->SetActiveIndex(next);
                        self->GetTopLevelWindows(self->allWindows);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            }
        }
        break;
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
            Module *hitModule = nullptr;
            auto IsHovering = [&](const char *id, D2D1_RECT_F *outRect) -> bool {
                D2D1_RECT_F r = self->renderer->GetModuleRect(id);
                if (r.right == 0.0f && r.bottom == 0.0f) return false;

                RECT phys = {
                    (LONG)(r.left * scale), (LONG)(r.top * scale),
                    (LONG)(r.right * scale), (LONG)(r.bottom * scale)
                };
                if (PtInRect(&phys, pt)) {
                    if (outRect) *outRect = r;
                    return true;
                }
                return false;
                };

            bool needsRepaint = false;

            for (auto const &[id, cfg] : self->renderer->theme.modules) {
                Module *m = self->renderer->GetModule(id);
                if (!m) continue;

                D2D1_RECT_F currentRect;
                bool isOver = IsHovering(id.c_str(), &currentRect);

                if (isOver) {
                    bool isGroup = m->config.type == "group";
                    bool existingIsSpecific = (hitModule && hitModule->config.type != "group");
                    if (!hitModule || !existingIsSpecific || !isGroup) {
                        hitModule = m;
                        newRectF = currentRect;
                    }
                }

                if (m->config.type == "workspaces") {
                    WorkspacesModule *ws = (WorkspacesModule *)m;
                    int newHoverIdx = -1;
                    if (isOver) {
                        float fullItemSize = ws->itemWidth + ws->itemPadding;
                        std::string pos = self->renderer->theme.global.position;
                        if (pos == "left" || pos == "right") {
                            float localY = ((float)pt.y / scale) - currentRect.top;
                            newHoverIdx = (int)(localY / fullItemSize);
                        }
                        else {
                            float localX = ((float)pt.x / scale) - currentRect.left;
                            newHoverIdx = (int)(localX / fullItemSize);
                        }
                        if (newHoverIdx < 0 || newHoverIdx >= ws->count) newHoverIdx = -1;
                    }
                    if (ws->hoveredIndex != newHoverIdx) {
                        ws->SetHoveredIndex(newHoverIdx);
                        needsRepaint = true;
                    }
                }
                else {
                    if (m->isHovered != isOver) {
                        m->isHovered = isOver;
                        needsRepaint = true;
                    }
                }
            }

            if (needsRepaint) InvalidateRect(hwnd, NULL, FALSE);

            if (hitModule) {
                std::string type = hitModule->config.type;

                if (type == "audio") newText = L"Volume";
                else if (type == "network") newText = L"Network";
                else if (type == "battery") newText = L"Battery";
                else if (type == "cpu") newText = L"CPU Usage";
                else if (type == "gpu") newText = L"GPU Temperature";
                else if (type == "ram") newText = L"RAM Usage";
                else if (type == "tray") newText = L"System Tray";
                else if (type == "notification") newText = L"Notifications";
                else if (type == "weather") newText = L"Weather Near Me";
                else if (type == "dock") {
                    DockModule *dock = (DockModule *)hitModule;
                    float localX = ((float)pt.x / scale) - newRectF.left;
                    float modW = newRectF.right - newRectF.left;
                    Style s = dock->GetEffectiveStyle();
                    float iSize = (dock->config.dockIconSize > 0) ? dock->config.dockIconSize : 24.0f;
                    float iSpace = (dock->config.dockSpacing > 0) ? dock->config.dockSpacing : 8.0f;

                    size_t count = dock->GetCount();
                    if (count > 0) {
                        float totalIconWidth = (count * iSize) + ((count - 1) * iSpace);
                        float bgWidth = modW - s.margin.left - s.margin.right;
                        float startX = s.margin.left + ((bgWidth - totalIconWidth) / 2.0f);

                        if (localX >= startX && localX <= (startX + totalIconWidth)) {
                            float offset = localX - startX;
                            int index = (int)(offset / (iSize + iSpace));
                            float relativePos = fmod(offset, (iSize + iSpace));

                            if (relativePos <= iSize && index >= 0 && index < count)
                                newText = dock->GetTitleAtIndex(index);
                        }
                    }
                }
                else if (type == "clock") {
                    SYSTEMTIME st; GetLocalTime(&st);
                    wchar_t buf[64];
                    swprintf_s(buf, L"%02d/%02d/%d", st.wMonth, st.wDay, st.wYear);
                    newText = buf;
                }
                else if (type == "ping") {
                    PingModule *pm = static_cast<PingModule *>(hitModule);
                    std::string ip = pm->targetIP;
                    std::wstring w_ip(ip.begin(), ip.end());
                    newText = L"Ping Target: " + w_ip + L"\nLatency: " + std::to_wstring(pm->lastPing) + L"ms";
                }
                // Custom modules or others default to empty, or you can add a description field later
            }
            if (!newText.empty()) {
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
            if (self->renderer) {
                for (Module *m : self->renderer->leftModules) { m->isHovered = false; }
                for (Module *m : self->renderer->centerModules) { m->isHovered = false; }
                for (Module *m : self->renderer->rightModules) { m->isHovered = false; }

                // Specific reset for workspaces
                Module *ws = self->renderer->GetModule("workspaces");
                if (ws) ((WorkspacesModule *)ws)->SetHoveredIndex(-1);

                InvalidateRect(hwnd, NULL, FALSE);
            }
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
        auto IsMouseOverModule = [&](std::string id, RECT *outScreenRect) -> bool {
            D2D1_RECT_F f = self->renderer->GetModuleRect(id);
            if (f.right == 0.0f && f.bottom == 0.0f) return false;

            RECT localR = {
                (LONG)(f.left * scale), (LONG)(f.top * scale),
                (LONG)(f.right * scale), (LONG)(f.bottom * scale)
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
        RECT targetRect = { 0 };

        for (auto const &[id, cfg] : self->renderer->theme.modules) {

            if (IsMouseOverModule(id, &targetRect)) {
                Module *m = self->renderer->GetModule(id);
                if (!m) continue;

                std::string type = m->config.type;
                if (type == "workspaces") {
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
                        self->workspaces.SwitchWorkspace(index); // Update logic
                        ws->SetActiveIndex(index); // Update visuals
                        self->GetTopLevelWindows(self->allWindows);
                        InvalidateRect(hwnd, NULL, FALSE); // force a refresh
                    }
                    if (ws->hoveredIndex != index) {
                        ws->hoveredIndex = index;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    handled = true;
                }
                else if (type == "app_icon") {
                    SendMessage(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
                    handled = true;
                }
                else if (type == "audio") {
                    if (self->flyout) self->flyout->Toggle(targetRect);
                    handled = true;
                }
                else if (type == "tray") {
                    if (!self->trayFlyout) {
                        self->trayFlyout = new TrayFlyout(
                            self->hInst,
                            self->renderer->GetFactory(),
                            self->renderer->GetWICFactory(),
                            self->renderer->theme
                        );
                    }
                    self->trayFlyout->Toggle(targetRect);
                    handled = true;
                }
                else if (type == "network") {
                    CommandExecutor::Execute("shell:ms-settings:network", hwnd);
                    handled = true;
                }
                else if (type == "battery") {
                    ShellExecute(NULL, L"open", L"ms-settings:batterysaver", NULL, NULL, SW_SHOWNORMAL);
                    handled = true;
                }
                else if (type == "ping" || type == "weather") {
                    std::string action = m->config.onClick;
                    if (!action.empty()) {
                        CommandExecutor::Execute(action, hwnd);
                        handled = true;
                    }
                }
                else if (type == "notification") {
                    CommandExecutor::Execute("notification", hwnd);
                    handled = true;
                }
                else if (type == "dock") {
                    DockModule *dock = (DockModule *)m;

                    D2D1_RECT_F modRect = self->renderer->GetModuleRect(id);
                    float modW = modRect.right - modRect.left;
                    float clickX = ((float)pt.x / scale) - modRect.left;

                    Style s = dock->GetEffectiveStyle();
                    float iSize = (dock->config.dockIconSize > 0) ? dock->config.dockIconSize : 24.0f;
                    float iSpace = (dock->config.dockSpacing > 0) ? dock->config.dockSpacing : 8.0f;

                    int count = dock->GetCount();

                    if (count > 0) {
                        float totalIconWidth = (count * iSize) + ((count - 1) * iSpace);
                        float bgWidth = modW - s.margin.left - s.margin.right;
                        float startX = s.margin.left + ((bgWidth - totalIconWidth) / 2.0f);

                        if (clickX >= startX && clickX <= (startX + totalIconWidth)) {
                            float offset = clickX - startX;
                            int index = (int)(offset / (iSize + iSpace));
                            float relativePos = fmod(offset, (iSize + iSpace));

                            if (relativePos <= iSize && index >= 0 && index < count) {
                                WindowInfo target = dock->GetWindowInfoAtIndex(index);

                                if (target.hwnd == NULL) {
                                    ShellExecuteW(NULL, L"open", target.exePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                                }
                                else {
                                    if (self->workspaces.managedWindows.count(target.hwnd)) {
                                        int targetWksp = self->workspaces.managedWindows[target.hwnd];

                                        if (targetWksp != self->workspaces.currentWorkspace) {
                                            self->workspaces.SwitchWorkspace(targetWksp);
                                            Module *wsMod = self->renderer->GetModule("workspaces");
                                            if (wsMod) ((WorkspacesModule *)wsMod)->SetActiveIndex(targetWksp);

                                            self->GetTopLevelWindows(self->allWindows);
                                            InvalidateRect(hwnd, NULL, FALSE);
                                        }
                                    }

                                    if (target.hwnd == ::GetForegroundWindow()) {
                                        ::ShowWindow(target.hwnd, SW_MINIMIZE);
                                    }
                                    else {
                                        if (::IsIconic(target.hwnd)) ::ShowWindow(target.hwnd, SW_RESTORE);
                                        ::SetForegroundWindow(target.hwnd);
                                    }
                                }
                            }
                        }
                    }
                    handled = true;
                }
                else if (type == "custom") {
                    std::string action = m->config.onClick;
                    if (!action.empty()) {
                        CommandExecutor::Execute(action, hwnd);
                        handled = true;
                    }
                }

                if (handled) break;
            }
        }

        return 0;
    }
case WM_RBUTTONUP:
{
    if (!self || !self->renderer) break;

    float scale = GetDpiForWindow(hwnd) / 96.0f;
    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    Module *hitModule = nullptr;
    D2D1_RECT_F modRect = { 0 };

    for (auto const &[id, cfg] : self->renderer->theme.modules) {
        D2D1_RECT_F f = self->renderer->GetModuleRect(id);
        if (f.right == 0.0f) continue;

        RECT localR = { (LONG)(f.left * scale), (LONG)(f.top * scale), (LONG)(f.right * scale), (LONG)(f.bottom * scale) };
        if (PtInRect(&localR, pt)) {
            hitModule = self->renderer->GetModule(id);
            modRect = f;
            break;
        }
    }

    if (!hitModule || hitModule->config.type != "dock") break;
    DockModule *dock = (DockModule *)hitModule;

    int count = dock->GetCount();
    if (count == 0) break;

    Style containerStyle = dock->GetEffectiveStyle();
    Style itemStyle = dock->config.itemStyle; // Use itemStyle for padding math
    float itemBoxWidth = dock->config.dockIconSize + itemStyle.padding.left + itemStyle.padding.right;
    float itemSpacing = itemStyle.margin.left + itemStyle.margin.right;
    if (dock->config.dockSpacing > 0) itemSpacing = dock->config.dockSpacing;
    float totalWidth = (count * itemBoxWidth) + ((count - 1) * itemSpacing);
    float modW = modRect.right - modRect.left;
    float startX = containerStyle.margin.left + ((modW - containerStyle.margin.left - containerStyle.margin.right - totalWidth) / 2.0f);
    float mouseX = ((float)pt.x / scale) - modRect.left;
    if (mouseX >= startX && mouseX <= (startX + totalWidth)) {
        float relativeX = mouseX - startX;
        float fullItemStride = itemBoxWidth + itemSpacing;
        int idx = (int)(relativeX / fullItemStride);

        if (idx >= 0 && idx < count) {
            WindowInfo targetWin = dock->GetWindowInfoAtIndex(idx);
            HMENU hMenu = CreatePopupMenu();
            std::wstring title = targetWin.title.empty() ? L"Application" : targetWin.title;
            if (title == L"Application" && !targetWin.exePath.empty()) {
                size_t ls = targetWin.exePath.find_last_of(L"\\/");
                if (ls != std::wstring::npos) title = targetWin.exePath.substr(ls + 1);
            }

            AppendMenuW(hMenu, MF_STRING | MF_DISABLED, 0, title.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 100, targetWin.hwnd ? L"Open New Window" : L"Launch");

            bool isPinned = false;
            for (auto &p : self->pinnedApps) {
                if (!p.empty() && p == targetWin.exePath) isPinned = true;
            }
            AppendMenuW(hMenu, MF_STRING, 101, isPinned ? L"Unpin" : L"Pin");

            if (targetWin.hwnd) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 102, L"Close");
            }
            POINT screenPt = pt; ClientToScreen(hwnd, &screenPt);
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, screenPt.x, screenPt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);

            if (cmd == 100) { // Launch / Open
                std::wstring path = targetWin.exePath;
                if (path.empty() && targetWin.hwnd) path = Railing::GetWindowExePath(targetWin.hwnd);
                if (!path.empty()) ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            else if (cmd == 101) { // Pin / Unpin
                std::wstring path = targetWin.exePath;
                if (path.empty() && targetWin.hwnd) path = Railing::GetWindowExePath(targetWin.hwnd);

                auto it = std::find(self->pinnedApps.begin(), self->pinnedApps.end(), path);
                if (it != self->pinnedApps.end()) self->pinnedApps.erase(it);
                else self->pinnedApps.push_back(path);

                self->SavePinnedApps();
                self->GetTopLevelWindows(self->allWindows); // Refresh Data
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (cmd == 102) { // Close
                if (targetWin.hwnd) PostMessage(targetWin.hwnd, WM_CLOSE, 0, 0);
            }
        }
    }
}
return 0;
case WM_TIMER:
    if (wParam == 1 && self) self->UpdateSystemStats();

    if (wParam == 2 && self && self->cachedConfig.global.autoHide) {
        POINT pt; GetCursorPos(&pt);
        RECT barRect; GetWindowRect(hwnd, &barRect);

        std::string pos = self->cachedConfig.global.position;
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        if (pos == "top")    barRect.top = 0;
        if (pos == "bottom") barRect.bottom = screenH;
        if (pos == "left")   barRect.left = 0;
        if (pos == "right")  barRect.right = screenW;

        bool mouseOver = PtInRect(&barRect, pt);
        bool flyoutOpen = (self->flyout && self->flyout->IsVisible()) ||
            (self->trayFlyout && self->trayFlyout->IsVisible());

        bool shouldShow = false;

        if (flyoutOpen || mouseOver || self->IsMouseAtEdge()) {
            shouldShow = true;
            self->lastInteractionTime = GetTickCount64();
        }
        else {
            ULONGLONG diff = GetTickCount64() - self->lastInteractionTime;
            if (diff < self->cachedConfig.global.autoHideDelay) shouldShow = true;
        }

        float oldProgress = self->showProgress; // Capture state before update
        float speed = 0.15f;
        if (shouldShow) {
            self->showProgress += speed;
            if (self->showProgress > 1.0f) self->showProgress = 1.0f;
        }
        else {
            self->showProgress -= speed;
            if (self->showProgress < 0.0f) self->showProgress = 0.0f;
        }

        bool progressChanged = (self->showProgress != oldProgress);
        bool stateChanged = (shouldShow != !self->isHidden);

        if (progressChanged || stateChanged) {

            self->isHidden = !shouldShow;

            float dpi = (float)GetDpiForWindow(hwnd);
            float scale = dpi / 96.0f;
            int h = (int)(self->cachedConfig.global.height * scale); // Height

            int mTop = (int)(self->cachedConfig.global.margin.top * scale);
            int mBottom = (int)(self->cachedConfig.global.margin.bottom * scale);
            int mLeft = (int)(self->cachedConfig.global.margin.left * scale);
            int mRight = (int)(self->cachedConfig.global.margin.right * scale);
            int x = 0, y = 0, w = 0;

            if (pos == "bottom") {
                int shownY = screenH - mBottom - h;
                int hiddenY = screenH;

                y = (int)(hiddenY + (shownY - hiddenY) * self->showProgress);

                x = mLeft;
                w = screenW - mLeft - mRight;
            }
            else if (pos == "top") {
                int shownY = mTop;
                int hiddenY = -h;

                y = (int)(hiddenY + (shownY - hiddenY) * self->showProgress);

                x = mLeft;
                w = screenW - mLeft - mRight;
            }
            // TODO: Add Left/Right logic here
            SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }
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

            float dpi = (float)GetDpiForWindow(hwnd);
            float scale = dpi / 96.0f;
            bool hit = false;

            RECT barRect; GetWindowRect(hwnd, &barRect);
            for (auto const &[id, cfg] : self->renderer->theme.modules) {
                D2D1_RECT_F f = self->renderer->GetModuleRect(id);
                if (f.right == 0.0f && f.bottom == 0.0f) continue;
                RECT localR = {
                    (LONG)(f.left * scale), (LONG)(f.top * scale),
                    (LONG)(f.right * scale), (LONG)(f.bottom * scale)
                };
                if (PtInRect(&localR, pt)) {
                    hit = true;
                    break;
                }
            }

            if (hit) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
            }
            else {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
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
        if (wParam == ABN_POSCHANGED) UpdateAppBarPosition(hwnd, self->cachedConfig);
        return 0;
    case WM_DESTROY:
        UnregisterAppBar(hwnd);
        if (self && self->titleHook) {
            UnhookWinEvent(self->titleHook);
            self->titleHook = nullptr;
        }
        if (self->flyout) DestroyWindow(self->flyout->hwnd);
        if (self->trayFlyout) delete self->trayFlyout;
        UnregisterHotKey(hwnd, HOTKEY_KILL_THIS);
        PostQuitMessage(0);
        return 0;
    default:
        if (uMsg == self->shellMsgId) {
            int code = (int)wParam;
            HWND targetHwnd = (HWND)lParam;
            
            switch (code) {
            case HSHELL_WINDOWCREATED:
                if (self->IsAppWindow(targetHwnd)) {
                    self->workspaces.AddWindow(targetHwnd);
                }
                break;
            case HSHELL_WINDOWDESTROYED: {
                    self->workspaces.RemoveWindow(targetHwnd);
                    Module *m = self->renderer->GetModule("dock");
                    if (m) ((DockModule *)m)->ClearAttention(targetHwnd);
                    break;
                }
            case HSHELL_FLASH:
            case HSHELL_RUDEAPPACTIVATED: {
                Module *m = self->renderer->GetModule("dock");
                if (m) ((DockModule *)m)->SetAttention(targetHwnd, true);
            }
            break;
            case HSHELL_REDRAW: {
                    Module *m = self->renderer->GetModule("dock");
                    if (m) ((DockModule *)m)->InvalidateIcon(targetHwnd);
                    break;
                }
            }

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
    if (needsRepaint) InvalidateRect(hwndBar, NULL, FALSE);
}

void Railing::DrawBar(HWND hwnd) {
    HWND foreground = GetForegroundWindow();

    if (!renderer) {
        renderer = new RailingRenderer(hwnd, cachedConfig);
        renderer->pWorkspaceManager = &workspaces;
    }
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
    renderer->Draw(allWindows, pinnedApps, foreground);
}

void Railing::GetTopLevelWindows(std::vector<WindowInfo> &outWindows)
{
    std::vector<WindowInfo> runningWindows;
    std::pair<Railing *, std::vector<WindowInfo> *> params(this, &runningWindows);

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *ctx = reinterpret_cast<std::pair<Railing *, std::vector<WindowInfo> *> *>(lParam);
        Railing *self = ctx->first;
        auto *list = ctx->second;

        if (!IsWindowVisible(hwnd)) return TRUE;
        wchar_t title[256];
        GetWindowText(hwnd, title, 256);
        if (title[0] == L'\0') return TRUE;

        if (self->IsAppWindow(hwnd)) {
            std::wstring exe = GetWindowExePath(hwnd);
            RECT r; GetWindowRect(hwnd, &r);
            list->push_back({ hwnd, title, r, exe, false });
        }
        return TRUE;
        }, reinterpret_cast<LPARAM>(&params));

    std::vector<WindowInfo> finalList;

    for (const auto &pinPath : pinnedApps) {
        bool foundRunning = false;
        auto it = std::find_if(runningWindows.begin(), runningWindows.end(),
            [&pinPath](const WindowInfo &w) { return w.exePath == pinPath; });

        if (it != runningWindows.end()) {
            it->isPinned = true;
            finalList.push_back(*it);
            runningWindows.erase(it);
        }
        else {
            WindowInfo ghost;
            ghost.hwnd = NULL;
            ghost.title = L"";
            ghost.exePath = pinPath;
            ghost.isPinned = true;
            finalList.push_back(ghost);
        }
    }

    finalList.insert(finalList.end(), runningWindows.begin(), runningWindows.end());
    outWindows = finalList;
}

bool Railing::IsMouseAtEdge()
{
    POINT pt; GetCursorPos(&pt);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    std::string pos = cachedConfig.global.position;
    int tolerance = 2; // How close to the edge

    if (pos == "bottom") return pt.y >= screenH - tolerance;
    else if (pos == "top") return pt.y <= tolerance;
    else if (pos == "left") return pt.x <= tolerance;
    else if (pos == "right") return pt.x >= screenW - tolerance;
    else return false;
}

BOOL Railing::IsAppWindow(HWND hwnd)
{
    // 1. Basic Validity
    if (!IsWindow(hwnd)) return FALSE;
    if (!IsWindowVisible(hwnd)) return FALSE;

    // 2. Cloaked Check (Windows 8/10/11 DWM)
    int cloakedVal = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloakedVal, sizeof(cloakedVal));
    if (SUCCEEDED(hr) && cloakedVal != 0) return FALSE;

    // 3. Styles
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);

    // RULE A: If explicit AppWindow, always show it (even if owned).
    if (exStyle & WS_EX_APPWINDOW) return TRUE;

    // RULE B: If explicit ToolWindow, always hide it.
    if (exStyle & WS_EX_TOOLWINDOW) return FALSE;

    // RULE C: Ownership Check (The "Strict" Rule)
    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner != NULL) {
        return FALSE;
    }

    // RULE D: The "Physical" Check (Anti-Electron)
    RECT rect;
    GetWindowRect(hwnd, &rect);
    if ((rect.right - rect.left) < 20 || (rect.bottom - rect.top) < 20) {
        return FALSE;
    }

    if (hwnd == this->hwndBar) return FALSE;

    char className[256];
    GetClassNameA(hwnd, className, 256);
    if (strcmp(className, "Progman") == 0) return FALSE; // Desktop
    if (strcmp(className, "Shell_TrayWnd") == 0) return FALSE; // Native Taskbar

    return TRUE;
}