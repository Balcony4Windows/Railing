#include "MainMenu.h"
#include "BarInstance.h"
#include "Railing.h"
#include <shobjidl.h>
#include <thread>
#include <shtypes.h>
#include <ShObjIdl_core.h>

HMENU MainMenu::CreateTaskbarContextMenu()
{
    HMENU root = CreatePopupMenu();

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

    // Bars
    HMENU bars = CreatePopupMenu();
    AppendMenu(bars, MF_STRING, CMD_BAR_NEW, L"New Bar");
    AppendMenu(bars, MF_STRING, CMD_BAR_DUPLICATE, L"Duplicate This Bar");
    AppendMenu(bars, MF_SEPARATOR, 0, nullptr);
    AppendMenu(bars, MF_STRING, CMD_BAR_DELETE, L"Delete This Bar...");

    AppendMenu(root, MF_POPUP, (UINT_PTR)bars, L"Bars");

    // Config
    HMENU config = CreatePopupMenu();
    AppendMenu(config, MF_STRING, CMD_CONFIG_RELOAD, L"Reload Config");
    AppendMenu(config, MF_STRING, CMD_CONFIG_RELOAD_MODULES, L"Reload Modules Only");
    AppendMenu(config, MF_SEPARATOR, 0, nullptr);
    AppendMenu(config, MF_STRING, CMD_CONFIG_LOAD, L"Load Config...");
    AppendMenu(config, MF_STRING, CMD_CONFIG_EDIT, L"Edit Config...");

    AppendMenu(root, MF_POPUP, (UINT_PTR)config, L"Configuration");

    // Interaction
    HMENU interact = CreatePopupMenu();
    AppendMenu(interact, MF_STRING, CMD_INTERACT_MOVE, L"Move Bar");
    AppendMenu(interact, MF_STRING, CMD_INTERACT_RESIZE, L"Resize Bar");
    AppendMenu(interact, MF_SEPARATOR, 0, nullptr);
    AppendMenu(interact, MF_STRING | MF_CHECKED, CMD_INTERACT_SNAP, L"Snap To Edges");
    
    AppendMenu(root, MF_POPUP, (UINT_PTR)interact, L"Interaction");

    // System
    HMENU system = CreatePopupMenu();
    AppendMenu(system, MF_STRING, CMD_SYSTEM_RESTART_BAR, L"Restart Taskbar");
    AppendMenu(system, MF_STRING, CMD_SYSTEM_RESTART_SHELL, L"Restart Shell");
    AppendMenu(system, MF_SEPARATOR, 0, nullptr);
    AppendMenu(system, MF_STRING, CMD_SYSTEM_EXIT_TO_EXPLORER, L"Exit to Explorer");
    
    AppendMenu(root, MF_POPUP, (UINT_PTR)system, L"System");

    AppendMenu(root, MF_SEPARATOR, 0, nullptr);
    AppendMenu(root, MF_STRING, CMD_ABOUT, L"About");

    return root;
}

void MainMenu::Show(HWND hwnd, POINT screenPt) {
    wchar_t buf[128];
    swprintf_s(buf, L"Clicked Point: {%d, %d}\n", screenPt.x, screenPt.y);
    OutputDebugString(buf);
    HMENU menu = CreateTaskbarContextMenu();
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
    case CMD_LAYOUT_RIGHT:
        break;
    case CMD_LAYOUT_FLOATING:
    case CMD_LAYOUT_LOCK:
        break;
    case CMD_BAR_DUPLICATE:
        Railing::instance->CreateNewBar(bar->configFileName);
        break;
    case CMD_BAR_DELETE: {
        int result = MessageBox(hwnd, L"Are you sure you want to close this bar?",
            L"Remove Bar", MB_YESNO | MB_ICONWARNING | MB_TOPMOST);

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
        std::thread([hwnd, bar]() {
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
            }).detach();
        break;
    }
    case CMD_CONFIG_RELOAD: {
        const char *cfgName = bar->configFileName.c_str();

        bar->config = ThemeLoader::Load(cfgName);
        if (bar->renderer) {
            bar->renderer->Reload(cfgName);
            bar->renderer->Resize();
        }

        UnregisterAppBar(hwnd);
        RegisterAppBar(hwnd);
        UpdateAppBarPosition(hwnd, bar->config);
        InvalidateRect(hwnd, NULL, FALSE);
        OutputDebugString(L"[Railing] Config Fully Reloaded.\n");
        break;
    }
    case CMD_CONFIG_RELOAD_MODULES: {
        const char *cfgName = bar->configFileName.c_str();

        bar->config = ThemeLoader::Load(cfgName);
        if (bar->renderer)
            bar->renderer->Reload(cfgName);

        Railing::instance->UpdateGlobalStats();
        InvalidateRect(hwnd, NULL, FALSE);
        OutputDebugString(L"[Railing] Modules Reloaded.\n");
        break;
    }
    case CMD_INTERACT_MOVE:
    case CMD_INTERACT_RESIZE:
    case CMD_INTERACT_SNAP:
        break;
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
    }
}