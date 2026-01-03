#include "CommandExecutor.h"
#include <shellapi.h>
#include <vector>
#include <sstream>
#include <powrprof.h> // For sleep/hibernate if needed
#pragma comment(lib, "PowrProf.lib")

void CommandExecutor::Execute(const std::string &command, HWND hwndBar) {
    if (command.empty()) return;

    if (command == "toggle_desktop") {
        SendWinKey('D');
    }
    else if (command == "toggle_start") {
        SendMessage(hwndBar, WM_SYSCOMMAND, SC_TASKLIST, 0);
    }
    else if (command == "notification") {
        SendWinKey('N');
    }
    else if (command == "lock") {
        LockWorkStation();
    }
    else if (command == "taskmgr") {
        ShellExecute(NULL, NULL, L"taskmgr.exe", NULL, NULL, SW_SHOWNORMAL);
    }
    else if (command == "run") {
        // Only works via COM usually, but this shortcut works often
        ShellExecute(NULL, NULL, L"explorer.exe", L"Shell:::{2559a1f3-21d7-11d4-bdaf-00c04f60b9f0}", NULL, SW_SHOWNORMAL);
    }

    else if (command.rfind("exec:", 0) == 0) {
        OpenProcess(command.substr(5));
    }
    else if (command.rfind("shell:", 0) == 0) {
        // Usage: "shell:ms-settings:display"
        std::string target = command.substr(6);
        std::wstring wtarget(target.begin(), target.end());
        ShellExecuteW(NULL, L"open", wtarget.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
}

void CommandExecutor::SendWinKey(char key) {
    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = key;
    inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = key; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_LWIN; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void CommandExecutor::OpenProcess(std::string cmd) {
    std::wstring wcmd(cmd.begin(), cmd.end());
    ShellExecuteW(NULL, L"open", wcmd.c_str(), NULL, NULL, SW_SHOWNORMAL);
}