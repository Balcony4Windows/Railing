#pragma once
#include "Module.h"
#include <shellapi.h>
#include <map>

class DockModule : public Module {
    std::map<HWND, ID2D1Bitmap *> iconCache;
    float iconSize = 24.0f;
    float spacing = 8.0f;
    float animSpeed = 0.25f;
    float currentHighlightX = 0.0f;
    bool isHighlightInitialized = false;

    ID2D1Bitmap *GetOrLoadIcon(RenderContext &ctx, const WindowInfo &win) {
        size_t key = (size_t)win.hwnd;
        if (key == 0) key = std::hash<std::wstring>{}(win.exePath);

        if (iconCache.count((HWND)key)) return iconCache[(HWND)key];

        HICON hIcon = NULL;

        if (win.hwnd) {
            int reqSize = (iconSize > 24) ? ICON_BIG : ICON_SMALL;
            hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, reqSize, 0);
            if (!hIcon) hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, ICON_SMALL, 0);
            if (!hIcon) hIcon = (HICON)GetClassLongPtr(win.hwnd, GCLP_HICON);
        }
        else ExtractIconExW(win.exePath.c_str(), 0, &hIcon, NULL, 1); // Pinned apps

        if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);

        ID2D1Bitmap *bmp = nullptr;
        IWICBitmap *wicBitmap = nullptr;
        if (SUCCEEDED(ctx.wicFactory->CreateBitmapFromHICON(hIcon, &wicBitmap))) {
            IWICFormatConverter *converter = nullptr;
            ctx.wicFactory->CreateFormatConverter(&converter);
            if (converter) {
                converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut);
                ctx.rt->CreateBitmapFromWicBitmap(converter, nullptr, &bmp);
                converter->Release();
            }
            wicBitmap->Release();
        }
        return iconCache[(HWND)key] = bmp;
    }

public:
    DockModule(const ModuleConfig &cfg) : Module(cfg) {
        if (cfg.dockIconSize > 0) this->iconSize = cfg.dockIconSize;
        if (cfg.dockSpacing > 0) this->spacing = cfg.dockSpacing;
        if (cfg.dockAnimSpeed > 0) this->animSpeed = cfg.dockAnimSpeed;
    }

    ~DockModule() {
        for (auto &[k, v] : iconCache) if (v) v->Release();
    }

    float GetContentWidth(RenderContext &ctx) override {
        if (!ctx.windows || ctx.windows->empty()) return 0.0f;

        size_t count = ctx.windows->size();
        float width = (count * iconSize) + ((count - 1) * spacing);
        Style s = GetEffectiveStyle();
        return width + s.padding.left + s.padding.right + s.margin.left + s.margin.right;
    }

    void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override {
        if (!ctx.windows || ctx.windows->empty()) return;

        Style s = GetEffectiveStyle();
        if (s.has_bg) {
            D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(D2D1::RectF(x + s.margin.left, y + s.margin.top, x + w - s.margin.right, y + h - s.margin.bottom), s.radius, s.radius);
            ctx.bgBrush->SetColor(s.bg);
            ctx.rt->FillRoundedRectangle(bgRect, ctx.bgBrush);
        }

        size_t count = ctx.windows->size();
        float totalIconWidth = (count * iconSize) + ((count - 1) * spacing);
        float bgWidth = w - s.margin.left - s.margin.right;
        float startX = x + s.margin.left + ((bgWidth - totalIconWidth) / 2.0f);
        float bgHeight = h - s.margin.top - s.margin.bottom;
        float drawY = y + s.margin.top + ((bgHeight - iconSize) / 2.0f);

        float targetX = -1.0f;
        float searchCursor = startX;

        for (const auto &win : *ctx.windows) {
            if (win.hwnd == ctx.foregroundWindow) {
                targetX = searchCursor;
                break;
            }
            searchCursor += (iconSize + spacing);
        }

        if (targetX >= 0.0f) {
            if (!isHighlightInitialized) {
                currentHighlightX = targetX;
                isHighlightInitialized = true;
            }
            else {
                float diff = targetX - currentHighlightX;
                currentHighlightX += diff * animSpeed;
                if (abs(diff) < 0.5f) currentHighlightX = targetX;
                else InvalidateRect(ctx.hwnd, NULL, FALSE);
            }

            D2D1_RECT_F dest = D2D1::RectF(currentHighlightX, drawY, currentHighlightX + iconSize, drawY + iconSize);
            D2D1_ROUNDED_RECT activeRect = D2D1::RoundedRect(D2D1::RectF(dest.left - 2, dest.top - 2, dest.right + 2, dest.bottom + 2), 4, 4);
            
            ctx.bgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f));
            ctx.rt->FillRoundedRectangle(activeRect, ctx.bgBrush);
        }

        float drawCursor = startX;
        for (const auto &win : *ctx.windows) {
            ID2D1Bitmap *bmp = GetOrLoadIcon(ctx, win);
            if (bmp) {
                D2D1_RECT_F dest = D2D1::RectF(drawCursor, drawY, drawCursor + iconSize, drawY + iconSize);
                ctx.rt->DrawBitmap(bmp, dest);
            }
            drawCursor += (iconSize + spacing);
        }
        for (auto it = iconCache.begin(); it != iconCache.end();) {
            bool found = false;
            for (const auto &w : *ctx.windows) if (w.hwnd == it->first) found = true;
            if (!found) { if (it->second) it->second->Release(); it = iconCache.erase(it); }
            else ++it;
        }
    }
};