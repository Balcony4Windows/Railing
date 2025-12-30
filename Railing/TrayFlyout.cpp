#include "TrayFlyout.h"
#include <windowsx.h>
#include "RailingRenderer.h"
#include <wincodec.h>
#include <cmath>

// Helper to convert HICON to D2D Bitmap
ID2D1Bitmap *CreateBitmapFromIcon(ID2D1RenderTarget *rt, HICON hIcon) {
    if (!hIcon || !rt) return nullptr;
    IWICImagingFactory *pWICFactory = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pWICFactory));
    if (!pWICFactory) return nullptr;

    ID2D1Bitmap *d2dBitmap = nullptr;
    IWICBitmap *wicBitmap = nullptr;
    if (SUCCEEDED(pWICFactory->CreateBitmapFromHICON(hIcon, &wicBitmap))) {
        IWICFormatConverter *converter = nullptr;
        pWICFactory->CreateFormatConverter(&converter);
        converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeMedianCut);
        rt->CreateBitmapFromWicBitmap(converter, &d2dBitmap);
        converter->Release();
        wicBitmap->Release();
    }
    pWICFactory->Release();
    return d2dBitmap;
}

TrayFlyout::TrayFlyout(HINSTANCE hInst) {
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

    RailingRenderer::EnableBlur(hwnd, 0x00000000);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory);
}

TrayFlyout::~TrayFlyout() {
    if (pFactory) pFactory->Release();
    if (pRenderTarget) pRenderTarget->Release();
    if (pBgBrush) pBgBrush->Release();
    if (pHoverBrush) pHoverBrush->Release();
}

void TrayFlyout::Toggle(RECT iconRect) {
    ULONGLONG now = GetTickCount64();

    if (now - lastAutoCloseTime < 200) return;
    if (animState == AnimationState::Visible || animState == AnimationState::Entering) {
        animState = AnimationState::Exiting;
        lastAnimTime = now;
        InvalidateRect(hwnd, NULL, FALSE);
    }
    else {
        currentIcons.clear();
        for (int i = 0; i < 5; i++) {
            TrayIconData dummy;
            dummy.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            currentIcons.push_back(dummy);
        }

        int iconSize = 24;
        int padding = 10;
        int cols = 3;
        int rows = (!currentIcons.empty()) ? (int)std::ceil((float)currentIcons.size() / cols) : 1;
        int width = (cols * iconSize) + ((cols + 1) * padding);
        int height = (rows * iconSize) + ((rows + 1) * padding);

        HMONITOR hMonitor = MonitorFromRect(&iconRect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMonitor, &mi);

        int gap = 8;
        bool isBottom = iconRect.bottom > (mi.rcWork.bottom - 100);
        targetX = iconRect.left + ((iconRect.right - iconRect.left) / 2) - (width / 2);
        if (isBottom) {
            targetY = iconRect.top - height - gap;
        }
        else {
            targetY = iconRect.bottom + gap;
        }

        if (targetX < mi.rcWork.left + gap) targetX = mi.rcWork.left + gap;
        if (targetX + width > mi.rcWork.right - gap) targetX = mi.rcWork.right - gap;
        if (targetY < mi.rcWork.top + gap) targetY = mi.rcWork.top + gap;
        if (targetY + height > mi.rcWork.bottom - gap) targetY = mi.rcWork.bottom - gap;
        animState = AnimationState::Entering;
        currentAlpha = 0.0f;
        currentOffset = 20.0f; // Slide distance
        lastAnimTime = now;
        SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY + (int)currentOffset, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void TrayFlyout::UpdateAnimation() {
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
    case WM_MOUSEMOVE:
        // Hover logic handled in Draw/HitTest
        return 0;
    case WM_LBUTTONUP:
        if (self) self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
        return 0;
    case WM_RBUTTONUP:
        if (self) self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (self && self->animState != AnimationState::Hidden && self->animState != AnimationState::Exiting) {
                self->animState = AnimationState::Exiting;
                self->lastAnimTime = GetTickCount64();
                self->lastAutoCloseTime = GetTickCount64();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void TrayFlyout::Draw() {
    UpdateAnimation();
    if (animState == AnimationState::Hidden) return;

    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

    if (!pRenderTarget) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        pFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &pRenderTarget);
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.6f), &pBgBrush);
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.1f), &pHoverBrush);
    }
    else {
        pRenderTarget->Resize(size);
    }

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    // Update Brush Alpha
    pBgBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f * currentAlpha));
    pHoverBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.1f * currentAlpha));

    // Background
    D2D1_RECT_F bg = D2D1::RectF(0, 0, (float)rc.right, (float)rc.bottom);
    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(bg, 8, 8), pBgBrush);

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
        if (currentIcons[i].hIcon) {
            ID2D1Bitmap *bmp = CreateBitmapFromIcon(pRenderTarget, currentIcons[i].hIcon);
            if (bmp) {
                // To support fading, usually bitmaps need DrawBitmap's opacity param
                // opacity = currentAlpha
                pRenderTarget->DrawBitmap(bmp, iconDest, currentAlpha);
                bmp->Release();
            }
            else {
                pRenderTarget->FillRectangle(iconDest, pHoverBrush);
            }
        }
        col++;
        if (col >= cols) { col = 0; row++; }
    }

    pRenderTarget->EndDraw();
}

void TrayFlyout::OnClick(int x, int y, bool isRightClick) {
    for (const auto &icon : currentIcons) {
        if (x >= icon.rect.left && x <= icon.rect.right &&
            y >= icon.rect.top && y <= icon.rect.bottom) {

            // Trigger Close Animation
            animState = AnimationState::Exiting;
            lastAnimTime = GetTickCount64();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
    }
}