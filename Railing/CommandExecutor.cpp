#include "CommandExecutor.h"
#include <shellapi.h>
#include <sstream>
#include <iomanip>

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
    else if (command.rfind("search:", 0) == 0) {
        std::string param = command.substr(7); // search:
        std::string engine = "google";
        std::string term = param;

        size_t splitPos = param.find(':');
        if (splitPos != std::string::npos) {
            std::string possibleEngine = param.substr(0, splitPos);
            bool isEngine = (possibleEngine == "bing" || possibleEngine == "ddg"
                || possibleEngine == "youtube" || possibleEngine == "yt"
                || possibleEngine == "yahoo");

            if (isEngine) {
                engine = possibleEngine;
                term = param.substr(splitPos + 1);
            }
        }

        SearchWeb(term, engine);
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

inline std::string UrlEncode(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }
        if (c == ' ') {
            escaped << '+';
            continue;
        }
        escaped << std::uppercase;
        escaped << '%' << std::setw(2) << int((unsigned char)c);
        escaped << std::nouppercase;
    }
    return escaped.str();
}

void CommandExecutor::SearchWeb(std::string term, std::string engine="google") {
    if (term.empty()) return;
    std::string baseUrl = "https://www.google.com/search?q="; // Default

    if (engine == "bing") baseUrl = "https://www.bing.com/search?q=";
    else if (engine == "ddg" || engine == "duckduckgo") baseUrl = "https://duckduckgo.com/?q=";
    else if (engine == "youtube" || engine == "yt") baseUrl = "https://www.youtube.com/results?search_query=";
    else if (engine == "yahoo") baseUrl = "https://search.yahoo.com/search?p=";

    std::string fullUrl = baseUrl + UrlEncode(term);
    std::wstring wUrl(fullUrl.begin(), fullUrl.end());
    ShellExecuteW(NULL, L"open", wUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
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