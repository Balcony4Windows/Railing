#include "InputManager.h"
#include "ModulesConcrete.h"
#include "CommandExecutor.h"
#include <windowsx.h>
#include "WindowMonitor.h"
#include "Railing.h"

InputManager::InputManager(Railing *a, RailingRenderer *rr, TooltipHandler *t)
	: app(a), renderer(rr), tooltips(t) { }

void InputManager::HandleMouseMove(HWND hwnd, int x, int y)
{
	if (!isTrackingMouse) {
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
		TrackMouseEvent(&tme);
		isTrackingMouse = true;
	}
    float dpi = (float)GetDpiForWindow(app->hwndBar);
    float scale = dpi / 96.0f;

	D2D1_RECT_F rectF;
	Module *hitModule = HitTest(x, y, rectF);
    std::wstring newText = L"";
    bool needsRepaint = false;

    if (renderer) {
        for (auto const &[id, cfg] : renderer->theme.modules) {
            Module *m = renderer->GetModule(id);
            if (!m) continue;

            bool isOver = (m == hitModule);
            
            if (m->config.type == "workspaces") {
                WorkspacesModule *ws = (WorkspacesModule *)m;
                int newHoverIdx = -1;
                if (isOver) {
                    float fullItemSize = ws->itemWidth + ws->itemPadding;
                    std::string pos = renderer->theme.global.position;

                    if (pos == "left" || pos == "right") {
                        float localY = ((float)y / scale) - rectF.top;
                        newHoverIdx = (int)(localY / fullItemSize);
                    }
                    else {
                        float localX = ((float)x / scale) - rectF.left;
                        newHoverIdx = (int)(localX / fullItemSize);
                    }
                    if (newHoverIdx < 0 || newHoverIdx >= ws->count) newHoverIdx = -1;
                }
                if (ws->hoveredIndex != newHoverIdx) {
                    ws->SetHoveredIndex(newHoverIdx);
                    needsRepaint = true;
                }
            }
            else if(m->isHovered != isOver) {
                    m->isHovered = isOver;
                    needsRepaint = true;
            }
        }
    }

    if (hitModule) { // Tooltip logic
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
        else if (type == "dock") {
            DockModule *dock = (DockModule *)hitModule;
            float modW = rectF.right - rectF.left;
            float localX = ((float)x / scale) - rectF.left;
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
    }
    if (needsRepaint) InvalidateRect(hwnd, NULL, FALSE);

    if (!newText.empty() && lastTooltipText != newText) {
        lastTooltipText = newText;
        RECT logRect = { (LONG)rectF.left, (LONG)rectF.top, (LONG)rectF.right, (LONG)rectF.bottom };
        tooltips->Show(lastTooltipText.c_str(), logRect, renderer->theme.global.position, scale);
    }
    else if (!lastTooltipText.empty()) {
        tooltips->Hide();
        lastTooltipText.clear();
    }
}

void InputManager::HandleLeftClick(HWND hwnd, int x, int y) {
    D2D1_RECT_F rectF;
    Module *m = HitTest(x, y, rectF);
    if (!m) return;

    std::string type = m->config.type;
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;

    if (type == "workspaces") {
        WorkspacesModule *ws = (WorkspacesModule *)m;
        float fullItemSize = ws->itemWidth + ws->itemPadding;
        int index = 0;
        std::string pos = renderer->theme.global.position;

        if (pos == "left" || pos == "right") {
            float localY = ((float)y / scale) - rectF.top;
            index = (int)(localY / fullItemSize);
        }
        else {
            float localX = ((float)x / scale) - rectF.left;
            index = (int)(localX / fullItemSize);
        }

        if (index >= 0 && index < ws->count) {
            app->workspaces.SwitchWorkspace(index);
            ws->SetActiveIndex(index);
            WindowMonitor::GetTopLevelWindows(app->allWindows, app->pinnedApps, app->hwndBar);
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
    else if (type == "audio") {
        if (app->flyout) {
            RECT r = { (LONG)rectF.left, (LONG)rectF.top, (LONG)rectF.right, (LONG)rectF.bottom };
            app->flyout->Toggle(r);
        }
    }
    else if (type == "network") {
        if (!app->networkFlyout) {
            app->networkFlyout = new NetworkFlyout(
                app->hInst, renderer->GetFactory(), renderer->GetWriteFactory(),
                renderer->GetTextFormat(), renderer->GetIconFormat(), renderer->theme
            );
        }
        RECT r = { (LONG)rectF.left, (LONG)rectF.top, (LONG)rectF.right, (LONG)rectF.bottom };
        app->networkFlyout->Toggle(r);
    }
    else if (type == "tray") {
        if (!app->trayFlyout) {
            app->trayFlyout = new TrayFlyout(
                app->hInst, renderer->GetFactory(), renderer->GetWICFactory(), renderer->theme
            );
        }
        RECT r = { (LONG)rectF.left, (LONG)rectF.top, (LONG)rectF.right, (LONG)rectF.bottom };
        app->trayFlyout->Toggle(r);
    }
    else if (type == "battery") {
        ShellExecute(NULL, L"open", L"ms-settings:batterysaver", NULL, NULL, SW_SHOWNORMAL);
    }
    else if (type == "app_icon") {
        SendMessage(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0);
    }
    else if (type == "notification") {
        CommandExecutor::Execute("notification", hwnd);
    }
    else if (type == "custom" || type == "ping" || type == "weather") {
        std::string action = m->config.onClick;
        if (!action.empty()) CommandExecutor::Execute(action, hwnd);
    }
    else if (type == "dock") {
        DockModule *dock = (DockModule *)m;
        float modW = rectF.right - rectF.left;
        float clickX = ((float)x / scale) - rectF.left;

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
                        if (app->workspaces.managedWindows.count(target.hwnd)) {
                            int targetWksp = app->workspaces.managedWindows[target.hwnd];
                            if (targetWksp != app->workspaces.currentWorkspace) {
                                app->workspaces.SwitchWorkspace(targetWksp);
                                Module *wsMod = renderer->GetModule("workspaces");
                                if (wsMod) ((WorkspacesModule *)wsMod)->SetActiveIndex(targetWksp);

                                WindowMonitor::GetTopLevelWindows(app->allWindows, app->pinnedApps, app->hwndBar);
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                        }
                        // Toggle Minimize/Restore
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
    }
}

void InputManager::HandleRightClick(HWND hwnd, int x, int y) {
    D2D1_RECT_F rectF;
    Module *m = HitTest(x, y, rectF);
    if (!m || m->config.type != "dock") return;

    DockModule *dock = (DockModule *)m;
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    float modW = rectF.right - rectF.left;
    float clickX = ((float)x / scale) - rectF.left;

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
            if (index >= 0 && index < count) {
                WindowInfo targetWin = dock->GetWindowInfoAtIndex(index);

                // Show Context Menu
                HMENU hMenu = CreatePopupMenu();
                std::wstring title = targetWin.title.empty() ? L"Application" : targetWin.title;
                AppendMenuW(hMenu, MF_STRING | MF_DISABLED, 0, title.c_str());
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 100, targetWin.hwnd ? L"Open New Window" : L"Launch");

                bool isPinned = false;
                for (auto &p : app->pinnedApps) {
                    if (!p.empty() && p == targetWin.exePath) isPinned = true;
                }
                AppendMenuW(hMenu, MF_STRING, 101, isPinned ? L"Unpin" : L"Pin");

                if (targetWin.hwnd) {
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, 102, L"Close");
                }

                POINT pt = { x, y };
                ClientToScreen(hwnd, &pt);
                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);

                if (cmd == 100) { // Launch
                    std::wstring path = targetWin.exePath;
                    if (path.empty() && targetWin.hwnd) path = WindowMonitor::GetWindowExePath(targetWin.hwnd);
                    if (!path.empty()) ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                else if (cmd == 101) { // Pin
                    std::wstring path = targetWin.exePath;
                    if (path.empty() && targetWin.hwnd) path = WindowMonitor::GetWindowExePath(targetWin.hwnd);

                    auto it = std::find(app->pinnedApps.begin(), app->pinnedApps.end(), path);
                    if (it != app->pinnedApps.end()) app->pinnedApps.erase(it);
                    else app->pinnedApps.push_back(path);

                    app->SavePinnedApps();
                    WindowMonitor::GetTopLevelWindows(app->allWindows, app->pinnedApps, app->hwndBar);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                else if (cmd == 102) { // Close
                    if (targetWin.hwnd) PostMessage(targetWin.hwnd, WM_CLOSE, 0, 0);
                }
            }
        }
    }
}

void InputManager::HandleScroll(HWND hwnd, short delta)
{
    if (GetKeyState(VK_MENU) & 0x8000) { // Alt Key Held
        Module *m = renderer->GetModule("workspaces");
        if (m) {
            WorkspacesModule *ws = (WorkspacesModule *)m;
            int current = app->workspaces.currentWorkspace;
            int count = ws->count;
            int next = current;
            if (delta > 0) next--; else next++;
            if (next < 0) next = count - 1;
            if (next >= count) next = 0;

            if (next != current) {
                app->workspaces.SwitchWorkspace(next);
                ws->SetActiveIndex(next);
                WindowMonitor::GetTopLevelWindows(app->allWindows, app->pinnedApps, app->hwndBar);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
    }
}

void InputManager::OnMouseLeave(HWND hwnd)
{
    if (isTrackingMouse) {
        tooltips->Hide();
        lastTooltipText.clear();
        isTrackingMouse = false;

        // Reset Hover States
        if (renderer) {
            for (auto const &[id, cfg] : renderer->theme.modules) {
                Module *m = renderer->GetModule(id);
                if (m) m->isHovered = false;
                if (m && m->config.type == "workspaces") ((WorkspacesModule *)m)->SetHoveredIndex(-1);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

Module *InputManager::HitTest(int x, int y, D2D1_RECT_F &outRect)
{
	float dpi = (float)GetDpiForWindow(app->hwndBar);
	float scale = dpi / 96.0f;
	POINT pt = { x, y };
	
    // Get Screen Pos of the bar to offset scaling.
    for (auto const &[id, cfg] : renderer->theme.modules) {
        D2D1_RECT_F f = renderer->GetModuleRect(id);
        if (f.right == 0.0f && f.bottom == 0.0f) continue; // skip invisible modules?

        RECT physRect = {
            (LONG)(f.left * scale), (LONG)(f.top * scale),
            (LONG)(f.right * scale), (LONG)(f.bottom * scale)
        };

        if (PtInRect(&physRect, pt)) {
            outRect = f;
            return renderer->GetModule(id);
        }
    }
    return nullptr;
}

