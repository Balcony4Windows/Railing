#pragma once
#include <windows.h>
#include <string>

class TooltipHandler {
public:
    static TooltipHandler instance;
    TooltipHandler();
    ~TooltipHandler();

    void Initialize(HWND hParent);
    void Show(const std::wstring &text, RECT iconRect, std::string &position, float scale);

    void Hide();

private:
    HWND hwndTooltip = nullptr;
    HWND hwndParent = nullptr;
    std::wstring currentText = L"";

    SIZE GetTextSize(const std::wstring &text);
};