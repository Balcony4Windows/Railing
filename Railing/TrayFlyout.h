#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <vector>
#include <wincodec.h>
#include "ThemeTypes.h"

struct TrayIconData {
    HICON hIcon;
    RECT rect;
};

class TrayFlyout {
public:
    TrayFlyout(HINSTANCE hInst, ID2D1Factory *sharedFactory, IWICImagingFactory *sharedWIC, const ThemeConfig &config);
    ~TrayFlyout();

    void Toggle(RECT iconRect);
    void Draw();
    bool IsVisible() { return IsWindowVisible(hwnd); }
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnClick(int x, int y, bool isRightClick);

    HWND hwnd = nullptr;
    ThemeConfig::Global style;
    void UpdateBitmapCache();

    // Animation support
    void UpdateAnimation();
    enum class AnimationState { Hidden, Entering, Visible, Exiting };
    AnimationState animState = AnimationState::Hidden;
    float currentAlpha = 0.0f;
    float currentOffset = 0.0f;
    ULONGLONG lastAnimTime = 0;
    ULONGLONG lastAutoCloseTime = 0; // Debounce
    int targetX = 0, targetY = 0;

    ID2D1Factory *pFactory = nullptr;
    IWICImagingFactory *pWICFactory = nullptr;
    ID2D1HwndRenderTarget *pRenderTarget = nullptr;
    ID2D1SolidColorBrush *pBgBrush = nullptr;
    ID2D1SolidColorBrush *pHoverBrush = nullptr;
    ID2D1SolidColorBrush *pBorderBrush = nullptr;

    std::vector<TrayIconData> currentIcons;
    std::vector<ID2D1Bitmap *> cachedBitmaps;

    int hoverIndex = -1;
};