#include "NetworkFlyout.h"
#include <windowsx.h>
#include "RailingRenderer.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

// Constants
const float NET_LOGICAL_WIDTH = 320.0f;
const float NET_LOGICAL_HEIGHT = 450.0f;
const float ITEM_HEIGHT = 40.0f;
const float EXPANDED_HEIGHT = 110.0f;

NetworkFlyout::NetworkFlyout(BarInstance *owner, HINSTANCE hInst, ID2D1Factory *pSharedFactory, IDWriteFactory *pSharedWriteFactory, IDWriteTextFormat *pFormat, IDWriteTextFormat *pIconFormat, const ThemeConfig &config)
    : ownerBar(owner), hInst(hInst), pFactory(pSharedFactory), pWriteFactory(pSharedWriteFactory), pTextFormat(pFormat), pIconFormat(pIconFormat)
{
    FlyoutManager::Get().Register(this);
    this->style = config;

    WNDCLASS wc = { 0 };
    if (!GetClassInfo(hInst, L"NetworkFlyoutClass", &wc)) {
        wc.lpfnWndProc = NetworkFlyout::WindowProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"NetworkFlyoutClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = nullptr;
        RegisterClass(&wc);
    }
}

NetworkFlyout::~NetworkFlyout() {
    closing.store(true, std::memory_order_release);
    FlyoutManager::Get().Unregister(this);
    if (workerThread.joinable()) workerThread.join();
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }

    if (pRenderTarget) pRenderTarget->Release();
    if (pBgBrush) pBgBrush->Release();
    if (pFgBrush) pFgBrush->Release();
    if (pAccentBrush) pAccentBrush->Release();
    if (pBorderBrush) pBorderBrush->Release();
    if (pDimBrush) pDimBrush->Release();
}

// ... [CreateDeviceResources, PositionWindow - KEEP AS IS] ...
void NetworkFlyout::CreateDeviceResources() {
    if (!hwnd) return;
    if (!pRenderTarget) {
        RECT rc; GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU((std::max)(1L, rc.right - rc.left), (std::max)(1L, rc.bottom - rc.top));

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        pFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &pRenderTarget);
    }
    if (pRenderTarget) {
        if (!pBgBrush) pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBgBrush);
        if (!pFgBrush) pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &pFgBrush);
        if (!pAccentBrush) pRenderTarget->CreateSolidColorBrush(style.global.highlights, &pAccentBrush);
        if (!pBorderBrush) pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBorderBrush);
        if (!pDimBrush) pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.5f), &pDimBrush);
    }
}

void NetworkFlyout::PositionWindow(RECT iconRect) {
    if (!hwnd) return;
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;

    int w = (int)(NET_LOGICAL_WIDTH * scale);
    int h = (int)(NET_LOGICAL_HEIGHT * scale);

    HMONITOR hMonitor = MonitorFromRect(&iconRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    GetMonitorInfo(hMonitor, &mi);

    int gap = (int)(12 * scale);
    this->targetX = iconRect.left + ((iconRect.right - iconRect.left) / 2) - (w / 2);
    this->targetY = iconRect.top - h - gap;

    if (this->targetY < mi.rcWork.top) this->targetY = iconRect.bottom + gap;
    if (this->targetX < mi.rcWork.left + gap) this->targetX = mi.rcWork.left + gap;
    if (this->targetX + w > mi.rcWork.right - gap) this->targetX = mi.rcWork.right - w - gap;
}

void NetworkFlyout::Toggle(RECT iconRect) {
    if (!hwnd) {
        hwnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            L"NetworkFlyoutClass", L"NetworkFlyout",
            WS_POPUP, 0, 0, 100, 100,
            nullptr, nullptr, hInst, this
        );
    }

    if (IsVisible()) {
        Hide();
    }
    else {
        FlyoutManager::Get().CloseOthers(this);

        selectedSSID = L"";
        passwordInput = L"";
        connectionStatusMsg = L"";
        scrollOffset = 0;
        PositionWindow(iconRect);

        float dpi = (float)GetDpiForWindow(hwnd);
        float scale = dpi / 96.0f;
        currentAlpha = 0.0f;
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);

        SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY,
            (int)(NET_LOGICAL_WIDTH * scale), (int)(NET_LOGICAL_HEIGHT * scale), SWP_NOACTIVATE);

        currentAlpha = 0.0f;
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);

        ShowWindow(hwnd, SW_SHOW);

        mmTimerId = timeSetEvent(10, 1, [](UINT, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR) {
            NetworkFlyout *self = (NetworkFlyout *)dwUser;
            self->currentAlpha += 0.08f;
            if (self->currentAlpha >= 1.0f) {
                self->currentAlpha = 1.0f;
                if (self->mmTimerId) {
                    timeKillEvent(self->mmTimerId);
                    self->mmTimerId = 0;
                }
            }
            SetLayeredWindowAttributes(self->hwnd, 0, (BYTE)(self->currentAlpha * 255.0f), LWA_ALPHA);
            InvalidateRect(self->hwnd, NULL, FALSE);
            }, (DWORD_PTR)this, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);

        SetForegroundWindow(hwnd);
        SetFocus(hwnd);

        ScanAsync();

        static bool dwmSetupDone = false;
        if (!dwmSetupDone) {
            dwmSetupDone = true;

            if (style.global.blur) RailingRenderer::EnableBlur(hwnd, 0x00000000);
            else {
                MARGINS margins = { -1 };
                DwmExtendFrameIntoClientArea(hwnd, &margins);
            }

            DWM_WINDOW_CORNER_PREFERENCE pref = (style.global.radius > 0.0f) ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
        }
    }
}

void NetworkFlyout::Hide() {
    if (mmTimerId) {
        timeKillEvent(mmTimerId);
        mmTimerId = 0;
    }
}

LRESULT CALLBACK NetworkFlyout::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    NetworkFlyout *self = (NetworkFlyout *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        self = (NetworkFlyout *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    if (!self) return DefWindowProc(hwnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        self->Draw();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_ACTIVATE;
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) self->OnDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        self->isDraggingScrollbar = false;
        return 0;
    case WM_MOUSEWHEEL:
        self->OnScroll(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_CHAR:
        self->OnChar((wchar_t)wParam);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && !self->isDraggingScrollbar) {
            ShowWindow(hwnd, SW_HIDE);
            self->currentAlpha = 0.0f;
        }
        return 0;

    case WM_KILLFOCUS:
        if (!self->isDraggingScrollbar) {
            ShowWindow(hwnd, SW_HIDE);
            self->currentAlpha = 0.0f;
        }
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void NetworkFlyout::ScanAsync() {
    if (isBusy.load(std::memory_order_acquire)) return;
    uint64_t myToken = ++scanToken;
    if (workerThread.joinable()) workerThread.join();

    workerThread = std::thread([this, myToken]() {
        isBusy = true;
        backend.RequestScan();
        Sleep(100);
        auto nets = backend.ScanNetworks();
        
        if (closing.load(std::memory_order_acquire) || myToken != scanToken.load(std::memory_order_acquire)) {
            isBusy.store(false, std::memory_order_release);
            return;
        }
        cachedNetworks = std::move(nets);
        isBusy.store(false, std::memory_order_release);

        if (hwnd && IsWindow(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
    });
}

void NetworkFlyout::ConnectAsync(WifiNetwork net, std::wstring password) {
    if (isBusy) return;
    connectionStatusMsg = L"Connecting...";
    InvalidateRect(hwnd, NULL, FALSE);

    if (workerThread.joinable()) workerThread.join();

    workerThread = std::thread([this, net, password]() {
        isBusy = true;
        std::wstring result = backend.ConnectTo(net, password);
        this->connectionStatusMsg = result;
        Sleep(500);
        this->cachedNetworks = backend.ScanNetworks();
        isBusy = false;
        if (closing.load(std::memory_order_acquire)) {
            isBusy.store(false, std::memory_order_release);
            return;
        }

        if (hwnd && IsWindow(hwnd)) InvalidateRect(this->hwnd, NULL, FALSE);
    });
}

void NetworkFlyout::Draw() {
    CreateDeviceResources();
    if (!pRenderTarget) return;
    // Note: We always draw if we are visible, no enum checks needed
    if (!IsWindowVisible(hwnd)) return;

    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
    if (pRenderTarget->GetPixelSize().width != size.width) pRenderTarget->Resize(size);

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    D2D1_COLOR_F bgColor = style.global.background;
    if (bgColor.a == 0.0f) bgColor = D2D1::ColorF(0.08f, 0.08f, 0.1f, 0.85f);
    pBgBrush->SetColor(bgColor);

    D2D1_RECT_F bgRect = D2D1::RectF(0, 0, NET_LOGICAL_WIDTH, NET_LOGICAL_HEIGHT);
    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(bgRect, style.global.radius, style.global.radius), pBgBrush);

    if (style.global.borderWidth > 0.0f) {
        D2D1_COLOR_F bColor = style.global.borderColor;
        bColor.a *= currentAlpha;
        pBorderBrush->SetColor(bColor);
        pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(bgRect, style.global.radius, style.global.radius), pBorderBrush, style.global.borderWidth);
    }

    float pad = 16.0f;
    float y = 16.0f;

    pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
    pRenderTarget->DrawTextW(L"Wi-Fi", 5, pTextFormat, D2D1::RectF(pad, y, NET_LOGICAL_WIDTH, y + 24), pFgBrush);

    refreshBtnRect = D2D1::RectF(NET_LOGICAL_WIDTH - 40, y - 5, NET_LOGICAL_WIDTH - 10, y + 25);
    pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    if (pIconFormat) {
        pRenderTarget->DrawTextW(L"\uE72C", 1, pIconFormat, refreshBtnRect, pFgBrush);
    }
    pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    y += 30.0f;

    pDimBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f));
    pRenderTarget->FillRectangle(D2D1::RectF(pad, y, NET_LOGICAL_WIDTH - pad, y + 1), pDimBrush);
    y += 10.0f;

    float listHeight = NET_LOGICAL_HEIGHT - y - pad;
    D2D1_RECT_F listRect = D2D1::RectF(0, y, NET_LOGICAL_WIDTH, NET_LOGICAL_HEIGHT - pad);
    pRenderTarget->PushAxisAlignedClip(listRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float curY = y - scrollOffset;
    networkItemRects.clear();
    connectBtnRect = { 0,0,0,0 };
    passwordBoxRect = { 0,0,0,0 };

    float totalH = 0;

    for (const auto &net : cachedNetworks) {
        bool isExpanded = (net.ssid == selectedSSID);
        float h = isExpanded ? (ITEM_HEIGHT + EXPANDED_HEIGHT) : ITEM_HEIGHT;

        D2D1_RECT_F itemRect = D2D1::RectF(0, curY, NET_LOGICAL_WIDTH, curY + h);

        if (curY + h > y && curY < listRect.bottom) {

            if (isExpanded) {
                pDimBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
                pRenderTarget->FillRectangle(itemRect, pDimBrush);

                pAccentBrush->SetColor(style.global.highlights);
                pRenderTarget->FillRectangle(D2D1::RectF(0, curY + 4, 4, curY + h - 4), pAccentBrush);
            }

            pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
            pRenderTarget->DrawTextW(net.ssid.c_str(), (UINT32)net.ssid.length(), pTextFormat,
                D2D1::RectF(pad + 10, curY + 8, NET_LOGICAL_WIDTH - 60, curY + 30), pFgBrush);

            std::wstring sub;
            if (net.isConnected) sub = L"Connected";
            else if (net.isSecure) sub = L"Secured";
            else sub = L"Open";

            pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.5f));
            pRenderTarget->DrawTextW(sub.c_str(), (UINT32)sub.length(), pTextFormat,
                D2D1::RectF(pad + 10, curY + 24, NET_LOGICAL_WIDTH - 60, curY + 40), pFgBrush);

            DrawSignalIcon(D2D1::RectF(NET_LOGICAL_WIDTH - 40, curY + 10, NET_LOGICAL_WIDTH - 20, curY + 30), net.signalQuality);
            if (net.isSecure) DrawLockIcon(D2D1::RectF(NET_LOGICAL_WIDTH - 55, curY + 15, NET_LOGICAL_WIDTH - 45, curY + 25));

            if (isExpanded) {
                float exY = curY + ITEM_HEIGHT;

                if (isBusy && net.ssid == selectedSSID) {
                    pFgBrush->SetColor(style.global.highlights);
                    pRenderTarget->DrawTextW(connectionStatusMsg.c_str(), (UINT32)connectionStatusMsg.length(), pTextFormat,
                        D2D1::RectF(pad + 10, exY, NET_LOGICAL_WIDTH, exY + 20), pFgBrush);
                }

                float btnY = curY + h - 35.0f;

                if (!net.isConnected && net.isSecure) {
                    float boxY = exY + 10;
                    passwordBoxRect = D2D1::RectF(pad + 10, boxY, NET_LOGICAL_WIDTH - pad - 10, boxY + 28);

                    pBgBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.3f));
                    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(passwordBoxRect, 2, 2), pBgBrush);
                    pBorderBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.2f));
                    pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(passwordBoxRect, 2, 2), pBorderBrush);

                    pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
                    std::wstring displayPwd = passwordInput.empty() ? L"Enter network security key" : std::wstring(passwordInput.length(), L'*');
                    if (passwordInput.empty()) pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.4f));

                    pRenderTarget->PushAxisAlignedClip(passwordBoxRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    pRenderTarget->DrawTextW(displayPwd.c_str(), (UINT32)displayPwd.length(), pTextFormat,
                        D2D1::RectF(passwordBoxRect.left + 5, passwordBoxRect.top + 4, passwordBoxRect.right, passwordBoxRect.bottom), pFgBrush);
                    pRenderTarget->PopAxisAlignedClip();
                }

                connectBtnRect = D2D1::RectF(NET_LOGICAL_WIDTH - 110, btnY, NET_LOGICAL_WIDTH - pad - 10, btnY + 28);

                pBgBrush->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.3f, 0.6f));
                if (!isBusy) pBgBrush->SetColor(style.global.highlights);

                pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(connectBtnRect, 3, 3), pBgBrush);

                std::wstring btnText = net.isConnected ? L"Disconnect" : L"Connect";
                pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
                pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                pRenderTarget->DrawTextW(btnText.c_str(), (UINT32)btnText.length(), pTextFormat,
                    D2D1::RectF(connectBtnRect.left, connectBtnRect.top + 4, connectBtnRect.right, connectBtnRect.bottom), pFgBrush);
                pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }

            D2D1_RECT_F hitRect = itemRect;
            hitRect.bottom = curY + ITEM_HEIGHT;
            networkItemRects.push_back({ hitRect, net.ssid });
        }

        curY += h;
        totalH += h;
    }

    pRenderTarget->PopAxisAlignedClip();

    maxScroll = (std::max)(0.0f, totalH - listHeight);
    if (maxScroll > 0) {
        float trackH = listHeight;
        float thumbH = (std::max)(30.0f, (listHeight / totalH) * trackH);
        float thumbY = y + (scrollOffset / maxScroll) * (trackH - thumbH);

        scrollTrackRect = D2D1::RectF(NET_LOGICAL_WIDTH - 10, y, NET_LOGICAL_WIDTH - 2, y + trackH);
        scrollThumbRect = D2D1::RectF(NET_LOGICAL_WIDTH - 8, thumbY, NET_LOGICAL_WIDTH - 4, thumbY + thumbH);

        pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.2f));
        if (isDraggingScrollbar) pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.4f));
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(scrollThumbRect, 2, 2), pBgBrush);
    }

    pRenderTarget->EndDraw();
}

void NetworkFlyout::DrawSignalIcon(D2D1_RECT_F rect, int quality) {
    if (pIconFormat) {
        const wchar_t *glyph = L"\uE701";
        if (quality > 90) glyph = L"\uE874";
        else if (quality > 70) glyph = L"\uE873";
        else if (quality > 40) glyph = L"\uE872";
        else if (quality > 10) glyph = L"\uE871";

        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRenderTarget->DrawTextW(glyph, 1, pIconFormat, rect, pFgBrush);
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    else {
        float w = rect.right - rect.left;
        float h = rect.bottom - rect.top;
        float barW = (w / 4.0f) - 2.0f;
        int bars = 1;
        if (quality > 40) bars = 2;
        if (quality > 70) bars = 3;
        if (quality > 90) bars = 4;

        for (int i = 0; i < 4; i++) {
            float barH = (h * 0.4f) + (h * 0.6f * (i / 3.0f));
            D2D1_RECT_F r = D2D1::RectF(
                rect.left + (i * (barW + 2)),
                rect.bottom - barH,
                rect.left + (i * (barW + 2)) + barW,
                rect.bottom
            );
            if (i < bars) pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1.0f));
            else pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.2f));
            pRenderTarget->FillRectangle(r, pFgBrush);
        }
    }
}

void NetworkFlyout::DrawLockIcon(D2D1_RECT_F rect) {
    if (pIconFormat) {
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRenderTarget->DrawTextW(L"\uE72E", 1, pIconFormat, rect, pFgBrush);
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    else {
        float w = rect.right - rect.left;
        float h = rect.bottom - rect.top;
        D2D1_RECT_F body = D2D1::RectF(rect.left, rect.top + h * 0.4f, rect.right, rect.bottom);
        pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.8f));
        pRenderTarget->FillRectangle(body, pFgBrush);
        D2D1_RECT_F shackle = D2D1::RectF(rect.left + w * 0.2f, rect.top, rect.right - w * 0.2f, rect.top + h * 0.5f);
        pBorderBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.8f));
        pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(shackle, 2, 2), pBorderBrush, 1.5f);
    }
}

void NetworkFlyout::OnClick(int x, int y) {
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    float lx = x / scale;
    float ly = y / scale;

    if (lx >= refreshBtnRect.left && lx <= refreshBtnRect.right &&
        ly >= refreshBtnRect.top && ly <= refreshBtnRect.bottom) {
        ScanAsync();
        return;
    }

    if (maxScroll > 0) {
        if (lx >= scrollTrackRect.left - 5 && lx <= scrollTrackRect.right + 5 &&
            ly >= scrollTrackRect.top && ly <= scrollTrackRect.bottom) {

            isDraggingScrollbar = true;
            dragStartY = ly;
            dragStartScrollOffset = scrollOffset;

            if (ly < scrollThumbRect.top || ly > scrollThumbRect.bottom) {
                float trackH = scrollTrackRect.bottom - scrollTrackRect.top;
                float pct = (ly - scrollTrackRect.top) / trackH;
                scrollOffset = pct * maxScroll;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
    }

    if (!selectedSSID.empty() && connectBtnRect.right > 0) {
        if (lx >= connectBtnRect.left && lx <= connectBtnRect.right &&
            ly >= connectBtnRect.top && ly <= connectBtnRect.bottom) {

            for (const auto &net : cachedNetworks) {
                if (net.ssid == selectedSSID) {
                    if (net.isConnected) {
                        backend.Disconnect();
                        ScanAsync();
                    }
                    else {
                        ConnectAsync(net, passwordInput);
                    }
                    return;
                }
            }
        }
    }

    if (lx >= passwordBoxRect.left && lx <= passwordBoxRect.right &&
        ly >= passwordBoxRect.top && ly <= passwordBoxRect.bottom) {
        return;
    }

    for (const auto &pair : networkItemRects) {
        if (lx >= pair.first.left && lx <= pair.first.right &&
            ly >= pair.first.top && ly <= pair.first.bottom) {

            if (selectedSSID == pair.second) {
                // Keep selection
            }
            else {
                selectedSSID = pair.second;
                passwordInput = L"";
                connectionStatusMsg = L"";
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
    }
}

void NetworkFlyout::OnChar(wchar_t c) {
    if (selectedSSID.empty()) return;

    if (c == VK_BACK) {
        if (!passwordInput.empty()) passwordInput.pop_back();
    }
    else if (c == VK_RETURN) {
        for (const auto &net : cachedNetworks) {
            if (net.ssid == selectedSSID && !net.isConnected) {
                ConnectAsync(net, passwordInput);
                break;
            }
        }
    }
    else if (c >= 32 && c != 127) {
        passwordInput += c;
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

void NetworkFlyout::OnScroll(int delta) {
    scrollOffset -= (delta / (float)WHEEL_DELTA) * 40.0f;
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
    InvalidateRect(hwnd, NULL, FALSE);
}

void NetworkFlyout::OnDrag(int x, int y) {
    if (!isDraggingScrollbar) return;
    float dpi = (float)GetDpiForWindow(hwnd);
    float ly = y / (dpi / 96.0f);

    float trackH = scrollTrackRect.bottom - scrollTrackRect.top;
    float thumbH = scrollThumbRect.bottom - scrollThumbRect.top;

    if (trackH > thumbH) {
        float delta = ly - dragStartY;
        float scale = maxScroll / (trackH - thumbH);
        scrollOffset = dragStartScrollOffset + (delta * scale);

        if (scrollOffset < 0) scrollOffset = 0;
        if (scrollOffset > maxScroll) scrollOffset = maxScroll;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}