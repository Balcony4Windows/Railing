#include "MainMenu.h"
#include "BarInstance.h"
#include "Railing.h"
#include <shobjidl.h>
#include <thread>
#include <shtypes.h>
#include <ShObjIdl_core.h>

std::thread MainMenu::configDialogThread;

HMENU MainMenu::CreateTaskbarContextMenu(BarInstance *bar)
{
    HMENU root = CreatePopupMenu();

    // Bars
    HMENU bars = CreatePopupMenu();
    AppendMenu(bars, MF_STRING, CMD_BAR_NEW, L"New Bar");
    AppendMenu(bars, MF_STRING, CMD_BAR_DUPLICATE, L"Duplicate This Bar");
    AppendMenu(bars, MF_SEPARATOR, 0, nullptr);
    AppendMenu(bars, MF_STRING, CMD_BAR_DELETE, L"Delete This Bar...");

    AppendMenu(root, MF_POPUP, (UINT_PTR)bars, L"Bars");

    // Layout
    HMENU layout = CreatePopupMenu();
    HMENU position = CreatePopupMenu();
    AppendMenu(position, MF_STRING, CMD_LAYOUT_TOP, L"Top");
    AppendMenu(position, MF_STRING, CMD_LAYOUT_BOTTOM, L"Bottom");
    AppendMenu(position, MF_STRING, CMD_LAYOUT_LEFT, L"Left");
    AppendMenu(position, MF_STRING, CMD_LAYOUT_RIGHT, L"Right");
    AppendMenu(position, MF_SEPARATOR, 0, nullptr);
    AppendMenu(position, MF_STRING, CMD_LAYOUT_FLOATING, L"Floating...");

    AppendMenu(layout, MF_POPUP, (UINT_PTR)position, L"Position");
    AppendMenu(layout, MF_SEPARATOR, 0, nullptr);
    AppendMenu(layout, MF_STRING | MF_CHECKED, CMD_LAYOUT_LOCK, L"Lock Layout");

    AppendMenu(root, MF_POPUP, (UINT_PTR)layout, L"Layout");

    // Interaction
    HMENU interact = CreatePopupMenu();
    AppendMenu(interact, MF_STRING, CMD_INTERACT_MOVE, L"Move Bar");
    AppendMenu(interact, MF_STRING, CMD_INTERACT_RESIZE, L"Resize Bar");
    AppendMenu(interact, MF_SEPARATOR, 0, nullptr);
    AppendMenu(interact, MF_STRING, CMD_INTERACT_SNAP, L"Snap To Edges");

    AppendMenu(root, MF_POPUP, (UINT_PTR)interact, L"Interaction");

    // Config
    HMENU config = CreatePopupMenu();
    AppendMenu(config, MF_STRING, CMD_CONFIG_RELOAD, L"Reload Config");
    AppendMenu(config, MF_STRING, CMD_CONFIG_RELOAD_MODULES, L"Reload Modules Only");
    AppendMenu(config, MF_SEPARATOR, 0, nullptr);
    AppendMenu(config, MF_STRING, CMD_CONFIG_LOAD, L"Load Config...");
    AppendMenu(config, MF_STRING, CMD_CONFIG_EDIT, L"Edit Config...");

    AppendMenu(root, MF_POPUP, (UINT_PTR)config, L"Configuration");

    // System
    HMENU system = CreatePopupMenu();
    AppendMenu(system, MF_STRING, CMD_SYSTEM_RESTART_BAR, L"Restart Taskbar");
    AppendMenu(system, MF_STRING, CMD_SYSTEM_RESTART_SHELL, L"Restart Shell");
    AppendMenu(system, MF_SEPARATOR, 0, nullptr);
    AppendMenu(system, MF_STRING, CMD_SYSTEM_EXIT_TO_EXPLORER, L"Exit to Explorer");
    
    AppendMenu(root, MF_POPUP, (UINT_PTR)system, L"System");

    AppendMenu(root, MF_SEPARATOR, 0, nullptr);
    UINT flags = MF_STRING;
    if (bar && bar->config.global.autoHide) flags |= MF_CHECKED;
    AppendMenu(root, flags, CMD_AUTOHIDE, L"Auto-Hide");
    AppendMenu(root, MF_STRING, CMD_ABOUT, L"About");
    return root;
}

void MainMenu::Show(HWND hwnd, POINT screenPt) {
    BarInstance *bar = Railing::instance->FindBar(hwnd);
    HMENU menu = CreateTaskbarContextMenu(bar);
    SetForegroundWindow(hwnd);

    TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
        screenPt.x, screenPt.y,
        hwnd, nullptr);

    DestroyMenu(menu);
}

void MainMenu::HandleMenuCmd(HWND hwnd, UINT cmd) {
	BarInstance *bar = Railing::instance->FindBar(hwnd);
    if (!bar) return;

    auto GetCurrentConfigPath = [bar]() -> std::wstring {
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);
        std::wstring path(exePath);
        path = path.substr(0, path.find_last_of(L"\\/"));

        std::string cfg = bar->configFileName;
        std::wstring wCfg(cfg.begin(), cfg.end());
        return path + L"\\" + wCfg;
        };
    switch (cmd) {
        case CMD_LAYOUT_TOP:
        case CMD_LAYOUT_BOTTOM:
        case CMD_LAYOUT_LEFT:
        case CMD_LAYOUT_RIGHT: {
            std::string newPos = "top";
            if (cmd == CMD_LAYOUT_BOTTOM) newPos = "bottom";
            else if (cmd == CMD_LAYOUT_LEFT) newPos = "left";
            else if (cmd == CMD_LAYOUT_RIGHT) newPos = "right";
            bar->config.global.position = newPos;

            UnregisterAppBar(hwnd);
            RegisterAppBar(hwnd);
            UpdateAppBarPosition(hwnd, bar->config);

            if (bar->renderer) {
                bar->renderer->SetScreenPosition(newPos);
                bar->renderer->Resize();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case CMD_LAYOUT_FLOATING: {
            UnregisterAppBar(hwnd);
            bar->config.global.position = "floating";
            if (bar->renderer) bar->renderer->SetScreenPosition("floating");

            RECT rc; GetWindowRect(hwnd, &rc);
            int currentH = rc.bottom - rc.top;
            SetWindowPos(hwnd, NULL, rc.left, rc.top - 15, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (bar->renderer) bar->renderer->Resize();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case CMD_LAYOUT_LOCK: {
            if (bar->config.global.position != "floating") break;
            bar->config.global.position = "bottom";
            if (bar->renderer) bar->renderer->SetScreenPosition("bottom");
            RegisterAppBar(hwnd);
            UpdateAppBarPosition(hwnd, bar->config);
            if (bar->renderer) bar->renderer->Resize();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case CMD_BAR_DUPLICATE:
            Railing::instance->CreateNewBar(bar->configFileName);
            break;
        case CMD_BAR_DELETE: {
            int result = MessageBox(hwnd, L"Are you sure you want to close this bar?",
                L"Remove Bar", MB_YESNO | MB_ICONWARNING | MB_TOPMOST);
            UnregisterAppBar(bar->GetHwnd());
            if (result == IDYES) Railing::instance->DeleteBar(bar);
            break;
        }
        case CMD_BAR_NEW:
            Railing::instance->CreateNewBar("config.json");
            break;
        case CMD_CONFIG_EDIT: {
            std::wstring configPath = GetCurrentConfigPath();
            HINSTANCE res = ShellExecute(NULL, L"open", configPath.c_str(), NULL, NULL, SW_SHOW);
            if ((INT_PTR)res <= 32) ShellExecute(NULL, L"open", L"notepad.exe", configPath.c_str(), NULL, SW_SHOW);
            break;
        }
        case CMD_CONFIG_LOAD: {
            if (configDialogThread.joinable()) configDialogThread.join();
            configDialogThread = std::thread([hwnd, bar]() {
                HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                IFileOpenDialog *pFileOpen = nullptr;

                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void **>(&pFileOpen)))) {
                    COMDLG_FILTERSPEC rgSpec[] = { { L"JSON Config Files", L"*.json" }, { L"All Files", L"*.*" } };
                    pFileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);

                    wchar_t currentDir[MAX_PATH] = { 0 };
                    GetModuleFileNameW(NULL, currentDir, MAX_PATH);
                    std::filesystem::path exePath(currentDir);
                    std::filesystem::path exeDir = exePath.parent_path();

                    IShellItem *pFolder = nullptr;
                    if (SUCCEEDED(SHCreateItemFromParsingName(exeDir.c_str(), NULL, IID_PPV_ARGS(&pFolder)))) {
                        pFileOpen->SetDefaultFolder(pFolder);
                        pFolder->Release();
                    }
                    if (SUCCEEDED(pFileOpen->Show(NULL))) {
                        IShellItem *pItem;
                        if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                            PWSTR pszFilePath;
                            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                                std::filesystem::path selectedPath(pszFilePath);
                                std::string finalPathStr;
                                try {
                                    std::filesystem::path relative = std::filesystem::relative(selectedPath, exeDir);
                                    finalPathStr = relative.string(); // Auto-converts to UTF-8 in C++20, or system codepage in C++17
                                }
                                catch (...) {
                                    auto str = selectedPath.string();
                                    finalPathStr = str;
                                }

                                if (!finalPathStr.empty()) {
                                    bar->configFileName = finalPathStr;
                                    PostMessage(hwnd, WM_COMMAND, CMD_CONFIG_RELOAD, 0);
                                }
                                CoTaskMemFree(pszFilePath);
                            }
                            pItem->Release();
                        }
                    }
                    pFileOpen->Release();
                }
                if (SUCCEEDED(hrInit)) CoUninitialize();
                });
            break;
        }
        case CMD_CONFIG_RELOAD:
        case CMD_CONFIG_RELOAD_MODULES: {
            KillTimer(hwnd, 1 /* STATS_TIMER_ID*/);
            KillTimer(hwnd, ANIMATION_TIMER_ID);

            const char *cfgName = bar->configFileName.c_str();

            bar->config = ThemeLoader::Load(cfgName);
            if (bar->renderer) {
                bar->renderer->Reload(cfgName);
                if (cmd == CMD_CONFIG_RELOAD) bar->renderer->Resize();
            }

            if (cmd == CMD_CONFIG_RELOAD) {
                UnregisterAppBar(hwnd);
                RegisterAppBar(hwnd);
                UpdateAppBarPosition(hwnd, bar->config);
            }
            else Railing::instance->UpdateGlobalStats();

            SetTimer(hwnd, 1, 1000, NULL);
            SetTimer(hwnd, ANIMATION_TIMER_ID, 16, NULL);

            InvalidateRect(hwnd, NULL, FALSE);
            OutputDebugString(L"[Railing] Config Reloaded Safely.\n");
            break;
        }
        case CMD_INTERACT_RESIZE:
        case CMD_INTERACT_MOVE: {
            bar->interactionMode = (cmd == CMD_INTERACT_MOVE)
                ? InteractionMode::Move
                : InteractionMode::Resize;

            UnregisterAppBar(hwnd);
            LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
            SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_THICKFRAME);

            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            break;
        }
        case CMD_INTERACT_SNAP: {
            RECT rc; GetWindowRect(hwnd, &rc);
            HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);
            UINT dpi = GetDpiForWindow(hwnd);

            if (bar->interactionMode == InteractionMode::Move) {
                int distTop = abs(rc.top - mi.rcMonitor.top);
                int distBottom = abs(rc.bottom - mi.rcMonitor.bottom);
                int distLeft = abs(rc.left - mi.rcMonitor.left);
                int distRight = abs(rc.right - mi.rcMonitor.right);

                int minD = 20000;
                std::string newPos = "bottom";

                if (distBottom < minD) { minD = distBottom; newPos = "bottom"; }
                if (distTop < minD) { minD = distTop;    newPos = "top"; }
                if (distLeft < minD) { minD = distLeft;   newPos = "left"; }
                if (distRight < minD) { minD = distRight;  newPos = "right"; }

                bar->config.global.position = newPos;
            }
            else if (bar->interactionMode == InteractionMode::Resize) {
                int newThickness = 0;
                if (bar->config.global.position == "bottom" || bar->config.global.position == "top")
                    newThickness = rc.bottom - rc.top;
                else newThickness = rc.right - rc.left;

                bar->config.global.height = MulDiv(newThickness, 96, dpi);
            }

            bar->interactionMode = InteractionMode::None;
            LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
            SetWindowLongPtr(hwnd, GWL_STYLE, style & ~WS_THICKFRAME);

            RegisterAppBar(hwnd);
            UpdateAppBarPosition(hwnd, bar->config);

            if (bar->renderer) {
                bar->renderer->SetScreenPosition(bar->config.global.position);
                bar->renderer->Resize();
            }
            bar->SaveState();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case CMD_SYSTEM_EXIT_TO_EXPLORER: {
            ShellExecute(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
            DestroyWindow(hwnd);
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            break;
        }
        case CMD_SYSTEM_RESTART_BAR: {
            wchar_t exePath[MAX_PATH];
            GetModuleFileName(NULL, exePath, MAX_PATH);
            ShellExecute(NULL, L"open", exePath, NULL, NULL, SW_SHOW);
            DestroyWindow(hwnd);
            break;
        }
        case CMD_SYSTEM_RESTART_SHELL: // No impl until Explorer fully replaced!
            break;
        case CMD_ABOUT:
            ShellExecute(nullptr, L"open", L"https://github.com/Balcony4Windows/Railing/tree/main",
                NULL, NULL, SW_SHOWNORMAL);
            break;
        case CMD_AUTOHIDE: {
            bar->config.global.autoHide = !bar->config.global.autoHide;

            if (bar->config.global.autoHide) {
                // ENABLED: Unregister so windows expand to full screen
                UnregisterAppBar(hwnd);
            }
            else {
                // DISABLED: Register to reserve space again
                RegisterAppBar(hwnd);
                UpdateAppBarPosition(hwnd, bar->config);
            }

            // Force a resize/repaint
            if (bar->renderer) bar->renderer->Resize();
            InvalidateRect(hwnd, NULL, FALSE);
            bar->SaveState();
            break;
        }
    }
}