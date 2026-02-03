#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <vector>
#include <wincodec.h>
#include "ThemeTypes.h"
#include "TooltipHandler.h"
#include "TrayBackend.h"
#include "IFlyout.h"

class TrayFlyout : IFlyout {
public:
    TrayFlyout(HINSTANCE hInst, ID2D1Factory *sharedFactory, IWICImagingFactory *sharedWIC, TooltipHandler *tooltips, const ThemeConfig &config);
    ~TrayFlyout();

    HWND hwnd = nullptr;
    void Toggle(RECT iconRect);
    void Hide() override;
    void Draw();
    bool IsVisible() override { return (animState != AnimationState::Hidden); }
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnClick(int x, int y, bool isRightClick);
    void OnDoubleClick(int x, int y);
    void OnMouseLeave();
    void OnMouseMove(int x, int y);
    void UpdateLayout();
    int hoveredIconIndex = -1;
    TooltipHandler *tooltips = nullptr;
    bool ignoreNextDeactivate = false;

    void RefreshIcons() {
        currentIcons = TrayBackend::Get().GetIcons();
        UpdateBitmapCache();
        InvalidateRect(hwnd, NULL, FALSE);
    }

    ThemeConfig::Global style;
    void UpdateBitmapCache();

    float layoutIconSize = 32.0f;
    float layoutPadding = 12.0f;
    int layoutCols = 5;

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