#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <vector>
#include "ThemeTypes.h"
#include "WorkspaceManager.h"

struct RenderContext {
    ID2D1RenderTarget *rt; /* D2D RESOURCES */
    ID2D1Factory *factory;
	ID2D1SolidColorBrush *bgBrush;
    ID2D1SolidColorBrush *textBrush;
    ID2D1SolidColorBrush *borderBrush;

    IDWriteFactory *writeFactory; /* FONTS */
    IWICImagingFactory *wicFactory;
    IDWriteTextFormat *textFormat;
    IDWriteTextFormat *boldTextFormat = nullptr;
    IDWriteTextFormat *largeTextFormat;
    IDWriteTextFormat *iconFormat;
    IDWriteTextFormat *emojiFormat = nullptr;

    int cpuUsage = 0; /* SYSTEM STATE */
    int ramUsage = 0;
    int gpuTemp = 0;
    float volume = 0.0f;
	bool isMuted = false;
    ID2D1Bitmap *appIcon = nullptr;

    const std::vector<WindowInfo> *windows; // Live app list
    std::vector<std::wstring> *pinnedApps;
    HWND foregroundWindow;
    WorkspaceManager *workspaces;

    float scale = 1.0f; /* WINDOW INFO */
    UINT dpi = 96;
    bool isVertical = false;
    HWND hwnd;
    float logicalWidth = 0.0f;
    float logicalHeight = 0.0f;
};