#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include "AudioBackend.h"

struct AudioDeviceInfo {
    std::wstring name;
    std::wstring id;
};

class VolumeFlyout {
public:
    VolumeFlyout(HINSTANCE hInst);
    ~VolumeFlyout();

    void Toggle(int anchorX, int anchorY); // Open/Close
    bool IsVisible();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Draw();
    void OnClick(int x, int y);
    void OnDrag(int x, int y);

    // device management
    void RefreshDevices();

    HWND hwnd = nullptr;
    AudioBackend audio;
    std::vector<AudioDeviceInfo> devices;

    // D2D Resources
    static ID2D1Factory *pFactory;
    ID2D1HwndRenderTarget *pRenderTarget = nullptr;
    ID2D1SolidColorBrush *pBgBrush = nullptr;
    ID2D1SolidColorBrush *pFgBrush = nullptr;
    ID2D1SolidColorBrush *pAccentBrush = nullptr;
    static IDWriteFactory *pWriteFactory;
    static IDWriteTextFormat *pTextFormat;

    // State
    bool isDropdownOpen = false; // Output device list toggle
    bool isDraggingSlider = false;
    bool isHoveringSlider = false;
    float cachedVolume = 0.0f; // for smooth sliding
    ULONGLONG lastAudioUpdate = 0;

    // Animation
    int targetX = 0;
    int targetY = 0;
    enum class AnimationState { Hidden, Entering, Visible, Exiting };
    AnimationState animState = AnimationState::Hidden;
    float currentAlpha = 0.0f;
    float currentOffset = 10.0f; // Start 10px below final position
    static ULONGLONG lastAnimTime;
    static ULONGLONG lastAutoCloseTime;
    void UpdateAnimation();

    D2D1_RECT_F sliderRect = {};
    D2D1_RECT_F switchRect = {};
    D2D1_RECT_F dropdownRect = {};
    std::vector<D2D1_RECT_F> deviceItemRects;
};