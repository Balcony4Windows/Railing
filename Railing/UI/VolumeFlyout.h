#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include "AudioBackend.h"
#include "ThemeTypes.h"
#include "IFlyout.h"

class BarInstance;

struct AudioDeviceInfo {
    std::wstring name;
    std::wstring id;
};

class VolumeFlyout : IFlyout {
public:

    VolumeFlyout(
        BarInstance *owner, HINSTANCE hInst,
        ID2D1Factory *pSharedFactory, IDWriteFactory *pSharedWriteFactory,
        IDWriteTextFormat *pFormat, const ThemeConfig &config); 
    ~VolumeFlyout();
    AudioBackend audio;
    HWND hwnd = nullptr;
	BarInstance *ownerBar;

    void Toggle(RECT iconRect); // Open/Close
    void Hide() override;
    enum class AnimationState { Hidden, Entering, Visible, Exiting };
    AnimationState animState = AnimationState::Hidden;
    bool IsVisible() override {
        return (hwnd && IsWindowVisible(hwnd) && animState != AnimationState::Hidden);
    }
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Draw();
    void OnClick(int x, int y);
    void OnDrag(int x, int y);
	void PositionWindow(RECT iconRect);
    void RefreshDevices();

    std::vector<AudioDeviceInfo> devices;

    // D2D Resources
    ID2D1Factory *pFactory; // shared resources
    IDWriteFactory *pWriteFactory;

    ID2D1HwndRenderTarget *pRenderTarget = nullptr;
    ID2D1SolidColorBrush *pBgBrush = nullptr;
    ID2D1SolidColorBrush *pFgBrush = nullptr;
    ID2D1SolidColorBrush *pAccentBrush = nullptr;
    ID2D1SolidColorBrush *pBorderBrush = nullptr;
    IDWriteTextFormat *pTextFormat = nullptr;

    ThemeConfig::Global style;
    HINSTANCE hInst;

    // State
    bool isHoveringOpenButton = false;
    bool isDropdownOpen = false; // Output device list toggle
    bool isDraggingSlider = false;
    bool isHoveringSlider = false;
    float cachedVolume = 0.0f;
    ULONGLONG lastAudioUpdate = 0;

    // Animation
    int targetX = 0;
    int targetY = 0;
    float currentAlpha = 0.0f;
    float currentOffset = 10.0f;

    float scrollOffset = 0.0f;
    bool isDraggingScrollbar = false;
    float dragStartY = 0.0f;
    float dragStartScrollOffset = 0.0f;
    D2D1_RECT_F scrollTrackRect = { 0 };
    D2D1_RECT_F scrollThumbRect = { 0 };
    float maxScroll = 0.0f;

    static ULONGLONG lastAnimTime;
    static ULONGLONG lastAutoCloseTime;
    void UpdateAnimation();

    D2D1_RECT_F sliderRect = {};
    D2D1_RECT_F switchRect = {};
    D2D1_RECT_F dropdownRect = {};
    std::vector<D2D1_RECT_F> deviceItemRects;
};