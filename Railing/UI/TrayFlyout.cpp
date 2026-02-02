#include "TrayFlyout.h"
#include <windowsx.h>
#include "RailingRenderer.h"
#include <wincodec.h>
#include <cmath>
#include "dwmapi.h"

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#define WM_TRAY_REFRESH (WM_USER + 102)

ID2D1Bitmap *CreateBitmapFromIcon(ID2D1RenderTarget *rt, IWICImagingFactory *pWIC, HICON hIcon) {
    if (!hIcon || !rt || !pWIC) return nullptr;

    ID2D1Bitmap *d2dBitmap = nullptr;
    IWICBitmap *wicBitmap = nullptr;

    // Use WIC to wrap the HICON
    HRESULT hr = pWIC->CreateBitmapFromHICON(hIcon, &wicBitmap);
    if (FAILED(hr)) return nullptr;

    IWICFormatConverter *pConverter = nullptr;
    hr = pWIC->CreateFormatConverter(&pConverter);
    if (SUCCEEDED(hr)) {
        // Specifically use GUID_WICPixelFormat32bppPBGRA for Direct2D compatibility
        hr = pConverter->Initialize(
            wicBitmap,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.f,
            WICBitmapPaletteTypeCustom
        );

        if (SUCCEEDED(hr)) {
            rt->CreateBitmapFromWicBitmap(pConverter, NULL, &d2dBitmap);
        }
        pConverter->Release();
    }
    wicBitmap->Release();
    return d2dBitmap;
}

TrayFlyout::TrayFlyout(HINSTANCE hInst, ID2D1Factory *sharedFactory, IWICImagingFactory *sharedWIC, TooltipHandler *tooltips, const ThemeConfig &config)
    : pFactory(sharedFactory), pWICFactory(sharedWIC), tooltips(tooltips) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    this->style = config.global;

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"TrayFlyoutClass";
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"TrayFlyoutClass", L"Tray",
        WS_POPUP, 0, 0, 400, 200,
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
    if (TrayBackend::MultipleTrays) SetTimer(hwnd, 1, 1000, NULL);

    TrayBackend::Get().SetIconChangeCallback([this]() {
        if (IsWindow(hwnd)) PostMessage(hwnd, WM_TRAY_REFRESH, 0, 0);
        });
}

TrayFlyout::~TrayFlyout() {
    if (pRenderTarget) pRenderTarget->Release();
    if (pBgBrush) pBgBrush->Release();
    if (pHoverBrush) pHoverBrush->Release();
    if (pBorderBrush) pBorderBrush->Release();

    for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
    cachedBitmaps.clear();
    CoUninitialize();
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
        // Fetch Data
        currentIcons = TrayBackend::Get().GetIcons();
        UpdateLayout();

		for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
		cachedBitmaps.clear();

        // Dynamic Columns
        if (currentIcons.size() <= 5) layoutCols = max(1, (int)currentIcons.size());
        else if (currentIcons.size() <= 9) layoutCols = 3;
        else layoutCols = 5;

        int rows = 1;
        if (!currentIcons.empty()) {
            rows = (int)std::ceil((float)currentIcons.size() / (float)layoutCols);
        }

        int iconPx = (int)layoutIconSize;
        int padPx = (int)layoutPadding;

        int width = (layoutCols * iconPx) + ((layoutCols + 1) * padPx);

        // FIX: Add extra buffer to height to prevent rounded corners from clipping icons
        int height = (rows * iconPx) + ((rows + 1) * padPx) + 12;

        // Positioning
        HMONITOR hMonitor = MonitorFromRect(&iconRect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);

        int gap = 12;
        targetX = iconRect.left + ((iconRect.right - iconRect.left) / 2) - (width / 2);

        if (targetX < mi.rcWork.left + gap) targetX = mi.rcWork.left + gap;
        if (targetX + width > mi.rcWork.right - gap) targetX = mi.rcWork.right - width - gap;

        targetY = iconRect.top - height - gap;
        if (targetY < mi.rcWork.top) targetY = iconRect.bottom + gap;

        animState = AnimationState::Entering;
        currentAlpha = 0.0f;
        currentOffset = 20.0f;
        lastAnimTime = now;

        int r = (int)style.radius;
        // Make the region slightly larger than content to be safe
        HRGN hRgn = (r > 0) ? CreateRoundRectRgn(0, 0, width + 1, height + 1, r * 2, r * 2) : CreateRectRgn(0, 0, width, height);
        SetWindowRgn(hwnd, hRgn, TRUE);

        if (!style.animation.enabled) {
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

void TrayFlyout::UpdateLayout() {
    int col = 0;
    int row = 0;

    // Ensure these are set correctly based on current icon count
    if (currentIcons.size() <= 5) layoutCols = max(1, (int)currentIcons.size());
    else if (currentIcons.size() <= 9) layoutCols = 3;
    else layoutCols = 5;

    for (auto &icon : currentIcons) {
        float x = layoutPadding + (col * (layoutIconSize + layoutPadding));
        float y = layoutPadding + (row * (layoutIconSize + layoutPadding));

        // Store the hit-test coordinates immediately
        icon.rect.left = (LONG)x;
        icon.rect.top = (LONG)y;
        icon.rect.right = (LONG)(x + layoutIconSize);
        icon.rect.bottom = (LONG)(y + layoutIconSize);

        col++;
        if (col >= layoutCols) {
            col = 0;
            row++;
        }
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
    case WM_MOUSEMOVE:
        if (self) self->OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        if (self) self->OnMouseLeave();
        return 0;
    case WM_LBUTTONUP:
        if (self) self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
        return 0;
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        for (const auto &icon : self->currentIcons) {
            if (x >= icon.rect.left && x < icon.rect.right &&
                y >= icon.rect.top && y < icon.rect.bottom)
            {
                TrayBackend::Get().SendDoubleClick(icon);
                return 0;
            }
        }
        return 0;
    }
    case WM_RBUTTONUP:
        if (self) self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && self && self->animState != AnimationState::Hidden) {
            if (self->ignoreNextDeactivate) { // Inside an icon
                self->ignoreNextDeactivate = false;
                return 0;
            }
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
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
        if (hit == HTNOWHERE || hit == HTCAPTION) return HTCLIENT;
        return hit;
    }
    case WM_TRAY_REFRESH:
        if (self && self->animState == AnimationState::Visible) {
            self->currentIcons = TrayBackend::Get().GetIcons();

			for (auto *bmp : self->cachedBitmaps) if (bmp) bmp->Release();
			self->cachedBitmaps.clear();

            self->UpdateLayout();
            self->UpdateBitmapCache();
            InvalidateRect(hwnd, NULL , FALSE);
        }
        return 0;
    case WM_TIMER:
		if (wParam == 1) { // Tray host timer when multiple trays exist
            TrayBackend::Get().NotifyIconsChanged();
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void TrayFlyout::OnMouseMove(int x, int y) {
    TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);

    int newHoveredIndex = -1;
    int col = 0;
    int row = 0;

    for (size_t i = 0; i < currentIcons.size(); i++) {
        // Calculate rect the same way as in Draw()
        float iconX = layoutPadding + (col * (layoutIconSize + layoutPadding));
        float iconY = layoutPadding + (row * (layoutIconSize + layoutPadding));

        if (x >= iconX && x <= iconX + layoutIconSize &&
            y >= iconY && y <= iconY + layoutIconSize) {
            newHoveredIndex = (int)i;
            break;
        }

        col++;
        if (col >= layoutCols) { col = 0; row++; }
    }

    if (newHoveredIndex != hoveredIconIndex) {
        hoveredIconIndex = newHoveredIndex;

        if (hoveredIconIndex >= 0 && tooltips) {
            const TrayIconData &icon = currentIcons[hoveredIconIndex];

            // Recalculate rect for tooltip
            int col = hoveredIconIndex % layoutCols;
            int row = hoveredIconIndex / layoutCols;
            float iconX = layoutPadding + (col * (layoutIconSize + layoutPadding));
            float iconY = layoutPadding + (row * (layoutIconSize + layoutPadding));

            POINT topLeft = { (LONG)iconX, (LONG)iconY };
            POINT bottomRight = { (LONG)(iconX + layoutIconSize), (LONG)(iconY + layoutIconSize) };
            ClientToScreen(hwnd, &topLeft);
            ClientToScreen(hwnd, &bottomRight);
            RECT screenRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y + 20 };

            std::string position = "bottom";
            tooltips->Show(icon.tooltip, screenRect, position, 1.0f);
        }
        else if (tooltips) {
            tooltips->Hide();
        }

        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void TrayFlyout::OnMouseLeave() {
    hoveredIconIndex = -1;
    if (tooltips) tooltips->Hide();
    InvalidateRect(hwnd, NULL, FALSE);
}

void TrayFlyout::UpdateBitmapCache() {
    if (cachedBitmaps.size() != currentIcons.size()) {
        for (auto *bmp : cachedBitmaps) if (bmp) bmp->Release();
        cachedBitmaps.assign(currentIcons.size(), nullptr);
    }

    for (size_t i = 0; i < currentIcons.size(); i++) {
        if (cachedBitmaps[i] == nullptr && currentIcons[i].hIcon) {
            cachedBitmaps[i] = CreateBitmapFromIcon(pRenderTarget, pWICFactory, currentIcons[i].hIcon);
        }
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

        int col = 0;
        int row = 0;

        for (size_t i = 0; i < currentIcons.size(); i++) {
            float x = layoutPadding + (col * (layoutIconSize + layoutPadding));
            float y = layoutPadding + (row * (layoutIconSize + layoutPadding));

            D2D1_RECT_F iconDest = D2D1::RectF(x, y, x + layoutIconSize, y + layoutIconSize);
            currentIcons[i].rect = { (LONG)x, (LONG)y, (LONG)(x + layoutIconSize), (LONG)(y + layoutIconSize) };

            // Hover - use hoveredIconIndex instead of recalculating
            if ((int)i == hoveredIconIndex) {
                pRenderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(x - 2, y - 2, x + layoutIconSize + 2, y + layoutIconSize + 2), 6, 6),
                    pHoverBrush
                );
            }

            // Icon
            if (i < cachedBitmaps.size() && cachedBitmaps[i]) {
                pRenderTarget->DrawBitmap(cachedBitmaps[i], iconDest, currentAlpha);
            }
            else {
                pBgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.3f * currentAlpha));
                pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(iconDest, 2, 2), pBgBrush);
            }

            col++;
            if (col >= layoutCols) { col = 0; row++; }
        }
    }
    pRenderTarget->EndDraw();
}

void TrayFlyout::OnClick(int x, int y, bool isRightClick) {
    for (const auto &icon : currentIcons) {
        if (x >= icon.rect.left && x <= icon.rect.right &&
            y >= icon.rect.top && y <= icon.rect.bottom) {

            if (isRightClick) {
                ignoreNextDeactivate = true;
                TrayBackend::Get().SendRightClick(icon);
            }
            else {
                ignoreNextDeactivate = false;
                TrayBackend::Get().SendLeftClick(icon);
            }
            break;
        }
    }
}

void TrayFlyout::OnDoubleClick(int x, int y) {
    for (const auto &icon : currentIcons) {
        if (x >= icon.rect.left && x <= icon.rect.right &&
            y >= icon.rect.top && y <= icon.rect.bottom) {

            TrayBackend::Get().SendDoubleClick(icon);

            // Optional: Close flyout on double click?
            ShowWindow(hwnd, SW_HIDE); 
            break;
        }
    }
}