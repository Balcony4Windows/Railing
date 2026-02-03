#include "InputManager.h"
#include "ModulesConcrete.h"
#include "CommandExecutor.h"
#include <windowsx.h>
#include "WindowMonitor.h"
#include "Railing.h"
#include <cstdlib>

InputManager::InputManager(Railing *a, RailingRenderer *rr, TooltipHandler *t)
    : app(a), renderer(rr), tooltips(t) {
}

void InputManager::HandleMouseMove(HWND hwnd, int x, int y)
{
    if (!renderer) return;

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

    // 1. Global Dock Check & Cleanup
    // If we aren't hitting the dock module explicitly, we might be in the "Gap" or "Preview Window".
    if (!hitModule || hitModule->config.type != "dock") {
        Module *dm = renderer->GetModule("dock");
        if (dm) {
            DockModule *dock = (DockModule *)dm;

            // Only close if the preview is active AND we are truly outside the safe zone.
            if (dock->previewState.active) {
                // We rely on the Preview Window's robust hit testing
                if (!dock->IsMouseInPreviewOrGap()) {
                    dock->previewState.active = false;
                    dock->previewState.groupIndex = -1;
                    needsRepaint = true;
                }
            }
        }
    }

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
            else if (m->isHovered != isOver) {
                m->isHovered = isOver;
                needsRepaint = true;
            }
        }
    }

    if (hitModule) {
        if (!hitModule->config.tooltip.empty()) {
            std::string tip = hitModule->config.tooltip;
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, &tip[0], (int)tip.size(), NULL, 0);
            if (size_needed > 0) {
                std::wstring wstrTo(size_needed, 0);
                MultiByteToWideChar(CP_UTF8, 0, tip.c_str(), -1, &wstrTo[0], size_needed);
                newText = (wstrTo != L"null") ? wstrTo : L"";
            }
        }
        else {
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
                float logicalX = (float)x / scale;
                float logicalY = (float)y / scale;
                int menuRow = -1;

                bool inMenu = dock->HitTestPreview(logicalX, logicalY, menuRow);

                if (inMenu && menuRow >= 0) {
                    if (dock->previewState.hoveredRowIndex != menuRow) {
                        dock->previewState.hoveredRowIndex = menuRow;
                        needsRepaint = true;
                    }
                    newText = L"";
                }
                else {
                    dock->previewState.hoveredRowIndex = -1;

                    // --- DOCK HOVER LOGIC ---
                    std::string pos = renderer->theme.global.position;
                    bool isVertical = (pos == "left" || pos == "right");

                    float modLength = isVertical ? (rectF.bottom - rectF.top) : (rectF.right - rectF.left);
                    float localPos = isVertical ? (logicalY - rectF.top) : (logicalX - rectF.left);

                    Style s = dock->GetEffectiveStyle();
                    Style itemStyle = dock->config.itemStyle;

                    float iSize = (dock->config.dockIconSize > 0) ? dock->config.dockIconSize : 24.0f;

                    // Calculate Stride based on orientation padding
                    float paddingMain = isVertical ? (itemStyle.padding.top + itemStyle.padding.bottom) : (itemStyle.padding.left + itemStyle.padding.right);
                    float itemBoxStride = iSize + paddingMain;

                    float marginMain = isVertical ? (itemStyle.margin.top + itemStyle.margin.bottom) : (itemStyle.margin.left + itemStyle.margin.right);
                    float iSpace = (dock->config.dockSpacing > 0) ? dock->config.dockSpacing : marginMain;

                    size_t count = dock->GetCount();
                    if (count > 0) {
                        float totalLength = (count * itemBoxStride) + ((count - 1) * iSpace);

                        float marginStart = isVertical ? s.margin.top : s.margin.left;
                        float marginEnd = isVertical ? s.margin.bottom : s.margin.right;
                        float bgLength = modLength - marginStart - marginEnd;

                        float startPos = marginStart + ((bgLength - totalLength) / 2.0f);

                        if (localPos >= startPos && localPos <= (startPos + totalLength)) {
                            float offset = localPos - startPos;
                            int index = (int)(offset / (itemBoxStride + iSpace));
                            float relativePos = fmod(offset, (itemBoxStride + iSpace));

                            static int lastHoveredDockIndex = -1;
                            if (index != lastHoveredDockIndex && index >= 0 && index < count) {
                                dock->suppressPreview = false;
                                lastHoveredDockIndex = index;
                            }

                            if (relativePos <= itemBoxStride && index >= 0 && index < count) {
                                newText = dock->GetTitleAtIndex(index);
                                int wins = dock->GetWindowCountAtIndex(index);
                                if (wins > 1) {
                                    if (!dock->suppressPreview) {
                                        if (!dock->previewState.active || dock->previewState.groupIndex != index) {
                                            dock->previewState.active = true;
                                            dock->previewState.groupIndex = index;
                                            needsRepaint = true;
                                        }
                                    }
                                }
                                else {
                                    if (dock->previewState.active) {
                                        dock->previewState.active = false;
                                        dock->previewState.groupIndex = -1;
                                        needsRepaint = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (needsRepaint) InvalidateRect(hwnd, NULL, FALSE);

        if (!newText.empty()) {
            if (lastTooltipText != newText) {
                lastTooltipText = newText;
                RECT logRect = { (LONG)rectF.left, (LONG)rectF.top, (LONG)rectF.right, (LONG)rectF.bottom };
                tooltips->Show(lastTooltipText.c_str(), logRect, renderer->theme.global.position, scale);
            }
        }
        else {
            if (!lastTooltipText.empty()) {
                tooltips->Hide();
                lastTooltipText.clear();
            }
        }
    }
}

void InputManager::HandleLeftClick(HWND hwnd, int x, int y) {
    D2D1_RECT_F rectF;
    Module *m = HitTest(x, y, rectF);
    if (!m) return;

    std::string type = m->config.type;
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;

    auto GetScreenRect = [&](D2D1_RECT_F logicRect) -> RECT {
        RECT r = {
            (LONG)(logicRect.left * scale), (LONG)(logicRect.top * scale),
            (LONG)(logicRect.right * scale), (LONG)(logicRect.bottom * scale)
        };
        MapWindowPoints(hwnd, NULL, (LPPOINT)&r, 2);
        return r;
        };

    if (type == "dock") {
        DockModule *dock = (DockModule *)m;
        float logicalX = (float)x / scale;
        float logicalY = (float)y / scale;
        int clickedRow = -1;

        if (dock->HitTestPreview(logicalX, logicalY, clickedRow)) {
            if (clickedRow >= 0) {
                dock->ClickPreviewItem(clickedRow);
                InvalidateRect(hwnd, NULL, FALSE);
                return;
            }
        }

        dock->ForceHidePreview();

        // --- FIXED VERTICAL CLICK LOGIC ---
        std::string pos = renderer->theme.global.position;
        bool isVertical = (pos == "left" || pos == "right");

        float modLength = isVertical ? (rectF.bottom - rectF.top) : (rectF.right - rectF.left);
        float clickPos = isVertical ? (logicalY - rectF.top) : (logicalX - rectF.left);

        Style s = dock->GetEffectiveStyle();
        float iSize = (dock->config.dockIconSize > 0) ? dock->config.dockIconSize : 24.0f;

        Style itemStyle = dock->config.itemStyle;
        float paddingMain = isVertical ? (itemStyle.padding.top + itemStyle.padding.bottom) : (itemStyle.padding.left + itemStyle.padding.right);
        float itemBoxStride = iSize + paddingMain;

        float marginMain = isVertical ? (itemStyle.margin.top + itemStyle.margin.bottom) : (itemStyle.margin.left + itemStyle.margin.right);
        float iSpace = (dock->config.dockSpacing > 0) ? dock->config.dockSpacing : marginMain;

        int count = dock->GetCount();
        if (count > 0) {
            float totalLength = (count * itemBoxStride) + ((count - 1) * iSpace);

            float marginStart = isVertical ? s.margin.top : s.margin.left;
            float marginEnd = isVertical ? s.margin.bottom : s.margin.right;
            float bgLength = modLength - marginStart - marginEnd;

            float startPos = marginStart + ((bgLength - totalLength) / 2.0f);

            if (clickPos >= startPos && clickPos <= (startPos + totalLength)) {
                float offset = clickPos - startPos;
                int index = (int)(offset / (itemBoxStride + iSpace));
                float relativePos = fmod(offset, (itemBoxStride + iSpace));

                if (relativePos <= itemBoxStride && index >= 0 && index < count) {
                    WindowInfo info = dock->GetWindowInfoAtIndex(index);
                    if (info.hwnd == NULL) {
                        ShellExecuteW(NULL, L"open", info.exePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                    else {
                        HWND currentFg = GetForegroundWindow();
                        bool isGroupActive = false;
                        if (currentFg == info.hwnd) isGroupActive = true;
                        else {
                            HWND next = dock->GetNextWindowInGroup(index, currentFg, 0);
                            if (next == currentFg) isGroupActive = true;
                        }
                        HWND target = info.hwnd;
                        if (isGroupActive) {
                            target = dock->GetNextWindowInGroup(index, currentFg, 1);
                            if (target == currentFg) {
                                ShowWindow(target, SW_MINIMIZE);
                                dock->SetOptimisticFocus(NULL);
                                InvalidateRect(hwnd, NULL, FALSE);
                                return;
                            }
                        }
                        if (app->workspaces.managedWindows.count(target)) {
                            int wksp = app->workspaces.managedWindows[target];
                            if (wksp != app->workspaces.currentWorkspace) {
                                app->workspaces.SwitchWorkspace(wksp);
                                Module *wsMod = renderer->GetModule("workspaces");
                                if (wsMod) ((WorkspacesModule *)wsMod)->SetActiveIndex(wksp);
                                WindowMonitor::GetTopLevelWindows(app->allWindows, app->pinnedApps, app->hwndBar);
                            }
                        }
                        if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                        SetForegroundWindow(target);
                        dock->SetOptimisticFocus(target);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
    }
    else if (type == "workspaces") {
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
        if (app->flyout) { RECT r = GetScreenRect(rectF); app->flyout->Toggle(r); }
    }
    else if (type == "network") {
        if (!app->networkFlyout) app->networkFlyout = new NetworkFlyout(app->hInst, renderer->GetFactory(), renderer->GetWriteFactory(), renderer->GetTextFormat(), renderer->GetIconFormat(), renderer->theme);
        RECT r = GetScreenRect(rectF); app->networkFlyout->Toggle(r);
    }
    else if (type == "tray") {
        if (!app->trayFlyout) app->trayFlyout = new TrayFlyout(app->hInst, renderer->GetFactory(), renderer->GetWICFactory(), tooltips, renderer->theme);
        RECT r = GetScreenRect(rectF); app->trayFlyout->Toggle(r);
    }
    else if (type == "battery") { ShellExecute(NULL, L"open", L"ms-settings:batterysaver", NULL, NULL, SW_SHOWNORMAL); }
    else if (type == "app_icon") { SendMessage(hwnd, WM_SYSCOMMAND, SC_TASKLIST, 0); }
    else if (type == "notification") { CommandExecutor::Execute("notification", hwnd); }
    else if (type == "custom" || type == "ping" || type == "weather" || type == "clock") {
        std::string action = m->config.onClick;
        if (!action.empty()) CommandExecutor::Execute(action, hwnd);
    }
}

void InputManager::HandleRightClick(HWND hwnd, int x, int y) {
    D2D1_RECT_F rectF;
    Module *m = HitTest(x, y, rectF);
    if (!m || m->config.type != "dock") {
        MainMenu::Show(hwnd, {x, y});
        return;
    }
    DockModule *dock = (DockModule *)m;
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;

    std::string pos = renderer->theme.global.position;
    bool isVertical = (pos == "left" || pos == "right");

    float modLength = isVertical ? (rectF.bottom - rectF.top) : (rectF.right - rectF.left);
    float clickPos = isVertical ? (((float)y / scale) - rectF.top) : (((float)x / scale) - rectF.left);

    Style s = dock->GetEffectiveStyle();
    float iSize = (dock->config.dockIconSize > 0) ? dock->config.dockIconSize : 24.0f;

    Style itemStyle = dock->config.itemStyle;
    float paddingMain = isVertical ? (itemStyle.padding.top + itemStyle.padding.bottom) : (itemStyle.padding.left + itemStyle.padding.right);
    float itemBoxStride = iSize + paddingMain;

    float marginMain = isVertical ? (itemStyle.margin.top + itemStyle.margin.bottom) : (itemStyle.margin.left + itemStyle.margin.right);
    float iSpace = (dock->config.dockSpacing > 0) ? dock->config.dockSpacing : marginMain;

    int count = dock->GetCount();
    if (count > 0) {
        float totalLength = (count * itemBoxStride) + ((count - 1) * iSpace);

        float marginStart = isVertical ? s.margin.top : s.margin.left;
        float marginEnd = isVertical ? s.margin.bottom : s.margin.right;
        float bgLength = modLength - marginStart - marginEnd;
        float startPos = marginStart + ((bgLength - totalLength) / 2.0f);

        if (clickPos >= startPos && clickPos <= (startPos + totalLength)) {
            float offset = clickPos - startPos;
            int index = (int)(offset / (itemBoxStride + iSpace));
            if (index >= 0 && index < count) {
                WindowInfo targetWin = dock->GetWindowInfoAtIndex(index);
                HMENU hMenu = CreatePopupMenu();
                std::wstring title = targetWin.title.empty() ? L"Application" : targetWin.title;
                AppendMenuW(hMenu, MF_STRING | MF_DISABLED, 0, title.c_str());
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, 100, targetWin.hwnd ? L"Open New Window" : L"Launch");
                bool isPinned = dock->IsPinned(targetWin.exePath);
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
                if (cmd == 100) {
                    std::wstring path = targetWin.exePath;
                    if (path.empty() && targetWin.hwnd) path = WindowMonitor::GetWindowExePath(targetWin.hwnd);
                    if (!path.empty()) ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
                else if (cmd == 101) {
                    std::wstring path = targetWin.exePath;
                    if (path.empty() && targetWin.hwnd) path = WindowMonitor::GetWindowExePath(targetWin.hwnd);
                    if (!path.empty()) {
                        if (dock->IsPinned(path)) dock->UnpinApp(path);
                        else dock->PinApp(path);
                    }
                    WindowMonitor::GetTopLevelWindows(app->allWindows, app->pinnedApps, app->hwndBar);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                else if (cmd == 102) {
                    if (targetWin.hwnd) PostMessage(targetWin.hwnd, WM_CLOSE, 0, 0);
                }
            }
        }
    }
}

void InputManager::HandleScroll(HWND hwnd, short delta) {
    if (GetKeyState(VK_MENU) & 0x8000) {
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
        return;
    }
    POINT pt; GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    D2D1_RECT_F rectF;
    Module *m = HitTest(pt.x, pt.y, rectF);
    if (m && m->config.type == "dock") {
        DockModule *dock = (DockModule *)m;
        float dpi = (float)GetDpiForWindow(hwnd);
        float scale = dpi / 96.0f;

        // --- FIXED VERTICAL SCROLL LOGIC ---
        std::string pos = renderer->theme.global.position;
        bool isVertical = (pos == "left" || pos == "right");

        float modLength = isVertical ? (rectF.bottom - rectF.top) : (rectF.right - rectF.left);
        float clickPos = isVertical ? (((float)pt.y / scale) - rectF.top) : (((float)pt.x / scale) - rectF.left);

        Style s = dock->GetEffectiveStyle();
        float iSize = (dock->config.dockIconSize > 0) ? dock->config.dockIconSize : 24.0f;
        Style itemStyle = dock->config.itemStyle;

        float paddingMain = isVertical ? (itemStyle.padding.top + itemStyle.padding.bottom) : (itemStyle.padding.left + itemStyle.padding.right);
        float itemBoxStride = iSize + paddingMain;

        float marginMain = isVertical ? (itemStyle.margin.top + itemStyle.margin.bottom) : (itemStyle.margin.left + itemStyle.margin.right);
        float iSpace = (dock->config.dockSpacing > 0) ? dock->config.dockSpacing : marginMain;

        int count = dock->GetCount();
        float totalLength = (count * itemBoxStride) + ((count - 1) * iSpace);

        float marginStart = isVertical ? s.margin.top : s.margin.left;
        float marginEnd = isVertical ? s.margin.bottom : s.margin.right;
        float bgLength = modLength - marginStart - marginEnd;
        float startPos = marginStart + ((bgLength - totalLength) / 2.0f);

        if (clickPos >= startPos && clickPos <= (startPos + totalLength)) {
            float offset = clickPos - startPos;
            int index = (int)(offset / (itemBoxStride + iSpace));
            int direction = (delta > 0) ? -1 : 1;
            HWND currentFg = GetForegroundWindow();
            HWND nextWin = dock->GetNextWindowInGroup(index, currentFg, direction);
            if (nextWin && nextWin != currentFg) {
                if (IsIconic(nextWin)) ShowWindow(nextWin, SW_RESTORE);
                SetForegroundWindow(nextWin);
                dock->SetOptimisticFocus(nextWin);
                tooltips->Hide();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
    }
}

void InputManager::OnMouseLeave(HWND hwnd) {
    if (renderer) {
        Module *dm = renderer->GetModule("dock");
        if (dm) {
            DockModule *dock = (DockModule *)dm;

            // 1. Check if we moved into the preview window/gap (Valid Transition)
            if (dock->m_previewWin && dock->IsMouseInPreviewOrGap()) {
                return;
            }

            // 2. MOUSE LEFT THE BAR AND IS NOT IN PREVIEW
            // Explicitly disable active state to stop the Update() loop
            if (dock->previewState.active) {
                dock->previewState.active = false;
                dock->previewState.groupIndex = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }

            dock->suppressPreview = false;
        }
    }
    if (isTrackingMouse) {
        tooltips->Hide();
        lastTooltipText.clear();
        isTrackingMouse = false;
        if (renderer) {
            for (auto const &[id, cfg] : renderer->theme.modules) {
                Module *m = renderer->GetModule(id);
                if (m) {
                    m->isHovered = false;
                    if (m->config.type == "workspaces") ((WorkspacesModule *)m)->SetHoveredIndex(-1);
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

Module *InputManager::HitTest(int x, int y, D2D1_RECT_F &outRect) {
    if (!renderer) return nullptr;
    float dpi = (float)GetDpiForWindow(app->hwndBar);
    float scale = dpi / 96.0f;
    POINT pt = { x, y };

    auto CheckList = [&](const std::vector<Module *> &list) -> Module *{
        for (Module *m : list) {
            D2D1_RECT_F f = renderer->GetModuleRect(m->config.id);
            if (f.right == 0.0f && f.bottom == 0.0f) continue;

            RECT physRect = {
                (LONG)(f.left * scale), (LONG)(f.top * scale),
                (LONG)(f.right * scale), (LONG)(f.bottom * scale)
            };

            // INFLATE HIT BOX (Vertical padding for easier clicking)
            InflateRect(&physRect, 0, 24);

            if (PtInRect(&physRect, pt)) {
                if (m->config.type == "group") {
                    GroupModule *g = (GroupModule *)m;
                    for (auto *child : g->children) {
                        // --- FIX START ---
                        // Don't ask Renderer for child rects (it doesn't know them).
                        // Use the cachedRect stored on the child module itself.
                        D2D1_RECT_F cf = child->cachedRect;
                        // --- FIX END ---

                        RECT cphys = {
                            (LONG)(cf.left * scale), (LONG)(cf.top * scale),
                            (LONG)(cf.right * scale), (LONG)(cf.bottom * scale)
                        };

                        InflateRect(&cphys, 0, 24); // Inflate child items too

                        if (PtInRect(&cphys, pt)) {
                            outRect = cf;
                            return child;
                        }
                    }
                }
                outRect = f;
                return m;
            }
        }
        return nullptr;
        };

    Module *found = CheckList(renderer->leftModules); if (found) return found;
    found = CheckList(renderer->centerModules); if (found) return found;
    return CheckList(renderer->rightModules);
}