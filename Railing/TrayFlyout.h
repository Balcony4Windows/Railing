#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <vector>
#include "TrayManager.h"

class TrayFlyout {
public:
    TrayFlyout(HINSTANCE hInst);
    ~TrayFlyout();

    void Toggle(int anchorX, int anchorY);
    bool IsVisible();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void Draw();
    void OnClick(int x, int y, bool isRightClick);

    HWND hwnd = nullptr;
    TrayManager backend;
    std::vector<TrayIconData> currentIcons;

    // Animation support
    void UpdateAnimation();
    enum class AnimationState { Hidden, Entering, Visible, Exiting };
    AnimationState animState = AnimationState::Hidden;
    float currentAlpha = 0.0f;
    float currentOffset = 0.0f;
    ULONGLONG lastAnimTime = 0;
    ULONGLONG lastAutoCloseTime = 0; // Debounce
    int targetX = 0;
    int targetY = 0;

    ID2D1Factory *pFactory = nullptr;
    ID2D1HwndRenderTarget *pRenderTarget = nullptr;
    ID2D1SolidColorBrush *pBgBrush = nullptr;
    ID2D1SolidColorBrush *pHoverBrush = nullptr;

    int hoverIndex = -1;
};