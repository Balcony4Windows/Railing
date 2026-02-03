#include "MainMenu.h"
#include <Railing.h>

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
    HMENU menu = CreateTaskbarContextMenu();
    SetForegroundWindow(hwnd);

    TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
        screenPt.x, screenPt.y,
        hwnd, nullptr);

    DestroyMenu(menu);
}

void MainMenu::HandleMenuCmd(HWND hwnd, UINT cmd) {
    auto GetCurrentConfigPath = []() -> std::wstring {
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);
        std::wstring path(exePath);
        path = path.substr(0, path.find_last_of(L"\\/"));

        std::string cfg = Railing::instance->currentConfigName;
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
    case CMD_BAR_DELETE:
    case CMD_BAR_NEW:
        break;
    case CMD_CONFIG_EDIT: {
        std::wstring configPath = GetCurrentConfigPath();
        HINSTANCE res = ShellExecute(NULL, L"open", configPath.c_str(), NULL, NULL, SW_SHOW);
        if ((INT_PTR)res <= 32) ShellExecute(NULL, L"open", L"notepad.exe", configPath.c_str(), NULL, SW_SHOW);
        break;
    }
    case CMD_CONFIG_LOAD: {
        wchar_t szFile[MAX_PATH] = { 0 };
        wchar_t currentDir[MAX_PATH] = { 0 };

        GetModuleFileName(NULL, currentDir, MAX_PATH);
        std::wstring path(currentDir);
        wcsncpy_s(currentDir, path.c_str(), MAX_PATH);

        OPENFILENAME ofn = { 0 };
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = L"JSON Config Files\0*.json\0AllFiles\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = currentDir;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileName(&ofn) == TRUE) {
            std::wstring fullPath(ofn.lpstrFile);
            std::wstring filename;
            if (fullPath.find(path) == 0) filename = fullPath.substr(path.length() + 1);
            else {
                size_t lastSlash = fullPath.find_last_of(L"\\/");
                filename = (lastSlash != std::wstring::npos) ? fullPath.substr(lastSlash + 1) : fullPath;
            }

            int len = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, NULL, 0, NULL, NULL);
            std::string strFile(len, 0);
            WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, &strFile[0], len, NULL, NULL);
            strFile.pop_back();

            Railing::instance->currentConfigName = strFile;
            SendMessage(hwnd, WM_COMMAND, CMD_CONFIG_RELOAD, 0);
        }
        break;
    }
    case CMD_CONFIG_RELOAD: {
        const char *cfgName = Railing::instance->currentConfigName.c_str();

        Railing::instance->cachedConfig = ThemeLoader::Load(cfgName);
        if (Railing::instance->renderer) {
            Railing::instance->renderer->Reload(cfgName);
            Railing::instance->renderer->Resize();
        }

        UnregisterAppBar(hwnd);
        RegisterAppBar(hwnd);
        UpdateAppBarPosition(hwnd, Railing::instance->cachedConfig);
        InvalidateRect(hwnd, NULL, FALSE);
        OutputDebugString(L"[Railing] Config Fully Reloaded.\n");
        break;
    }
    case CMD_CONFIG_RELOAD_MODULES: {
        const char *cfgName = Railing::instance->currentConfigName.c_str();

        Railing::instance->cachedConfig = ThemeLoader::Load(cfgName);
        if (Railing::instance->renderer)
            Railing::instance->renderer->Reload(cfgName);

        Railing::instance->UpdateSystemStats();
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