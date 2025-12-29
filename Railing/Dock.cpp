#define NOMINMAX
#include "Dock.h"
#include <algorithm>
#include <cmath>
#include <psapi.h>

Dock::~Dock() {
    for (auto &pair : iconCache) {
        if (pair.second) pair.second->Release();
    }
    if (pWICFactory) pWICFactory->Release();
}

void Dock::Update(const std::vector<WindowInfo> &windows) {
    // Mark everyone as dying
    for (auto &item : renderList) item.isDying = true;

    // Sync with new list
    for (const auto &w : windows) {
        auto it = std::find_if(renderList.begin(), renderList.end(),
            [&](const RenderItem &r) { return r.win.hwnd == w.hwnd; });

        if (it != renderList.end()) {
            it->isDying = false;
            it->win = w;
        }
        else {
            RenderItem newItem;
            newItem.win = w;
            newItem.currentAlpha = 0.0f;
            newItem.isDying = false;
            newItem.currentX = -1.0f;
            renderList.push_back(newItem);
        }
    }

    for (auto it = iconCache.begin(); it != iconCache.end(); ) {
        HWND cachedHwnd = it->first;
        bool exists = false;
        for (const auto &w : windows) {
            if (w.hwnd == cachedHwnd) { exists = true; break; }
        }

        if (!exists) {
            if (it->second) it->second->Release(); // Free GPU memory
            it = iconCache.erase(it); // Free the map entry
        }
        else {
            ++it;
        }
    }
}

bool Dock::Draw(const RenderContext &ctx, HWND activeWindow, std::vector<ClickTarget> &outTargets) {
    float appSize = ctx.logicalHeight - 5.0f;
    float appGap = 6.0f;
    float iconDisplaySize = appSize * 0.85;

    int visibleCount = 0;
    for (const auto &item : renderList) {
        if (!item.isDying) visibleCount++;
    }

    // Total width of dock content
    float contentWidth = (visibleCount * appSize) + ((std::max(0, visibleCount - 1)) * appGap);
    float padding = 6.0f;
    float containerWidth = contentWidth + (padding * 2.0f);

    // Center the container
    float containerLeft = (ctx.logicalWidth - containerWidth) / 2.0f;
    float startX = containerLeft + padding;

    bool isAnimating = false;
    float delta = 0.2f; // Spring speed
    // Assign targets
    int validIndex = 0;
    for (auto &item : renderList) {
        if (!item.isDying) {
            item.targetX = startX + (validIndex * (appSize + appGap));
            if (item.currentX < 0.0f) item.currentX = item.targetX;
            validIndex++;
        }
    }

    // Animate properties
    for (auto &item : renderList) {
        float targetAlpha = item.isDying ? 0.0f : 1.0f;

        // Alpha Animation
        if (std::abs(item.currentAlpha - targetAlpha) > 0.01f) {
            item.currentAlpha += (targetAlpha - item.currentAlpha) * delta;
            isAnimating = true;
        }
        else {
            item.currentAlpha = targetAlpha;
        }

        // Position Animation (Slide)
        if (std::abs(item.currentX - item.targetX) > 0.5f) {
            item.currentX += (item.targetX - item.currentX) * delta;
            isAnimating = true;
        }
        else {
            item.currentX = item.targetX;
        }
    }

    // Cleanup dead items
    renderList.erase(
        std::remove_if(renderList.begin(), renderList.end(),
            [](const RenderItem &i) { return i.isDying && i.currentAlpha <= 0.01f; }),
        renderList.end()
    );
    float pillY = 0.0f;
    float pillHeight = 0.0f;
    // Only draw if we have items
    if (!renderList.empty()) {
        float minX = 100000.0f;
        float maxX = -100000.0f;
        bool hasVisible = false;

        for (const auto &item : renderList) {
            if (item.currentAlpha > 0.1f) {
                if (item.currentX < minX) minX = item.currentX;
                if (item.currentX > maxX) maxX = item.currentX;
                hasVisible = true;
            }
        }

        if (hasVisible) {
            // Container Rect
            float boxLeft = minX - padding;
            float boxRight = maxX + appSize + padding;
            float boxTop = (ctx.logicalHeight - (appSize + padding * 2)) / 2.0f; // vertically center
            float boxHeight = appSize + padding * 2.0f;

            pillHeight = ctx.logicalHeight * 0.85f;
            pillY = (ctx.logicalHeight - pillHeight) / 2.0f;

            D2D1_RECT_F bgRect = D2D1::RectF(boxLeft, pillY, boxRight, pillY + pillHeight);

            // Draw Background Pill (Black 20% transparent)
            ctx.bgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black, 0.2f));
            ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(bgRect, 4.0f, 4.0f), ctx.bgBrush);
        }
    }

    for (const auto &item : renderList) {
        float yPos = ((ctx.logicalHeight - appSize) / 2.0f);
        D2D1_RECT_F rectF = D2D1::RectF(item.currentX, yPos, item.currentX + appSize, yPos + appSize);
        if (!item.isDying) {
            RECT clickRect = { (LONG)(rectF.left * ctx.scale), (LONG)(rectF.top * ctx.scale),
                               (LONG)(rectF.right * ctx.scale), (LONG)(rectF.bottom * ctx.scale) };
            outTargets.push_back({ clickRect, item.win.hwnd });
        }

        bool isActive = (item.win.hwnd == activeWindow);

        if (isActive && pillHeight > 0) {
            float indicatorThickness = 2.0f;
            float indicatorWidth = appSize * 0.5f; // Make it slightly narrower than the app slot
            float centerX = item.currentX + (appSize / 2.0f);

            // Anchor exactly to the bottom of the background pill
            float bottomY = pillY + pillHeight;

            D2D1_RECT_F activeBar = D2D1::RectF(
                centerX - (indicatorWidth / 2.0f),
                bottomY - indicatorThickness,
                centerX + (indicatorWidth / 2.0f),
                bottomY
            );

            ctx.bgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White, 0.9f * item.currentAlpha));
            // Using a small radius (2.0f) so the top of the bar is rounded but it fits the pill
            ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(activeBar, 2.0f, 2.0f), ctx.bgBrush);
        }

        // Icon
        ID2D1Bitmap *icon = GetOrLoadIcon(item.win.hwnd, ctx.rt);
        if (icon) {
            float iconLeft = rectF.left + (appSize - iconDisplaySize) / 2.0f;
            float iconTop = rectF.top + (appSize - iconDisplaySize) / 2.0f;
            D2D1_RECT_F iconDest = D2D1::RectF(iconLeft, iconTop, iconLeft + iconDisplaySize, iconTop + iconDisplaySize);
            ctx.rt->DrawBitmap(icon, iconDest, item.currentAlpha);
        }
        else {
            // Fallback Text Icon
            float iconLeft = rectF.left + (appSize - iconDisplaySize) / 2.0f;
            float iconTop = rectF.top + (appSize - iconDisplaySize) / 2.0f;
            D2D1_RECT_F fallbackRect = D2D1::RectF(iconLeft, iconTop, iconLeft + iconDisplaySize, iconTop + iconDisplaySize);

            ctx.bgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Gray, 0.5f * item.currentAlpha));
            ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(fallbackRect, 4.0f, 4.0f), ctx.bgBrush);

            if (!item.win.title.empty()) {
                ctx.textBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White, item.currentAlpha));
                ctx.rt->DrawTextW(item.win.title.c_str(), 1, ctx.textFormat, fallbackRect, ctx.textBrush);
            }
        }
    }
    return isAnimating;
}

ID2D1Bitmap *Dock::GetOrLoadIcon(HWND hwnd, ID2D1RenderTarget *rt) {
    if (iconCache.find(hwnd) != iconCache.end()) return iconCache[hwnd];

    if (!pWICFactory || !rt) return nullptr;

    HICON hIcon = nullptr;
    bool shouldDestroy = false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);

    if (hProcess) {
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameEx(hProcess, NULL, exePath, MAX_PATH)) {
            HICON hTempIcon = nullptr;
            UINT id = 0;
            if (PrivateExtractIconsW(exePath, 0, 64, 64, &hTempIcon, &id, 1, 0) > 0) {
                hIcon = hTempIcon;
                shouldDestroy = true;
            }
        }
        CloseHandle(hProcess);
    }

    if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0);
    if (!hIcon) hIcon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    if (!hIcon) hIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM);
    if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);

    IWICBitmap *wicBitmap = nullptr;
    HRESULT hr = pWICFactory->CreateBitmapFromHICON(hIcon, &wicBitmap);
    ID2D1Bitmap *d2dBitmap = nullptr;
    if (SUCCEEDED(hr)) {
        IWICFormatConverter *converter = nullptr;
        pWICFactory->CreateFormatConverter(&converter);
        converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);
        rt->CreateBitmapFromWicBitmap(converter, &d2dBitmap);
        converter->Release();
        wicBitmap->Release();
    }
    if (shouldDestroy && hIcon) DestroyIcon(hIcon);

    iconCache[hwnd] = d2dBitmap;
    return d2dBitmap;
}