#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <vector>

#include "ThemeTypes.h"
#include "Module.h"
#include "ThemeLoader.h"
#include "Types.h"

class RailingRenderer
{
public:
    struct SystemStatusData {
        int cpuUsage = 0;
        int ramUsage = 0;
        int gpuTemp = 0;
        int wifiSignal = 0;
        bool isWifiConnected = false;
        bool isMuted = false;
        float volume = 0.0f;
    };
    RailingRenderer(HWND hwnd, const ThemeConfig &config);
    ~RailingRenderer();

    void UpdateAudioStats(float volume, bool isMuted) {
        this->currentStats.volume = volume;
        this->currentStats.isMuted = isMuted;
    }

    ThemeConfig theme;
    std::vector<Module *> leftModules;
    std::vector<Module *> centerModules;
    std::vector<Module *> rightModules;

    SystemStatusData currentStats;

    void Reload();
    void Draw(const std::vector<WindowInfo> &windows, const std::vector<std::wstring> &pinnedApps, HWND activeWindow);
    void Resize();
    static void EnableBlur(HWND hwnd, DWORD nColor);
    void UpdateBlurRegion();
    bool HitTest(POINT pt);
    D2D1_RECT_F GetModuleRect(std::string moduleId);
    Module *GetModule(std::string id);
    RECT GetAppIconRect() { return iconClickRect; }

    WorkspaceManager *pWorkspaceManager;

    ID2D1Factory *GetFactory() const { return pFactory; }
    IWICImagingFactory *GetWICFactory() const { return pWICFactory; }
    IDWriteFactory *GetWriteFactory() const { return pWriteFactory; }
    IDWriteTextFormat *GetTextFormat() const { return pTextFormat; }
    IDWriteTextFormat *GetIconFormat() const { return pIconFormat; }
    void UpdateStats(const SystemStatusData &data) { currentStats = data; }
private:
    HWND hwnd;

    ID2D1Factory *pFactory = nullptr;
    ID2D1HwndRenderTarget *pRenderTarget = nullptr;
    IDWriteFactory *pWriteFactory = nullptr;
    IDWriteTextFormat *pTextFormatBold = nullptr;
    IDWriteTextFormat *pTextFormat = nullptr;
    IDWriteTextFormat *pEmojiFormat = nullptr;
    IDWriteTextFormat *pIconFormat = nullptr;
    ID2D1SolidColorBrush *pTextBrush = nullptr;
    IDWriteInlineObject *pEllipsis = nullptr;
    ID2D1SolidColorBrush *pBgBrush = nullptr;
    ID2D1SolidColorBrush *pBorderBrush = nullptr;
    IWICImagingFactory *pWICFactory = nullptr;

    ID2D1Bitmap *pAppIcon = nullptr;
    RECT iconClickRect = {};
    std::map<std::string, D2D1_RECT_F> moduleRects;

    void LoadAppIcon();
    Module *FindModuleRecursive(const std::vector<Module *> &list, const std::string &id);

    static inline DWORD D2D1ColorFToBlurColor(const D2D1_COLOR_F &c)
    {
        BYTE a = (BYTE)(c.a * 255.0f + 0.5f);
        BYTE r = (BYTE)(c.r * 255.0f + 0.5f);
        BYTE g = (BYTE)(c.g * 255.0f + 0.5f);
        BYTE b = (BYTE)(c.b * 255.0f + 0.5f);
        return (DWORD)((a << 24) | (b << 16) | (g << 8) | r); // ABGR order
    }
};