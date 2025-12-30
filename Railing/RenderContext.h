#pragma once
#include <d2d1.h>
#include <dwrite.h>

struct RenderContext {
    ID2D1RenderTarget *rt;
    IDWriteTextFormat *textFormat;
    ID2D1SolidColorBrush *textBrush;
    ID2D1SolidColorBrush *bgBrush;
    ID2D1SolidColorBrush *borderBrush;
    ID2D1Bitmap *appIcon = nullptr;

    IDWriteFactory *writeFactory;
    IDWriteTextFormat *largeTextFormat;
    IDWriteTextFormat *iconFormat;

    int cpuUsage = 0;
    int ramUsage = 0;
    int gpuTemp = 0;
    float volume;
	bool isMuted;

    float logicalWidth;
    float logicalHeight;
    float pillOpacity = 3.0f;
    float rounding = 4.0f;
    float moduleGap = 8.0f;
    float innerPadding = 6.0f;
    float scale;
    UINT dpi;
    HWND hwnd;

    bool use24HourTime = false;
    bool isVertical = false;
    D2D1_COLOR_F accentColor;
};