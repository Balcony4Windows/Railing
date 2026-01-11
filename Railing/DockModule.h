#pragma once
#include "Module.h"
#include "WorkspaceManager.h"
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include <set>

class DockModule : public Module {
    std::map<HWND, ID2D1Bitmap *> iconCache;

    std::vector<WindowInfo> stableList;

    float iconSize = 24.0f;
    float spacing = 8.0f;
    float animSpeed = 0.25f;
    float currentHighlightX = 0.0f;
    bool isHighlightInitialized = false;

    ID2D1Bitmap *GetOrLoadIcon(RenderContext &ctx, const WindowInfo &win) {
        size_t key = (size_t)win.hwnd;
        if (key == 0) key = std::hash<std::wstring>{}(win.exePath); // Pinned apps

        if (iconCache.count((HWND)key)) return iconCache[(HWND)key];

        HICON hIcon = NULL;
        if (win.hwnd) {
            int reqSize = (iconSize > 24) ? ICON_BIG : ICON_SMALL;
            hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, reqSize, 0);
            if (!hIcon) hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, ICON_SMALL, 0);
            if (!hIcon) hIcon = (HICON)GetClassLongPtr(win.hwnd, GCLP_HICON);
        }
        else ExtractIconExW(win.exePath.c_str(), 0, &hIcon, NULL, 1);

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

    // This function ensures the list doesn't jump around
    void UpdateStableList(RenderContext &ctx) {
        std::set<HWND> currentFrameWindows;

        if (ctx.windows) {
            for (const auto &w : *ctx.windows) {
                currentFrameWindows.insert(w.hwnd);

                auto it = std::find_if(stableList.begin(), stableList.end(),
                    [&](const WindowInfo &existing) { return existing.hwnd == w.hwnd; });

                if (it == stableList.end()) stableList.push_back(w);
            }
        }

        if (ctx.workspaces) {
            for (auto const &[hwnd, wksp] : ctx.workspaces->managedWindows) {
                if (currentFrameWindows.count(hwnd)) continue; // Already added
                if (!IsWindow(hwnd)) continue;

                currentFrameWindows.insert(hwnd);

                auto it = std::find_if(stableList.begin(), stableList.end(),
                    [&](const WindowInfo &existing) { return existing.hwnd == hwnd; });

                if (it == stableList.end()) {
                    WindowInfo info;
                    info.hwnd = hwnd;
                    stableList.push_back(info);
                }
            }
        }

        // 3. Remove Closed Windows
        stableList.erase(std::remove_if(stableList.begin(), stableList.end(),
            [&](const WindowInfo &w) {
                if (w.hwnd == NULL) return false;
                return currentFrameWindows.find(w.hwnd) == currentFrameWindows.end();
            }),
            stableList.end());
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
        UpdateStableList(ctx); // Sync Data

        size_t count = stableList.size();
        if (count == 0) return 0.0f;

        float width = (count * iconSize) + ((count - 1) * spacing);
        Style s = GetEffectiveStyle();
        return width + s.padding.left + s.padding.right + s.margin.left + s.margin.right;
    }

    void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override {
        if (stableList.empty()) return;

        Style s = GetEffectiveStyle();
        if (s.has_bg) {
            D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(D2D1::RectF(x + s.margin.left, y + s.margin.top, x + w - s.margin.right, y + h - s.margin.bottom), s.radius, s.radius);
            ctx.bgBrush->SetColor(s.bg);
            ctx.rt->FillRoundedRectangle(bgRect, ctx.bgBrush);
        }

        size_t count = stableList.size();
        float totalIconWidth = (count * iconSize) + ((count - 1) * spacing);
        float bgWidth = w - s.margin.left - s.margin.right;
        float startX = x + s.margin.left + ((bgWidth - totalIconWidth) / 2.0f);
        float bgHeight = h - s.margin.top - s.margin.bottom;
        float drawY = y + s.margin.top + ((bgHeight - iconSize) / 2.0f);
        float targetX = -1.0f;
        float searchCursor = startX;

        for (const auto &win : stableList) {
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
        for (const auto &win : stableList) {
            ID2D1Bitmap *bmp = GetOrLoadIcon(ctx, win);
            if (bmp) {
                D2D1_RECT_F dest = D2D1::RectF(drawCursor, drawY, drawCursor + iconSize, drawY + iconSize);
                float opacity = 1.0f;
                if (!IsWindowVisible(win.hwnd)) opacity = 0.5f;

                ctx.rt->DrawBitmap(bmp, dest, opacity);
            }
            drawCursor += (iconSize + spacing);
        }

        // Clean Icon Cache
        for (auto it = iconCache.begin(); it != iconCache.end();) {
            bool found = false;
            for (const auto &w : stableList) if (w.hwnd == it->first) found = true;
            if (!found) { if (it->second) it->second->Release(); it = iconCache.erase(it); }
            else ++it;
        }
    }

    int GetCount() const {
        return (int)stableList.size();
    }

    std::wstring GetTitleAtIndex(int index) {
        if (index >= 0 && index < stableList.size()) {
            // If it's a pinned app, the title might be empty. Fallback to EXE name.
            if (stableList[index].title.empty() && !stableList[index].exePath.empty()) {
                std::wstring path = stableList[index].exePath;
                size_t lastSlash = path.find_last_of(L"\\");
                if (lastSlash != std::string::npos) return path.substr(lastSlash + 1);
                return path;
            }
            return stableList[index].title;
        }
        return L"";
    }

    HWND GetWindowAtIndex(int index) {
        if (index >= 0 && index < stableList.size()) return stableList[index].hwnd;
        return NULL;
    }

    WindowInfo GetWindowInfoAtIndex(int index) {
        return (index >= 0 && index < stableList.size()) ? stableList[index] : WindowInfo{};
    }
};