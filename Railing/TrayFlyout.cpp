#include "TrayFlyout.h"
#include <windowsx.h>
#include "RailingRenderer.h"
#include <wincodec.h>
#include <cmath>
#include "dwmapi.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

// Helper to convert HICON to D2D Bitmap
ID2D1Bitmap *CreateBitmapFromIcon(ID2D1RenderTarget *rt, IWICImagingFactory *pWIC, HICON hIcon) {
    if (!hIcon || !rt || !pWIC) return nullptr;

    ID2D1Bitmap *d2dBitmap = nullptr;
    IWICBitmap *wicBitmap = nullptr;

    if (SUCCEEDED(pWIC->CreateBitmapFromHICON(hIcon, &wicBitmap))) {
        IWICFormatConverter *converter = nullptr;
        pWIC->CreateFormatConverter(&converter);
        if (converter) {
            converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);
            rt->CreateBitmapFromWicBitmap(converter, &d2dBitmap);
            converter->Release();
        }
        wicBitmap->Release();
    }
    return d2dBitmap;
}

TrayFlyout::TrayFlyout(HINSTANCE hInst, ID2D1Factory *sharedFactory, IWICImagingFactory *sharedWIC, const ThemeConfig &config)
    : pFactory(sharedFactory), pWICFactory(sharedWIC) {
    this->style = config.global;

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"TrayFlyoutClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"TrayFlyoutClass", L"Tray",
        WS_POPUP, 0, 0, 200, 100,
        nullptr, nullptr, hInst, this
    );

    if (style.blur) RailingRenderer::EnableBlur(hwnd, 0x00000000);
    else {
        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
    DWM_WINDOW_CORNER_PREFERENCE preference = (style.radius > 0.0f) ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}

TrayFlyout::~TrayFlyout() {
    if (pRenderTarget) pRenderTarget->Release();
    if (pBgBrush) pBgBrush->Release();
    if (pHoverBrush) pHoverBrush->Release();
    if (pBorderBrush) pBorderBrush->Release();

    for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
    cachedBitmaps.clear();
}

void TrayFlyout::Toggle(RECT iconRect) {
    ULONGLONG now = GetTickCount64();
    if (now - lastAutoCloseTime < 200) return;

    if (animState == AnimationState::Visible || animState == AnimationState::Entering) {
        if (style.animation.enabled) {
            animState = AnimationState::Exiting;
            lastAnimTime = now;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else {
            animState = AnimationState::Hidden;
            ShowWindow(hwnd, SW_HIDE);
            for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
            cachedBitmaps.clear();
        }
    }
    else {
        currentIcons.clear();
        for (int i = 0; i < 9; i++) {
            TrayIconData dummy;
            dummy.hIcon = LoadIcon(NULL, IDI_APPLICATION); // In real app, LoadIconMetric or similar
            currentIcons.push_back(dummy);
        }

        for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
        cachedBitmaps.clear();

        int iconSize = 24;
        int padding = 10;
        int cols = 3;
        int rows = (!currentIcons.empty()) ? (int)std::ceil((float)currentIcons.size() / cols) : 1;
        int width = (cols * iconSize) + ((cols + 1) * padding);
        int height = (rows * iconSize) + ((rows + 1) * padding);

        HMONITOR hMonitor = MonitorFromRect(&iconRect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);

        int gap = 12;
        targetX = iconRect.left + ((iconRect.right - iconRect.left) / 2) - (width / 2);
        if (targetX < mi.rcWork.left + gap) targetX = mi.rcWork.left + gap;
        if (targetX + width > mi.rcWork.right - gap) targetX = mi.rcWork.right - width - gap;

        targetY = iconRect.top - height - gap; // Default to above
        if (targetY < mi.rcWork.top) targetY = iconRect.bottom + gap; // Flip if not enough space

        animState = AnimationState::Entering;
        currentAlpha = 0.0f;
        currentOffset = 20.0f;
        lastAnimTime = now;

        int r = (int)style.radius;
        HRGN hRgn = (r > 0) ? CreateRoundRectRgn(0, 0, width, height, r * 2, r * 2) : CreateRectRgn(0, 0, width, height);
        SetWindowRgn(hwnd, hRgn, TRUE);

        if (style.animation.enabled) {
            animState = AnimationState::Entering;
            currentAlpha = 0.0f;
            currentOffset = 20.0f;
        }
        else {
            animState = AnimationState::Visible;
            currentAlpha = 1.0f;
            currentOffset = 0.0f;
        }

        SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY + (int)currentOffset, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void TrayFlyout::UpdateAnimation() {
    if (!style.animation.enabled || animState == AnimationState::Visible || animState == AnimationState::Hidden) return;

    ULONGLONG now = GetTickCount64();
    float deltaTime = (float)(now - lastAnimTime) / 1000.0f;
    if (deltaTime == 0.0f) deltaTime = 0.016f;
    lastAnimTime = now;

    float animSpeed = 8.0f;
    bool needsMove = false;

    if (animState == AnimationState::Entering) {
        currentAlpha += deltaTime * animSpeed;
        if (currentAlpha >= 1.0f) {
            currentAlpha = 1.0f;
            animState = AnimationState::Visible;
        }
        currentOffset = (1.0f - currentAlpha) * 20.0f;
        if (animState != AnimationState::Visible) InvalidateRect(hwnd, NULL, FALSE);
        needsMove = true;
    }
    else if (animState == AnimationState::Exiting) {
        currentAlpha -= deltaTime * animSpeed;
        if (currentAlpha <= 0.0f) {
            currentAlpha = 0.0f;
            animState = AnimationState::Hidden;
            ShowWindow(hwnd, SW_HIDE);

            for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
            cachedBitmaps.clear();
        }
        currentOffset = (1.0f - currentAlpha) * 20.0f;
        if (animState != AnimationState::Hidden) InvalidateRect(hwnd, NULL, FALSE);
        needsMove = true;
    }

    if (needsMove && animState != AnimationState::Hidden) {
        SetWindowPos(hwnd, NULL,
            targetX,
            targetY + (int)currentOffset,
            0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        );
    }
}

LRESULT CALLBACK TrayFlyout::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    TrayFlyout *self = (TrayFlyout *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        self = (TrayFlyout *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (self) self->Draw();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
        if (self) self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
        return 0;
    case WM_RBUTTONUP:
        if (self) self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && self && self->animState != AnimationState::Hidden) {
            if (self->animState != AnimationState::Exiting) {
                self->lastAutoCloseTime = GetTickCount64();

                if (self->style.animation.enabled) {
                    self->animState = AnimationState::Exiting;
                    self->lastAnimTime = GetTickCount64();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                else {
                    self->animState = AnimationState::Hidden;
                    ShowWindow(hwnd, SW_HIDE);
                    for (auto *bmp : self->cachedBitmaps) if (bmp) bmp->Release();
                    self->cachedBitmaps.clear();
                }
            }
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void TrayFlyout::UpdateBitmapCache() {
    for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
    cachedBitmaps.clear();

    for (const auto &icon : currentIcons) {
        if (icon.hIcon) {
            cachedBitmaps.push_back(CreateBitmapFromIcon(pRenderTarget, pWICFactory, icon.hIcon));
        }
        else cachedBitmaps.push_back(nullptr);
    }
}

void TrayFlyout::Draw() {
    UpdateAnimation();
    if (animState == AnimationState::Hidden) return;

    RECT rc; GetClientRect(hwnd, &rc);
    if (rc.right == 0 || rc.bottom == 0) return; // Window has no size.
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

    if (!pRenderTarget) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        HRESULT hr = pFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &pRenderTarget);
        if (SUCCEEDED(hr) && pRenderTarget) {
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.6f), &pBgBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.1f), &pHoverBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBorderBrush);
            UpdateBitmapCache();
        }
        else return;
    }
    else {
        D2D1_SIZE_U curSize = pRenderTarget->GetPixelSize();
        if (curSize.width != size.width || curSize.height != size.height)
            pRenderTarget->Resize(size);
    }

    if (cachedBitmaps.size() != currentIcons.size()) UpdateBitmapCache();

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (pBgBrush && pHoverBrush && pBorderBrush) {
        // Update Brush Alpha
        D2D1_COLOR_F bgColor = style.background;
        bgColor.a *= currentAlpha;
        pBgBrush->SetColor(bgColor);
        pHoverBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.1f * currentAlpha));

        // Background
        D2D1_RECT_F bg = D2D1::RectF(0, 0, (float)rc.right, (float)rc.bottom);
        float r = style.radius;
        if (bgColor.a > 0.0f) pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(bg, r, r), pBgBrush);

        if (style.borderWidth > 0.0f && style.borderColor.a > 0.0f) {
            D2D1_COLOR_F bColor = style.borderColor;
            bColor.a *= currentAlpha; // Fade with window
            pBorderBrush->SetColor(bColor);

            float inset = style.borderWidth / 2.0f;
            D2D1_RECT_F borderRect = D2D1::RectF(inset, inset, rc.right - inset, rc.bottom - inset);
            pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(borderRect, r, r), pBorderBrush, style.borderWidth);
        }

        float padding = 10.0f;
        float iconSize = 24.0f;
        int cols = 3;
        int col = 0;
        int row = 0;

        for (size_t i = 0; i < currentIcons.size(); i++) {
            float x = padding + (col * (iconSize + padding));
            float y = padding + (row * (iconSize + padding));

            D2D1_RECT_F iconDest = D2D1::RectF(x, y, x + iconSize, y + iconSize);
            currentIcons[i].rect = { (LONG)x, (LONG)y, (LONG)(x + iconSize), (LONG)(y + iconSize) };
            // Hover
            POINT pt; GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (pt.x >= x && pt.x <= x + iconSize && pt.y >= y && pt.y <= y + iconSize) {
                pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 2, y - 2, x + iconSize + 2, y + iconSize + 2), 4, 4), pHoverBrush);
            }
            // Icon
            if (i < cachedBitmaps.size() && cachedBitmaps[i]) {
                pRenderTarget->DrawBitmap(cachedBitmaps[i], iconDest, currentAlpha);
            }

            col++;
            if (col >= cols) { col = 0; row++; }
        }
    }
    pRenderTarget->EndDraw();
}

void TrayFlyout::OnClick(int x, int y, bool isRightClick) {
    for (const auto &icon : currentIcons) {
        if (x >= icon.rect.left && x <= icon.rect.right &&
            y >= icon.rect.top && y <= icon.rect.bottom) {
            if (style.animation.enabled) {
                animState = AnimationState::Exiting;
                lastAnimTime = GetTickCount64();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else {
                animState = AnimationState::Hidden;
                ShowWindow(hwnd, SW_HIDE);
                for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
                cachedBitmaps.clear();
            }
            break;
        }
    }
}