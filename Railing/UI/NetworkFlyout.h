#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mmsystem.h>
#include "BarInstance.h"
#include "NetworkBackend.h"
#pragma comment(lib, "winmm.lib")

class NetworkFlyout : public IFlyout
{
public:
    NetworkFlyout(BarInstance *owner, HINSTANCE hInst, ID2D1Factory *pSharedFactory, IDWriteFactory *pSharedWriteFactory, IDWriteTextFormat *pFormat, IDWriteTextFormat *pIconFormat, const ThemeConfig &config);
    ~NetworkFlyout();

    void Toggle(RECT iconRect);
    void Hide() override;
    bool IsVisible() override { return hwnd != nullptr && IsWindowVisible(hwnd); }
    HWND GetHwnd() const { return hwnd; }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    BarInstance *ownerBar;
    HINSTANCE hInst;
    HWND hwnd = nullptr;

    // D2D Resources
    ID2D1Factory *pFactory;
    IDWriteFactory *pWriteFactory;
    IDWriteTextFormat *pTextFormat;
    IDWriteTextFormat *pIconFormat;
    ID2D1HwndRenderTarget *pRenderTarget = nullptr;
    ID2D1SolidColorBrush *pBgBrush = nullptr;
    ID2D1SolidColorBrush *pFgBrush = nullptr;
    ID2D1SolidColorBrush *pAccentBrush = nullptr;
    ID2D1SolidColorBrush *pBorderBrush = nullptr;
    ID2D1SolidColorBrush *pDimBrush = nullptr;

    ThemeConfig style;
    NetworkBackend backend;

    // Logic
    std::vector<WifiNetwork> cachedNetworks;
    std::wstring selectedSSID = L"";
    std::wstring connectionStatusMsg = L"";
    std::wstring passwordInput = L"";
    
    // Threading
    std::thread workerThread;
    std::atomic<bool> closing{ false };
    std::atomic<uint64_t> scanToken{ 0 };
    std::atomic<bool> isBusy{ false };

    // Simple Animation State
    UINT_PTR mmTimerId = 0;
    float currentAlpha = 0.0f;
    int targetX = 0;
    int targetY = 0;

    // Layout
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;
    bool isDraggingScrollbar = false;
    float dragStartY = 0.0f;
    float dragStartScrollOffset = 0.0f;

    // Hit Rects
    D2D1_RECT_F scrollTrackRect = {};
    D2D1_RECT_F scrollThumbRect = {};
    D2D1_RECT_F connectBtnRect = {};
    D2D1_RECT_F passwordBoxRect = {};
    D2D1_RECT_F refreshBtnRect = {};
    std::vector<std::pair<D2D1_RECT_F, std::wstring>> networkItemRects;

    void CreateDeviceResources();
    void Draw();
    void PositionWindow(RECT iconRect);

    void OnClick(int x, int y);
    void OnScroll(int delta);
    void OnDrag(int x, int y);
    void OnChar(wchar_t c);

    void ScanAsync();
    void ConnectAsync(WifiNetwork net, std::wstring password);
    void DrawSignalIcon(D2D1_RECT_F rect, int quality);
    void DrawLockIcon(D2D1_RECT_F rect);
};