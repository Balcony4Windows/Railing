#pragma once
#include "Module.h"
#include "WorkspaceManager.h"
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include <set>
#include <cwctype>

class DockModule : public Module {
    std::map<HWND, ID2D1Bitmap *> iconCache;
    std::map<size_t, ID2D1Bitmap *> pinnedIconCache;

    ID2D1PathGeometry *pStarGeo = nullptr; /* Star symbol! Pinned apps */
    std::set<HWND> attentionWindows; // For Shell hooks
    std::vector<WindowInfo> stableList;

    float iconSize = 24.0f;
    float spacing = 8.0f;
    float animSpeed = 0.25f;
    float currentHighlightX = 0.0f;
    bool isHighlightInitialized = false;
    int cleanupCounter = 0;
    int updateThrottle = 0;

    size_t GetPathHash(const std::wstring &path) {
        std::wstring lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), std::towlower);
        return std::hash<std::wstring>{}(lower);
    }

    void CreateStarGeometry(ID2D1Factory *pFactory, float radius) {
        if (pStarGeo) return;
        pFactory->CreatePathGeometry(&pStarGeo);
        ID2D1GeometrySink *pSink = nullptr;
        if (SUCCEEDED(pStarGeo->Open(&pSink))) {
            pSink->BeginFigure(D2D1::Point2F(0, -radius), D2D1_FIGURE_BEGIN_FILLED);

            float inner = radius * 0.15f;
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(inner, -inner), D2D1::Point2F(radius, 0)));
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(inner, inner), D2D1::Point2F(0, radius)));
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(-inner, inner), D2D1::Point2F(-radius, 0)));
            pSink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(-inner, -inner), D2D1::Point2F(0, -radius)));
            pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
            pSink->Close();
            pSink->Release();
        }
    }

    ID2D1Bitmap *GetOrLoadIcon(RenderContext &ctx, const WindowInfo &win) {
        // Check Running Cache (Fastest)
        if (win.hwnd && iconCache.count(win.hwnd)) return iconCache[win.hwnd];

        size_t pathHash = GetPathHash(win.exePath);
        if (pinnedIconCache.count(pathHash)) {
            ID2D1Bitmap *existing = pinnedIconCache[pathHash];
            if (win.hwnd) {
                existing->AddRef();
                iconCache[win.hwnd] = existing;
                return existing;
            }
            return existing;
        }
        // Load from Disk (Expensive)
        HICON hIcon = NULL;
        bool shouldDestroy = false;
        if (!win.exePath.empty()) {
            int targetSize = (int)iconSize;
            if (targetSize < 32) targetSize = 32; else if (targetSize < 48) targetSize = 48;
            UINT id = 0;
            if (PrivateExtractIconsW(win.exePath.c_str(), 0, targetSize, targetSize, &hIcon, &id, 1, 0) > 0) {
                if (hIcon) shouldDestroy = true;
            }
        }

        if (!hIcon && win.hwnd) {
            hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, ICON_BIG, 0);
            if (!hIcon) hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, ICON_SMALL, 0);
            if (!hIcon) hIcon = (HICON)GetClassLongPtr(win.hwnd, GCLP_HICON);
        }

        if (!hIcon && !win.exePath.empty()) {
            if (ExtractIconExW(win.exePath.c_str(), 0, &hIcon, NULL, 1) > 0 && hIcon) shouldDestroy = true;
        }
        if (!hIcon) { hIcon = LoadIcon(NULL, IDI_APPLICATION); shouldDestroy = false; }

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

        if (shouldDestroy && hIcon) DestroyIcon(hIcon);
        if (win.hwnd) iconCache[win.hwnd] = bmp;
        else pinnedIconCache[pathHash] = bmp;

        return bmp;
    }

    void UpdateStableList(RenderContext &ctx) {
        if (!ctx.windows || !ctx.pinnedApps) return;
        if (++updateThrottle < 4) return;
        updateThrottle = 0;

        std::map<HWND, const WindowInfo *> currentMap;
        std::vector<const WindowInfo *> zOrderList;
        zOrderList.reserve(ctx.windows->size());

        for (const auto &w : *ctx.windows) {
            if (!IsWindowVisible(w.hwnd)) continue;
            RECT r; GetWindowRect(w.hwnd, &r);
            if ((r.right - r.left) < 20) continue;

            currentMap[w.hwnd] = &w;
            zOrderList.push_back(&w);
        }

        std::vector<WindowInfo> nextList;
        nextList.reserve(ctx.pinnedApps->size() + zOrderList.size());
        std::set<HWND> processedHwnds;

        // PHASE A: Pinned Apps
        for (const auto &path : *ctx.pinnedApps) {
            WindowInfo item = {};
            item.exePath = path;
            item.isPinned = true;
            item.hwnd = NULL;

            size_t lastSlash = path.find_last_of(L"\\/");
            item.title = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : L"App";
            size_t pathHash = GetPathHash(path);

            bool foundStable = false;
            for (const auto &old : stableList) {
                if (old.isPinned && old.exePath == path && old.hwnd && currentMap.count(old.hwnd)) {
                    if (GetPathHash(old.exePath) == pathHash) {
                        item.hwnd = old.hwnd;
                        item.title = currentMap[old.hwnd]->title;
                        foundStable = true;
                        break;
                    }
                }
            }
            if (!foundStable) {
                for (auto *curr : zOrderList) {
                    if (GetPathHash(curr->exePath) == pathHash) {
                        item.hwnd = curr->hwnd;
                        item.title = curr->title;
                        break;
                    }
                }
            }

            if (item.hwnd) processedHwnds.insert(item.hwnd);
            nextList.push_back(item);
        }

        // 3. PHASE B: Unpinned Apps (Preserve Old Order)
        for (const auto &old : stableList) {
            if (!old.isPinned && old.hwnd && currentMap.count(old.hwnd)) {
                if (processedHwnds.find(old.hwnd) == processedHwnds.end()) {
                    WindowInfo fresh = *currentMap[old.hwnd];
                    fresh.isPinned = false;
                    nextList.push_back(fresh);
                    processedHwnds.insert(old.hwnd);
                }
            }
        }

        // 4. PHASE C: New Arrivals
        for (auto *curr : zOrderList) {
            if (processedHwnds.find(curr->hwnd) == processedHwnds.end()) {
                WindowInfo newItem = *curr;
                newItem.isPinned = false;
                nextList.push_back(newItem);
                processedHwnds.insert(newItem.hwnd);
            }
        }
        stableList = nextList; // Commit
    }

    void FillHiddenWindowInfo(WindowInfo &info) {
        if (!IsWindow(info.hwnd)) return;

        wchar_t titleBuf[256];
        if (GetWindowTextW(info.hwnd, titleBuf, 256) > 0) {
            info.title = titleBuf;
        }

        DWORD pid;
        GetWindowThreadProcessId(info.hwnd, &pid);
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            wchar_t path[MAX_PATH];
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
                info.exePath = path;
            }
            CloseHandle(hProcess);
        }
    }

public:
    DockModule(const ModuleConfig &cfg) : Module(cfg) {
        if (cfg.dockIconSize > 0) this->iconSize = cfg.dockIconSize;
        if (cfg.dockSpacing > 0) this->spacing = cfg.dockSpacing;
        if (cfg.dockAnimSpeed > 0) this->animSpeed = cfg.dockAnimSpeed;
    }

    ~DockModule() {
        for (auto &[k, v] : iconCache) if (v) v->Release();
        if (pStarGeo) pStarGeo->Release();
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

        Style containerStyle = GetEffectiveStyle();
        Style itemStyle = config.itemStyle;

        if (containerStyle.has_bg) {
            D2D1_ROUNDED_RECT bgRect = D2D1::RoundedRect(
                D2D1::RectF(x + containerStyle.margin.left, y + containerStyle.margin.top, x + w - containerStyle.margin.right, y + h - containerStyle.margin.bottom),
                containerStyle.radius, containerStyle.radius
            );
            ctx.bgBrush->SetColor(containerStyle.bg);
            ctx.rt->FillRoundedRectangle(bgRect, ctx.bgBrush);
        }

        size_t count = stableList.size();
        float itemBoxWidth = iconSize + itemStyle.padding.left + itemStyle.padding.right;
        float itemSpacing = itemStyle.margin.left + itemStyle.margin.right;
        if (spacing > 0) itemSpacing = spacing;

        float totalWidth = (count * itemBoxWidth) + ((count - 1) * itemSpacing);
        float bgWidth = w - containerStyle.margin.left - containerStyle.margin.right;
        float startX = x + containerStyle.margin.left + ((bgWidth - totalWidth) / 2.0f);
        float availableH = h - containerStyle.margin.top - containerStyle.margin.bottom;
        float drawY = y + containerStyle.margin.top + ((availableH - iconSize) / 2.0f);
        float targetX = -1.0f;
        float searchCursor = startX;

        if (!pStarGeo && ctx.factory) {
            CreateStarGeometry(ctx.factory, 6.0f);
        }

        for (const auto &win : stableList) {
            if (win.hwnd == ctx.foregroundWindow) {
                targetX = searchCursor;
                break;
            }
            searchCursor += (itemBoxWidth + itemSpacing);
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

            D2D1_COLOR_F activeColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f);
            if (config.states.count("active")) {
                Style s = config.states.at("active");
                if (s.indicator.a > 0.0f) activeColor = s.indicator;
                else if (s.has_bg) activeColor = s.bg;
            }

            // Draw Indicator Dot/Bar
            float indW = 16.0f;
            float indH = 3.0f;
            float centerX = currentHighlightX + (itemBoxWidth / 2.0f);
            float bottomY = drawY + iconSize + 6.0f;

            D2D1_ROUNDED_RECT activeRect = D2D1::RoundedRect(
                D2D1::RectF(centerX - (indW / 2), bottomY, centerX + (indW / 2), bottomY + indH),
                2, 2
            );

            ctx.bgBrush->SetColor(activeColor);
            ctx.rt->FillRoundedRectangle(activeRect, ctx.bgBrush);
        }

        float drawCursor = startX;
        for (const auto &win : stableList) {
            if (win.hwnd == ctx.foregroundWindow && attentionWindows.count(win.hwnd))
                attentionWindows.erase(win.hwnd); /* Read notif */
            ID2D1Bitmap *bmp = GetOrLoadIcon(ctx, win);
            float iconX = drawCursor + itemStyle.padding.left;
            if (bmp) {
                D2D1_RECT_F dest = D2D1::RectF(iconX, drawY, iconX + iconSize, drawY + iconSize);

                float opacity = (win.hwnd && !IsWindowVisible(win.hwnd)) ? 0.5f : 1.0f;
                ctx.rt->DrawBitmap(bmp, dest, opacity);
            }

            if (attentionWindows.count(win.hwnd)) {
                float dotSize = 6.0f;
                float dotX = iconX + iconSize - dotSize + 1.0f;
                float dotY = drawY - 1.0f; // Slightly above icon

                D2D1_ELLIPSE dot = D2D1::Ellipse(
                    D2D1::Point2F(dotX + (dotSize / 2), dotY + (dotSize / 2)),
                    dotSize / 2, dotSize / 2
                );
                ctx.bgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Red));
                ctx.rt->FillEllipse(dot, ctx.bgBrush);
            }

            if (win.isPinned && pStarGeo) {
                float starX = iconX + iconSize - 2.0f;
                float starY = drawY + iconSize - 2.0f;

                D2D1::Matrix3x2F oldTransform;
                ctx.rt->GetTransform(&oldTransform); // Save current state
                ctx.rt->SetTransform(D2D1::Matrix3x2F::Translation(starX, starY));

                ctx.bgBrush->SetColor(D2D1::ColorF(0xFFD700)); // Gold
                ctx.rt->FillGeometry(pStarGeo, ctx.bgBrush);
                ctx.rt->SetTransform(oldTransform);
            }
            drawCursor += (itemBoxWidth + itemSpacing);
        }

        // Clean Icon Cache
        if (++cleanupCounter > 60) {
            cleanupCounter = 0;
            for (auto it = iconCache.begin(); it != iconCache.end();) {
                bool isUsed = false;
                for (const auto &w : stableList) {
                    if (w.hwnd == it->first) { isUsed = true; break; }
                }
                if (!isUsed) {
                    if (it->second) it->second->Release();
                    it = iconCache.erase(it);
                }
                else ++it;


            }

            for (auto it = pinnedIconCache.begin(); it != pinnedIconCache.end();) {
                bool isUsed = false;
                for (const auto &w : stableList) {
                    if (w.hwnd == NULL && w.isPinned && GetPathHash(w.exePath) == it->first)
                    {
                        isUsed = true; break;
                    }
                }
                if (!isUsed) {
                    if (it->second) it->second->Release();
                    it = pinnedIconCache.erase(it);
                }
                else ++it;
            }
        }
    }

    void SetAttention(HWND hwnd, bool active) {
        if (active) attentionWindows.insert(hwnd);
        else attentionWindows.erase(hwnd);
    }

    void ClearAttention(HWND hwnd) {
        if (attentionWindows.count(hwnd)) attentionWindows.erase(hwnd);
    }

    void InvalidateIcon(HWND hwnd) {
        if (hwnd && iconCache.count(hwnd)) {
            if (iconCache[hwnd]) iconCache[hwnd]->Release();
            iconCache.erase(hwnd);
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