#pragma once
#define NOMINMAX
#include "Module.h"
#include "WorkspaceManager.h"
#include "PinnedAppsIO.h"
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include <set>
#include <cwctype>
#include "DockPreviewWindow.h"
#pragma comment(lib, "version.lib")

struct DockItem {
    std::wstring exePath;
    size_t pathHash;
    bool isPinned = false;
    std::wstring title;
    std::vector<HWND> windows;
};

class DockModule : public Module {
    std::map<HWND, ID2D1Bitmap *> iconCache;
    std::map<size_t, ID2D1Bitmap *> pinnedIconCache;
    std::vector<PinnedAppEntry> m_pinnedApps;

    std::set<HWND> attentionWindows;
    std::vector<DockItem> stableList;

    ULONGLONG lastAnimTime = 0;
    float iconSize = 24.0f;
    float spacing = 8.0f;
    float animSpeed = 0.25f;

    float currentHighlightPos = 0.0f;
    bool isHighlightInitialized = false;

    int cleanupCounter = 0;
    int keepAliveFrames = 0;

    size_t GetPathHash(const std::wstring &path) {
        std::wstring lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), std::towlower);
        return std::hash<std::wstring>{}(lower);
    }

    ID2D1Bitmap *GetItemIcon(RenderContext &ctx, const DockItem &item) {
        if (pinnedIconCache.count(item.pathHash)) return pinnedIconCache[item.pathHash];
        HWND hIconSource = item.windows.empty() ? NULL : item.windows[0];
        if (hIconSource && iconCache.count(hIconSource)) return iconCache[hIconSource];

        WindowInfo tempInfo = {};
        tempInfo.hwnd = hIconSource;
        tempInfo.exePath = item.exePath;
        tempInfo.pathHash = item.pathHash;
        tempInfo.isPinned = item.isPinned;
        return GetOrLoadIcon(ctx, tempInfo);
    }

    std::pair<std::wstring, int> GetEffectiveIconPath(const WindowInfo &win) {
        std::wstring path = win.exePath;
        int index = 0;

        if (win.isPinned) {
            size_t h = GetPathHash(win.exePath);
            for (const auto &entry : m_pinnedApps) {
                if (GetPathHash(entry.path) == h && !entry.iconPath.empty()) {
                    path = entry.iconPath;
                    index = entry.iconIndex;
                    break;
                }
            }
        }
        return { path, index };
    }

    ID2D1Bitmap *GetOrLoadIcon(RenderContext &ctx, const WindowInfo &win) {
        if (win.hwnd && iconCache.count(win.hwnd)) return iconCache[win.hwnd];
        size_t pathHash = GetPathHash(win.exePath);
        if (pinnedIconCache.count(pathHash)) {
            ID2D1Bitmap *existing = pinnedIconCache[pathHash];
            if (win.hwnd) {
                existing->AddRef();
                iconCache[win.hwnd] = existing;
            }
            return existing;
        }

        auto [loadPath, loadIndex] = GetEffectiveIconPath(win);
        HICON hIcon = NULL;
        bool shouldDestroy = false;

        if (!loadPath.empty()) {
            int targetSize = (int)iconSize;
            if (targetSize < 32) targetSize = 32; else if (targetSize < 48) targetSize = 48;
            UINT id = 0;
            if (PrivateExtractIconsW(loadPath.c_str(), loadIndex, targetSize, targetSize, &hIcon, &id, 1, 0) > 0) {
                if (hIcon) shouldDestroy = true;
            }
            if (!hIcon && ExtractIconExW(loadPath.c_str(), 0, &hIcon, NULL, 1) > 0 && hIcon) {
                shouldDestroy = true;
            }
        }

        if (!hIcon && win.hwnd) {
            hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, ICON_BIG, 0);
            if (!hIcon) hIcon = (HICON)SendMessage(win.hwnd, WM_GETICON, ICON_SMALL, 0);
            if (!hIcon) hIcon = (HICON)GetClassLongPtr(win.hwnd, GCLP_HICON);
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

    bool PruneDeadWindows(HWND hwnd = NULL) {
        bool changed = false;
        for (auto &item : stableList) {
            auto it = item.windows.begin();
            while (it != item.windows.end()) {
                HWND h = *it;
                if (!IsWindow(h)) {
                    if (h == optimisticHwnd) {
                        optimisticHwnd = NULL;
                        optimisticTime = 0;
                    }
                    it = item.windows.erase(it);
                    changed = true;
                }
                else {
                    ++it;
                }
            }
        }

        if (changed) {
            isDirty = true;
            if (hwnd) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return changed;
    }

    void UpdateStableList(RenderContext &ctx) {
        if (!isDirty && !optimisticHwnd) return;
        if (!ctx.windows) return;

        isDirty = false;
        std::vector<const WindowInfo *> zOrderList;

        for (const auto &w : *ctx.windows) {
            if (IsWindow(w.hwnd)) {
                if (IsWindowVisible(w.hwnd) || w.hwnd == optimisticHwnd) {
                    RECT r; GetWindowRect(w.hwnd, &r);
                    if ((r.right - r.left) >= 20) zOrderList.push_back(&w);
                }
            }
        }

        std::map<size_t, std::vector<HWND>> liveWindows;
        std::map<size_t, std::wstring> titles;

        for (const auto *win : zOrderList) {
            size_t h = (win->pathHash != 0) ? win->pathHash : GetPathHash(win->exePath);
            liveWindows[h].push_back(win->hwnd);
            if (titles[h].empty()) titles[h] = win->title;
        }

        std::vector<DockItem> newList;
        std::set<size_t> processedHashes;

        for (const auto &oldItem : stableList) {
            size_t h = oldItem.pathHash;
            bool isPinned = IsPinned(oldItem.exePath);
            bool hasWindows = liveWindows.count(h);

            if (isPinned || hasWindows) {
                DockItem item = oldItem;
                item.isPinned = isPinned;
                if (hasWindows) item.title = titles[h];

                std::vector<HWND> mergedWindows;
                const auto &currentLive = liveWindows[h];

                for (HWND oldHwnd : oldItem.windows) {
                    if (std::find(currentLive.begin(), currentLive.end(), oldHwnd) != currentLive.end()) {
                        mergedWindows.push_back(oldHwnd);
                    }
                }
                for (HWND liveHwnd : currentLive) {
                    bool alreadyHas = false;
                    for (HWND existing : mergedWindows) {
                        if (existing == liveHwnd) { alreadyHas = true; break; }
                    }
                    if (!alreadyHas) mergedWindows.push_back(liveHwnd);
                }

                item.windows = mergedWindows;
                newList.push_back(item);
                processedHashes.insert(h);
            }
        }

        for (const auto *win : zOrderList) {
            size_t h = (win->pathHash != 0) ? win->pathHash : GetPathHash(win->exePath);

            if (processedHashes.find(h) == processedHashes.end()) {
                DockItem newItem = {};
                newItem.exePath = win->exePath;
                newItem.pathHash = h;
                newItem.isPinned = false;
                newItem.title = win->title;
                newItem.windows = liveWindows[h];

                newList.push_back(newItem);
                processedHashes.insert(h);
            }
        }

        for (PinnedAppEntry &entry : m_pinnedApps) {
            size_t h = GetPathHash(entry.path);
            if (processedHashes.find(h) == processedHashes.end()) {
                DockItem item = {};
                item.exePath = entry.path;
                item.pathHash = h;
                item.isPinned = true;
                item.title = entry.name;
                newList.push_back(item);
                processedHashes.insert(h);
            }
        }

        stableList = newList;
    }

public:
    DockPreviewWindow *m_previewWin = nullptr;
    bool suppressPreview = false;

    HWND optimisticHwnd = NULL;
    ULONGLONG optimisticTime = 0;

    DockModule(const ModuleConfig &cfg) : Module(cfg) {
        if (cfg.dockIconSize > 0) this->iconSize = cfg.dockIconSize;
        if (cfg.dockSpacing > 0) this->spacing = cfg.dockSpacing;
        if (cfg.dockAnimSpeed > 0) this->animSpeed = cfg.dockAnimSpeed;

        m_pinnedApps = PinnedAppsIO::Load();
    }

    ~DockModule() {
        for (auto &[k, v] : iconCache) if (v) v->Release();
    }

    void SetOptimisticFocus(HWND hwnd) {
        optimisticHwnd = hwnd;
        optimisticTime = GetTickCount64();
    }

    bool isDirty = true;
    void MarkDirty() { isDirty = true; }

    void PinApp(std::wstring path, std::wstring name = L"", std::wstring iconPath = L"", int iconIndex = 0) {
        for (PinnedAppEntry &app : m_pinnedApps) {
            if (GetPathHash(app.path) == GetPathHash(path)) return;
        }
        m_pinnedApps.push_back({ path, L"", name, iconPath, iconIndex });
        PinnedAppsIO::Save(m_pinnedApps);
    }

    void UnpinApp(std::wstring path) {
        auto it = std::remove_if(m_pinnedApps.begin(), m_pinnedApps.end(),
            [&](const PinnedAppEntry &e) { return GetPathHash(e.path) == GetPathHash(path); });
        if (it != m_pinnedApps.end()) {
            m_pinnedApps.erase(it, m_pinnedApps.end());
            PinnedAppsIO::Save(m_pinnedApps);
        }
    }

    bool IsPinned(const std::wstring &path) {
        size_t h = GetPathHash(path);
        for (const auto &app : m_pinnedApps) {
            if (GetPathHash(app.path) == h) return true;
        }
        return false;
    }

    bool useThumbnails = false;
    struct PreviewState {
        bool active = false;
        int groupIndex = -1;
        D2D1_RECT_F bounds = {};
        int hoveredRowIndex = -1;
        int spawnFrames = 0;
        RECT lastIconRect = {};
    } previewState;

    int GetWindowCountAtIndex(int index) {
        if (index >= 0 && index < stableList.size()) {
            return (int)stableList[index].windows.size();
        }
        return 0;
    }

    bool IsMouseInPreviewOrGap() {
        if (!m_previewWin) return false;

        POINT cursor; GetCursorPos(&cursor);
        RECT winRect;
        if (!GetWindowRect(m_previewWin->GetHwnd(), &winRect)) return false;

        if (PtInRect(&winRect, cursor)) return true;
        if (PtInRect(&previewState.lastIconRect, cursor)) return true;

        RECT gapRect;
        gapRect.left = std::min<LONG>(winRect.left, previewState.lastIconRect.left) - 20;
        gapRect.right = std::max<LONG>(winRect.right, previewState.lastIconRect.right) + 20;
        gapRect.top = std::min<LONG>(winRect.bottom, previewState.lastIconRect.top) + 20;
        gapRect.bottom = std::max<LONG>(winRect.top, previewState.lastIconRect.bottom) + 20;

        if (PtInRect(&gapRect, cursor)) return true;
        return false;
    }

    float GetContentWidth(RenderContext &ctx) override {
        if (optimisticHwnd && !IsWindow(optimisticHwnd)) {
            optimisticHwnd = NULL;
            optimisticTime = 0;
            isDirty = true;
        }

        PruneDeadWindows(ctx.hwnd);
        UpdateStableList(ctx);

        size_t count = stableList.size();
        if (count == 0) return 0.0f;

        float length = (count * iconSize) + ((count - 1) * spacing);
        Style s = GetEffectiveStyle();

        if (config.position == "left" || config.position == "right")
            return length + s.padding.top + s.padding.bottom + s.margin.top + s.margin.bottom;
        else
            return length + s.padding.left + s.padding.right + s.margin.left + s.margin.right;
    }

    void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override {
        if (!m_previewWin && ctx.factory && ctx.writeFactory)
            m_previewWin = new DockPreviewWindow(ctx.factory, ctx.writeFactory, ctx.wicFactory);

        if (optimisticHwnd && !IsWindow(optimisticHwnd)) {
            optimisticHwnd = NULL;
            optimisticTime = 0;
            isDirty = true;
        }

        PruneDeadWindows(ctx.hwnd);
        UpdateStableList(ctx);
        if (stableList.empty()) return;

        bool isVertical = (config.position == "left" || config.position == "right");

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

        // Calculate Main Axis dimensions
        float itemBoxSize = iconSize; // Assume square
        // Add padding to main axis size
        float itemBoxStride = itemBoxSize + (isVertical ? (itemStyle.padding.top + itemStyle.padding.bottom) : (itemStyle.padding.left + itemStyle.padding.right));

        float itemSpacing = isVertical ? (itemStyle.margin.top + itemStyle.margin.bottom) : (itemStyle.margin.left + itemStyle.margin.right);
        if (spacing > 0) itemSpacing = spacing;

        float totalLength = (count * itemBoxStride) + ((count - 1) * itemSpacing);

        float startMain = 0.0f;
        float fixedCross = 0.0f; // Center coordinate on the cross axis

        if (isVertical) {
            float bgHeight = h - containerStyle.margin.top - containerStyle.margin.bottom;
            startMain = y + containerStyle.margin.top + ((bgHeight - totalLength) / 2.0f);

            float bgWidth = w - containerStyle.margin.left - containerStyle.margin.right;
            fixedCross = x + containerStyle.margin.left + ((bgWidth - iconSize) / 2.0f);
        }
        else {
            float bgWidth = w - containerStyle.margin.left - containerStyle.margin.right;
            startMain = x + containerStyle.margin.left + ((bgWidth - totalLength) / 2.0f);

            float bgHeight = h - containerStyle.margin.top - containerStyle.margin.bottom;
            fixedCross = y + containerStyle.margin.top + ((bgHeight - iconSize) / 2.0f);
        }

        float targetPos = -1.0f;
        float searchCursor = startMain;

        HWND activeWin = (optimisticHwnd && (GetTickCount64() - optimisticTime < 500)) ? optimisticHwnd : ctx.foregroundWindow;
        const DockItem *activeItem = nullptr;

        for (const auto &item : stableList) {
            bool isGroupActive = false;
            for (HWND hw : item.windows) {
                if (hw == activeWin) {
                    isGroupActive = true; break;
                }
            }
            if (isGroupActive) {
                targetPos = searchCursor;
                activeItem = &item;
                break;
            }
            searchCursor += (itemBoxStride + itemSpacing);
        }

        if (targetPos >= 0.0f) {
            ULONGLONG now = GetTickCount64();

            if (!isHighlightInitialized) {
                currentHighlightPos = targetPos;
                isHighlightInitialized = true;
            }
            else {
                float dt = (float)(now - lastAnimTime) / 1000.0f;
                if (dt > 0.1f) dt = 0.016f;
                float damping = 1.0f - std::pow(0.01f, dt * (animSpeed * 10.0f));

                float diff = targetPos - currentHighlightPos;
                currentHighlightPos += diff * damping;
                if (abs(diff) < 0.5f) {
                    currentHighlightPos = targetPos;
                    if (activeItem) InvalidateRect(ctx.hwnd, NULL, FALSE);
                }
                else InvalidateRect(ctx.hwnd, NULL, FALSE);
            }
            lastAnimTime = now;

            D2D1_COLOR_F activeColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f);
            if (config.states.count("active")) {
                Style s = config.states.at("active");
                if (s.indicator.a > 0.0f) activeColor = s.indicator;
                else if (s.has_bg) activeColor = s.bg;
            }

            // Draw Indicator
            float barThickness = 3.0f;
            float barLength = 16.0f;
            if (activeItem && activeItem->windows.size() > 1) barLength = 28.0f;

            D2D1_ROUNDED_RECT activeRect;

            if (isVertical) {
                // Vertical Bar indicator: Left or Right side based on alignment
                bool onRight = (config.position == "left");
                float barX = onRight ? (fixedCross + iconSize + 6) : (fixedCross - 6 - barThickness);

                float barY = currentHighlightPos + (itemBoxStride / 2) - (barLength / 2);
                activeRect = D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barThickness, barY + barLength), 2, 2);
            }
            else {
                // Horizontal Bar indicator: Top or Bottom
                bool onBottom = (config.position == "bottom");
                float barY = onBottom ? (fixedCross + iconSize + 6) : (fixedCross - 6 - barThickness);

                float barX = currentHighlightPos + (itemBoxStride / 2) - (barLength / 2);
                activeRect = D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barLength, barY + barThickness), 2, 2);
            }

            ctx.bgBrush->SetColor(activeColor);
            ctx.rt->FillRoundedRectangle(activeRect, ctx.bgBrush);
        }
        else {
            isHighlightInitialized = false;
            if (optimisticHwnd) InvalidateRect(ctx.hwnd, NULL, FALSE);
        }

        float drawCursor = startMain;
        for (const auto &item : stableList) {
            for (HWND hw : item.windows) {
                if (hw == ctx.foregroundWindow && attentionWindows.count(hw)) attentionWindows.erase(hw);
            }

            ID2D1Bitmap *bmp = GetItemIcon(ctx, item);

            float iconX, iconY;
            if (isVertical) {
                iconX = fixedCross; // Centered
                iconY = drawCursor + itemStyle.padding.top;
            }
            else {
                iconX = drawCursor + itemStyle.padding.left;
                iconY = fixedCross; // Centered
            }

            if (bmp) {
                D2D1_RECT_F dest = D2D1::RectF(iconX, iconY, iconX + iconSize, iconY + iconSize);
                float opacity = item.windows.empty() ? 0.5f : 1.0f;
                ctx.rt->DrawBitmap(bmp, dest, opacity);
            }

            int winCount = (int)item.windows.size();
            if (winCount > 0 && activeItem != &item) {
                int dotsToShow = (std::min)(winCount, 3);
                float dotSize = 4.0f; float dotGap = 2.0f;
                float totalDotsLen = (dotsToShow * dotSize) + ((dotsToShow - 1) * dotGap);

                if (isVertical) {
                    // Vertical: Dots on the side
                    bool onRight = (config.position == "left");
                    float dotX = onRight ? (iconX + iconSize + 7.0f) : (iconX - 7.0f - dotSize);
                    float clusterStartY = iconY + (iconSize / 2.0f) - (totalDotsLen / 2.0f);

                    for (int d = 0; d < dotsToShow; d++) {
                        float dy = clusterStartY + (d * (dotGap + dotSize));
                        D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dotX + (dotSize / 2), dy + (dotSize / 2)), dotSize / 2, dotSize / 2);
                        ctx.bgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.4f));
                        ctx.rt->FillEllipse(dot, ctx.bgBrush);
                    }
                }
                else {
                    // Horizontal: Dots on bottom/top
                    bool onBottom = (config.position == "bottom");
                    float dotY = onBottom ? (iconY + iconSize + 7.0f) : (iconY - 7.0f - dotSize);
                    float clusterStartX = iconX + (iconSize / 2.0f) - (totalDotsLen / 2.0f);

                    for (int d = 0; d < dotsToShow; d++) {
                        float dx = clusterStartX + (d * (dotGap + dotSize));
                        D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(dx + (dotSize / 2), dotY + (dotSize / 2)), dotSize / 2, dotSize / 2);
                        ctx.bgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.4f));
                        ctx.rt->FillEllipse(dot, ctx.bgBrush);
                    }
                }
            }

            bool hasAttention = false;
            for (HWND hw : item.windows) {
                if (attentionWindows.count(hw)) { hasAttention = true; break; }
            }
            if (hasAttention) {
                D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(iconX + iconSize - 2, iconY + 2), 3, 3);
                ctx.bgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Red));
                ctx.rt->FillEllipse(dot, ctx.bgBrush);
            }

            drawCursor += (itemBoxStride + itemSpacing);
        }

        bool shouldShow = false;
        if (previewState.active && previewState.groupIndex >= 0 && previewState.groupIndex < stableList.size()) {
            if (m_previewWin) {
                if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                    POINT pt; GetCursorPos(&pt);
                    RECT winRect; GetWindowRect(m_previewWin->GetHwnd(), &winRect);
                    bool inWindow = PtInRect(&winRect, pt);
                    bool inIcon = PtInRect(&previewState.lastIconRect, pt);
                    if (!inWindow && !inIcon) {
                        previewState.active = false;
                        shouldShow = false;
                    }
                }

                if (previewState.active) {
                    float dpi = (float)GetDpiForWindow(ctx.hwnd);
                    float scale = dpi / 96.0f;

                    float logicalMain = startMain + (previewState.groupIndex * (itemBoxStride + itemSpacing));

                    POINT pt;
                    if (isVertical) {
                        pt = { (LONG)(fixedCross * scale), (LONG)(logicalMain * scale) };
                    }
                    else {
                        pt = { (LONG)(logicalMain * scale), (LONG)(fixedCross * scale) };
                    }

                    ClientToScreen(ctx.hwnd, &pt);
                    previewState.lastIconRect = {
                        pt.x, pt.y,
                        pt.x + (LONG)(itemBoxSize * scale),
                        pt.y + (LONG)(itemBoxSize * scale)
                    };

                    bool inSafeZone = IsMouseInPreviewOrGap();
                    if (inSafeZone || previewState.spawnFrames < 20) {
                        shouldShow = true;
                        if (inSafeZone) previewState.spawnFrames = 0;
                        else previewState.spawnFrames++;

                        DockPreviewColors colors;
                        colors.bg = D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f);
                        const DockItem &item = stableList[previewState.groupIndex];
                        HICON hPreviewIcon = NULL;
                        HWND hIconWin = item.windows.empty() ? NULL : item.windows[0];
                        if (hIconWin) {
                            hPreviewIcon = (HICON)SendMessage(hIconWin, WM_GETICON, ICON_BIG, 0);
                            if (!hPreviewIcon) hPreviewIcon = (HICON)GetClassLongPtr(hIconWin, GCLP_HICON);
                        }
                        m_previewWin->Update(ctx.hwnd, item.title, item.windows,
                            hPreviewIcon, previewState.lastIconRect, colors, config.position);
                    }
                    else {
                        previewState.active = false;
                        shouldShow = false;
                        previewState.spawnFrames = 0;
                    }
                }
            }
        }

        if (!shouldShow && m_previewWin) m_previewWin->Hide();

        if (activeItem) {
            keepAliveFrames = 60;
        }

        if (keepAliveFrames > 0) {
            keepAliveFrames--;
            InvalidateRect(ctx.hwnd, NULL, FALSE);
        }

        if (++cleanupCounter > 600) cleanupCounter = 0;
    }

    bool HitTestPreview(float x, float y, int &outHoverIndex) {
        if (!m_previewWin || !m_previewWin->IsVisible()) return false;
        if (m_previewWin->IsMouseOver()) {
            outHoverIndex = m_previewWin->GetHoveredIndex();
            return true;
        }
        return false;
    }

    void ClickPreviewItem(int index) {
        if (previewState.groupIndex < 0 || previewState.groupIndex >= stableList.size()) return;
        if (index < 0) return;

        const DockItem &item = stableList[previewState.groupIndex];
        if (index < item.windows.size()) {
            HWND target = item.windows[index];
            if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
            SetForegroundWindow(target);
            SetOptimisticFocus(target);
            previewState.active = false;
        }
    }

    HWND GetWindowAtIndex(int index) {
        if (index >= 0 && index < stableList.size()) {
            const DockItem &item = stableList[index];
            if (item.windows.empty()) return NULL;
            return item.windows[0];
        }
        return NULL;
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
            return stableList[index].title;
        }
        return L"";
    }

    HWND GetNextWindowInGroup(int index, HWND currentHwnd, int step = 1) {
        if (index < 0 || index >= stableList.size()) return NULL;
        const DockItem &item = stableList[index];
        if (item.windows.empty()) return NULL;
        if (item.windows.size() == 1) return item.windows[0];

        int currentIdx = -1;
        for (size_t i = 0; i < item.windows.size(); i++) {
            if (item.windows[i] == currentHwnd) { currentIdx = (int)i; break; }
        }

        if (currentIdx == -1) return item.windows[0];

        int nextIdx = (currentIdx + step) % (int)item.windows.size();
        if (nextIdx < 0) nextIdx += (int)item.windows.size();
        return item.windows[nextIdx];
    }

    void ForceHidePreview() {
        previewState.active = false;
        previewState.groupIndex = -1;
        suppressPreview = true;
        if (m_previewWin) m_previewWin->Hide();
    }

    WindowInfo GetWindowInfoAtIndex(int index) {
        WindowInfo info = {};
        if (index >= 0 && index < stableList.size()) {
            const DockItem &item = stableList[index];
            info.exePath = item.exePath;
            info.title = item.title;
            info.isPinned = item.isPinned;
            info.pathHash = item.pathHash;

            if (!item.windows.empty()) {
                info.hwnd = item.windows[0];
                GetWindowRect(info.hwnd, &info.rect);
            }
            else {
                info.hwnd = NULL;
            }
        }
        return info;
    }
};