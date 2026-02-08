#pragma once
#include <string>
#include <windows.h>

class CommandExecutor {
public:
    static void SafetyTest(HINSTANCE hInst);
    static void Execute(const std::string &command, HWND hwndBar);
private:
    static void SendWinKey(char key);
    static void OpenProcess(std::string cmd);
    static void SearchWeb(std::string term, std::string baseUrl);
};