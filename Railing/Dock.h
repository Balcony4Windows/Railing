#pragma once
#include "RenderContext.h"
#include "Types.h"
#include <vector>
#include <map>
#include <wincodec.h>

class Dock {
public:
    Dock() : pWICFactory(nullptr) {}
    Dock(IWICImagingFactory *wic) : pWICFactory(wic) { if (wic) wic->AddRef(); }
    ~Dock();

    void Update(const std::vector<WindowInfo> &windows);
    struct ClickTarget { RECT rect; HWND hwnd; };
    bool Draw(const RenderContext &ctx, HWND activeWindow, std::vector<ClickTarget> &outTargets);
    void SetWICFactory(IWICImagingFactory *wic) {
        if (pWICFactory) pWICFactory->Release();
        pWICFactory = wic;
        if (pWICFactory) pWICFactory->AddRef();
    }
private:
    struct RenderItem {
        WindowInfo win = {};
        float currentAlpha = 0.0f;
        float currentX = 0.0f;
        float targetX = 0.0f;
        bool isDying = 0;
    };

    IWICImagingFactory *pWICFactory;
    std::vector<RenderItem> renderList;
    std::map<HWND, ID2D1Bitmap *> iconCache;

    ID2D1Bitmap *GetOrLoadIcon(HWND hwnd, ID2D1RenderTarget *rt);
};