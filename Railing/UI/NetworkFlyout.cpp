#include "NetworkFlyout.h"
#include "RailingRenderer.h"
#include "Railing.h" 
#include <dwmapi.h>
#include <windowsx.h>
#include <shellapi.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

ULONGLONG NetworkFlyout::lastAutoCloseTime = 0;

NetworkFlyout::NetworkFlyout(HINSTANCE hInst,
    ID2D1Factory *pFact,
    IDWriteFactory *pWFact,
    IDWriteTextFormat *pTxt,
    IDWriteTextFormat *pIcon,
    ThemeConfig &theme)
    : hInst(hInst), pFactory(pFact), pWriteFactory(pWFact), pTextFormat(pTxt), pIconFormat(pIcon), theme(theme)
{
    WNDCLASS wc = {};
    if (!GetClassInfo(hInst, L"RailingNetworkFlyout", &wc)) {
        wc.lpfnWndProc = NetworkFlyout::WndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"RailingNetworkFlyout";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
    }
}

NetworkFlyout::~NetworkFlyout() {
    if (pRenderTarget) pRenderTarget->Release();
    if (pTextBrush) pTextBrush->Release();
    if (pBgBrush) pBgBrush->Release();
    if (pHighlightBrush) pHighlightBrush->Release();
    if (pScrollBrush) pScrollBrush->Release();
    if (pBorderBrush) pBorderBrush->Release();
    if (pInputLayout) pInputLayout->Release();
}

void NetworkFlyout::UpdateInputLayout(float maxWidth) {
    if (pInputLayout) { pInputLayout->Release(); pInputLayout = nullptr; }

    std::wstring masked(passwordBuffer.length(), L'*');
    if (masked.empty()) masked = L""; // Empty layout for placeholder logic

    HRESULT hr = pWriteFactory->CreateTextLayout(
        masked.c_str(),
        (UINT32)masked.length(),
        pTextFormat,
        maxWidth,
        35.0f, // height
        &pInputLayout
    );

    if (SUCCEEDED(hr) && pInputLayout) {
        pInputLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pInputLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
}

int NetworkFlyout::GetInputCaretFromPoint(float x, float y, float offsetX, float offsetY) {
    if (!pInputLayout) return 0;

    BOOL isTrailing;
    BOOL isInside;
    DWRITE_HIT_TEST_METRICS metrics;
    pInputLayout->HitTestPoint(x - offsetX, y - offsetY, &isTrailing, &isInside, &metrics);
    int idx = metrics.textPosition + (isTrailing ? 1 : 0);
    return idx;
}

std::wstring NetworkFlyout::GetClipboardText() {
    if (!OpenClipboard(NULL)) return L"";

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL) { CloseClipboard(); return L""; }

    wchar_t *pszText = static_cast<wchar_t *>(GlobalLock(hData));
    if (pszText == NULL) { CloseClipboard(); return L""; }

    std::wstring text(pszText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

void NetworkFlyout::SetClipboardText(const std::wstring &text) {
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();

    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(wchar_t));
    if (hGlob) {
        memcpy(GlobalLock(hGlob), text.c_str(), (text.length() + 1) * sizeof(wchar_t));
        GlobalUnlock(hGlob);
        SetClipboardData(CF_UNICODETEXT, hGlob);
    }
    CloseClipboard();
}

bool NetworkFlyout::OnSetCursor(int x, int y) {
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    float lx = x / scale;
    float ly = y / scale;
    std::lock_guard<std::recursive_mutex> lock(networkMutex);

    float currentY = headerHeight - scrollOffset;
    bool hand = false;

    for (int i = 0; i < (int)cachedNetworks.size(); i++) {
        float h = (i == selectedIndex) ? expandedHeight : itemHeight;
        if (ly >= currentY && ly < currentY + h) {

            if (i == selectedIndex) {
                float contentY = currentY + 50;
                float infoSize = 35.0f;

                if (cachedNetworks[i].isConnected) {
                    float btnWidth = 120.0f;
                    float gap = 10.0f;
                    float totalGroupW = btnWidth + gap + infoSize;
                    float startX = 5 + ((width - 10) - totalGroupW) / 2.0f;
                    if (lx >= startX && lx <= startX + btnWidth && ly >= contentY && ly <= contentY + 35) hand = true;
                    float infoLeft = startX + btnWidth + gap;
                    if (lx >= infoLeft && lx <= infoLeft + infoSize && ly >= contentY && ly <= contentY + infoSize) hand = true;
                }
                else {
                    float infoLeft = 5 + 15;
                    if (lx >= infoLeft && lx <= infoLeft + infoSize && ly >= contentY && ly <= contentY + infoSize) hand = true;
                    if (lx > width - 85 && lx < width - 10 && ly >= contentY && ly <= contentY + 35) hand = true;

                    float boxLeft = infoLeft + infoSize + 10;
                    if (lx >= boxLeft && lx <= width - 90 && ly >= contentY && ly <= contentY + 35) {
                        SetCursor(LoadCursor(NULL, IDC_IBEAM));
                        return true;
                    }
                }
            }
            else hand = true;
            break;
        }
        currentY += h;
    }

    if (hand) {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return true;
    }

    return false;
}

void NetworkFlyout::PositionWindow(RECT anchorRect) {
    if (!hwnd) return;
    this->currentAnchor = anchorRect;

    size_t netCount = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(networkMutex);
        netCount = cachedNetworks.size();
    }
    if (netCount == 0) netCount = 8;

    int totalH = headerHeight + 10;
    for (size_t i = 0; i < netCount; i++) {
        if ((int)i == selectedIndex) totalH += expandedHeight;
        else totalH += itemHeight;
    }

    if (totalH > maxWindowHeight) {
        currentWindowHeight = maxWindowHeight;
        maxScroll = (float)(totalH - maxWindowHeight);
    }
    else {
        currentWindowHeight = totalH;
        maxScroll = 0;
    }
    if (currentWindowHeight < 100) currentWindowHeight = 100;

    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    int w = (int)(width * scale);
    int h = (int)(currentWindowHeight * scale);

    this->targetX = anchorRect.left + ((anchorRect.right - anchorRect.left) / 2) - (w / 2);

    HMONITOR hMon = MonitorFromRect(&anchorRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) }; GetMonitorInfo(hMon, &mi);
    int screenCenterY = (mi.rcMonitor.bottom - mi.rcMonitor.top) / 2;
    int gap = (int)(12 * scale);

    if (anchorRect.top > screenCenterY) {
        this->targetY = anchorRect.top - h - gap;
        if (this->targetY < mi.rcWork.top) this->targetY = mi.rcWork.top + gap;
    }
    else {
        this->targetY = anchorRect.bottom + gap;
    }

    if (this->targetX < mi.rcWork.left + gap) this->targetX = mi.rcWork.left + gap;
    if (this->targetX + w > mi.rcWork.right - gap) this->targetX = mi.rcWork.right - w - gap;
}

void NetworkFlyout::Toggle(RECT anchorRect) {
    ULONGLONG now = GetTickCount64();
    if (now - lastAutoCloseTime < 200) return;

    if (!hwnd) {
        hwnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            L"RailingNetworkFlyout", L"", WS_POPUP, 0, 0, width, currentWindowHeight,
            NULL, NULL, hInst, this
        );

        if (theme.global.blur) RailingRenderer::EnableBlur(hwnd, 0x00000000);
        else { MARGINS m = { -1 }; DwmExtendFrameIntoClientArea(hwnd, &m); }

        DWM_WINDOW_CORNER_PREFERENCE pref = (theme.global.radius > 0.0f) ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }

    if (animState == AnimationState::Visible || animState == AnimationState::Entering) {
        if (theme.global.animation.enabled) {
            animState = AnimationState::Exiting;
            lastAnimTime = now;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else {
            animState = AnimationState::Hidden;
            ShowWindow(hwnd, SW_HIDE);
        }
    }
    else {
        scrollOffset = 0;
        selectedIndex = -1;
        passwordBuffer = L"";
        statusMessage = L"";
        isWorking = false;

        PositionWindow(anchorRect);
        if (now - lastScanTime > 10000) {
            isScanning = true;
            lastScanTime = now;
            std::thread([this]() {
                backend.RequestScan();
                auto nets = backend.ScanNetworks();
                try {
                    std::lock_guard<std::recursive_mutex> lock(networkMutex);
                    cachedNetworks = nets;
                }
                catch (...) {}

                if (IsWindow(hwnd)) PostMessage(hwnd, WM_NET_SCAN_COMPLETE, 0, 0);
                Sleep(3500);

                nets = backend.ScanNetworks();
                try {
                    std::lock_guard<std::recursive_mutex> lock(networkMutex);
                    cachedNetworks = nets;
                }
                catch (...) {} // Trigger a redraw with the new data
                isScanning = false;
                if (IsWindow(hwnd)) PostMessage(hwnd, WM_NET_SCAN_COMPLETE, 0, 0);

                }).detach();
        }
        else isScanning = false;

        float dpi = (float)GetDpiForWindow(hwnd);
        float scale = dpi / 96.0f;
        int w = (int)(width * scale);
        int h = (int)(currentWindowHeight * scale);

        int r = (int)theme.global.radius;
        HRGN hRgn = (r > 0) ? CreateRoundRectRgn(0, 0, w + 1, h + 1, r * 2, r * 2) : CreateRectRgn(0, 0, w, h);
        SetWindowRgn(hwnd, hRgn, TRUE);

        if (theme.global.animation.enabled) {
            animState = AnimationState::Entering;
            currentAlpha = 0.01f;
            currentOffset = (targetY > 500) ? 20.0f : -20.0f;
            lastAnimTime = now;
        }
        else {
            animState = AnimationState::Visible;
            currentAlpha = 1.0f;
            currentOffset = 0.0f;
        }

        SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY + (int)currentOffset, w, h, SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);

        if (pRenderTarget) pRenderTarget->Resize(D2D1::SizeU(w, h));
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void NetworkFlyout::UpdateAnimation() {
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
        currentOffset = (1.0f - currentAlpha) * 20.0f * (targetY > 500 ? 1 : -1);
        needsMove = true;
    }
    else if (animState == AnimationState::Exiting) {
        currentAlpha -= deltaTime * animSpeed;
        if (currentAlpha <= 0.0f) {
            currentAlpha = 0.0f;
            animState = AnimationState::Hidden;
            ShowWindow(hwnd, SW_HIDE);
            return;
        }
        currentOffset = (1.0f - currentAlpha) * 20.0f * (targetY > 500 ? 1 : -1);
        needsMove = true;
    }

    if (needsMove && animState != AnimationState::Hidden) {
        SetLayeredWindowAttributes(hwnd, 0, (BYTE)(currentAlpha * 255), LWA_ALPHA);
        SetWindowPos(hwnd, NULL, targetX, targetY + (int)currentOffset, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void NetworkFlyout::CreateDeviceResources() {
    if (!pRenderTarget && hwnd) {
        RECT rc; GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        HRESULT hr = pFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &pRenderTarget);

        if (SUCCEEDED(hr) && pRenderTarget) {
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBgBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &pTextBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.08f), &pHighlightBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.3f), &pScrollBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBorderBrush);
        }
    }
    else if (pRenderTarget) {
        RECT rc; GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
        if (pRenderTarget->GetPixelSize().width != size.width) {
            pRenderTarget->Resize(size);
        }
    }
}

void NetworkFlyout::Draw() {
    UpdateAnimation();
    if (animState == AnimationState::Hidden) return;
    CreateDeviceResources();
    if (!pRenderTarget) return;

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    std::lock_guard<std::recursive_mutex> lock(networkMutex);

    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_RECT_F layout = D2D1::RectF(0, 0, (float)rc.right, (float)rc.bottom);

    D2D1_COLOR_F bgColor = theme.global.background;
    if (theme.global.blur && bgColor.a > 0.6f) bgColor.a = 0.6f;
    bgColor.a *= currentAlpha;
    if (bgColor.a == 0.0f) bgColor = D2D1::ColorF(0.08f, 0.08f, 0.1f, 0.6f * currentAlpha);
    
    float rad = theme.global.radius;
    pBgBrush->SetColor(bgColor);
    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(layout, rad, rad), pBgBrush);

    if (theme.global.borderWidth > 0.0f) {
        pBorderBrush->SetColor(theme.global.borderColor);
        pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(layout, rad, rad), pBorderBrush, theme.global.borderWidth);
    }

    float scale = GetDpiForWindow(hwnd) / 96.0f;
    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
    float logicalW = (float)width;
    std::wstring titleText = L"Wi-Fi Networks";
    if (isScanning) titleText = L"Scanning...";
    pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, currentAlpha));
    pRenderTarget->DrawTextW(titleText.c_str(), (UINT32)titleText.length(), pTextFormat, D2D1::RectF(0, 0, logicalW, (float)headerHeight), pTextBrush);
    pHighlightBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f * currentAlpha));
    pRenderTarget->DrawLine(D2D1::Point2F(15, (float)headerHeight), D2D1::Point2F(logicalW - 15, (float)headerHeight), pHighlightBrush, 1.0f);

    pRenderTarget->PushAxisAlignedClip(D2D1::RectF(0, (float)headerHeight, logicalW, (float)currentWindowHeight - 5), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (cachedNetworks.empty()) {
        // If the list is empty, show status text in the middle
        std::wstring emptyText = isScanning ? L"Scanning..." : L"No Networks Found";

        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.6f * currentAlpha));
        pRenderTarget->DrawTextW(emptyText.c_str(), (UINT32)emptyText.length(), pTextFormat, D2D1::RectF(0, 60, logicalW, 100), pTextBrush);

        // Force redraw so we see the status change when scan finishes
        if (isScanning) InvalidateRect(hwnd, NULL, FALSE);
    }
    else {
        float y = (float)headerHeight - scrollOffset;

        for (int i = 0; i < (int)cachedNetworks.size(); i++) {
            float height = (i == selectedIndex) ? (float)expandedHeight : (float)itemHeight;

            if (y + height < headerHeight) { y += height; continue; }
            if (y > currentWindowHeight) break;

            WifiNetwork &net = cachedNetworks[i];
            D2D1_RECT_F rowRect = D2D1::RectF(5, y, logicalW - 5, y + height);

            if (i == hoveredIndex && i != selectedIndex) {
                pHighlightBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f * currentAlpha));
                pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rowRect, 4, 4), pHighlightBrush);
            }
            if (i == selectedIndex) {
                pHighlightBrush->SetColor(D2D1::ColorF(0, 0, 0, 0.3f * currentAlpha));
                pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(rowRect, 4, 4), pHighlightBrush);
            }

            std::wstring icon = L"\uE871";
            if (net.signalQuality > 80) icon = L"\uE701";
            else if (net.signalQuality > 60) icon = L"\uE874";
            else if (net.signalQuality > 40) icon = L"\uE873";
            else if (net.signalQuality > 20) icon = L"\uE872";

            D2D1_RECT_F iconRect = D2D1::RectF(rowRect.left + 5, rowRect.top, rowRect.left + 45, rowRect.top + itemHeight);
            D2D1_COLOR_F btnColor = D2D1::ColorF(D2D1::ColorF::CornflowerBlue);
            btnColor.a *= currentAlpha;
            pTextBrush->SetColor(net.isConnected ? btnColor : D2D1::ColorF(1, 1, 1, currentAlpha));
            pRenderTarget->DrawTextW(icon.c_str(), 1, pIconFormat, iconRect, pTextBrush);

            // SSID
            D2D1_RECT_F textRect = D2D1::RectF(rowRect.left + 50, rowRect.top, rowRect.right - 35, rowRect.top + itemHeight);
            pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, currentAlpha));
            pRenderTarget->DrawTextW(net.ssid.c_str(), (UINT32)net.ssid.length(), pTextFormat, textRect, pTextBrush);
            pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

            if (net.isSecure) {
                D2D1_RECT_F lockRect = D2D1::RectF(rowRect.right - 35, rowRect.top, rowRect.right - 5, rowRect.top + itemHeight);
                pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.5f * currentAlpha));
                pRenderTarget->DrawTextW(L"\uE72E", 1, pIconFormat, lockRect, pTextBrush);
            }

            if (i == selectedIndex) {
                float contentY = rowRect.top + 50;

                if (net.isConnected) {
                    float btnWidth = 120.0f;
                    float gap = 10.0f;
                    float infoSize = 35.0f;
                    float totalGroupW = btnWidth + gap + infoSize;
                    float startX = rowRect.left + ((rowRect.right - rowRect.left) - totalGroupW) / 2.0f;

                    D2D1_RECT_F btnRect = D2D1::RectF(startX, contentY, startX + btnWidth, contentY + 35);
                    pBgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Crimson, 0.8f * currentAlpha));
                    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnRect, 4, 4), pBgBrush);
                    pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, currentAlpha));
                    pRenderTarget->DrawTextW(L"Disconnect", 10, pTextFormat, btnRect, pTextBrush);

                    D2D1_RECT_F infoRect = D2D1::RectF(btnRect.right + gap, contentY, btnRect.right + gap + infoSize, contentY + infoSize);
                    pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f * currentAlpha));
                    pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(infoRect.left + infoSize / 2, infoRect.top + infoSize / 2), infoSize / 2, infoSize / 2), pBgBrush);
                    pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, currentAlpha));
                    pRenderTarget->DrawTextW(L"\uE946", 1, pIconFormat, infoRect, pTextBrush);
                }
                else {
                    float infoSize = 35.0f;
                    D2D1_RECT_F infoRect = D2D1::RectF(rowRect.left + 15, contentY, rowRect.left + 15 + infoSize, contentY + infoSize);
                    pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f * currentAlpha));
                    pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(infoRect.left + infoSize / 2, infoRect.top + infoSize / 2), infoSize / 2, infoSize / 2), pBgBrush);
                    pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, currentAlpha));
                    pRenderTarget->DrawTextW(L"\uE946", 1, pIconFormat, infoRect, pTextBrush);

                    D2D1_RECT_F boxRect = D2D1::RectF(infoRect.right + 10, contentY, rowRect.right - 90, contentY + 35);

                    pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f * currentAlpha));
                    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(boxRect, 4, 4), pBgBrush);
                    pBorderBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.3f * currentAlpha));
                    pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(boxRect, 4, 4), pBorderBrush, 1.0f);

                    if (!pInputLayout) UpdateInputLayout(boxRect.right - boxRect.left - 10);

                    D2D1_POINT_2F textOrigin = D2D1::Point2F(boxRect.left + 5, boxRect.top);

                    if (pInputLayout && !isWorking) {
                        if (passwordBuffer.empty()) pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.4f));
                        else pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, 1.0f));
                        pRenderTarget->DrawTextLayout(textOrigin, pInputLayout, pTextBrush);
                    }

                    if (!passwordBuffer.empty() && pInputLayout && !isWorking) {
                        int selStart = min(selectionAnchor, selectionFocus);
                        int selLen = abs(selectionAnchor - selectionFocus);
                        if (selLen > 0) {
                            DWRITE_HIT_TEST_METRICS metrics[32];
                            UINT32 count = 0;
                            pInputLayout->HitTestTextRange(selStart, selLen, 0, 0, metrics, 32, &count);
                            pBgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::DodgerBlue, 0.5f * currentAlpha));
                            for (UINT32 m = 0; m < count; m++) {
                                D2D1_RECT_F highlight = D2D1::RectF(
                                    textOrigin.x + metrics[m].left, textOrigin.y + metrics[m].top,
                                    textOrigin.x + metrics[m].left + metrics[m].width, textOrigin.y + metrics[m].top + metrics[m].height
                                );
                                pRenderTarget->FillRectangle(highlight, pBgBrush);
                            }
                        }

                        float caretX, caretY;
                        DWRITE_HIT_TEST_METRICS cm;
                        pInputLayout->HitTestTextPosition(selectionFocus, FALSE, &caretX, &caretY, &cm);
                        float finalX = textOrigin.x + caretX;
                        float finalY = textOrigin.y + caretY;
                        float lineCenter = finalY + (cm.height / 2.0f);
                        pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, 1.0f));
                        pRenderTarget->DrawLine(D2D1::Point2F(finalX, lineCenter - 8), D2D1::Point2F(finalX, lineCenter + 8), pTextBrush, 1.0f);
                    }

                    D2D1_RECT_F btnRect = D2D1::RectF(rowRect.right - 85, contentY, rowRect.right - 10, contentY + 35);
                    float btnAlpha = isWorking ? 0.4f : 0.8f;
                    D2D1_COLOR_F btnColor = theme.global.highlights;
                    btnColor.a *= (btnAlpha * currentAlpha);
                    pBgBrush->SetColor(btnColor);
                    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(btnRect, 4, 4), pBgBrush);
                    pTextBrush->SetColor(D2D1::ColorF(1, 1, 1, currentAlpha));
                    pRenderTarget->DrawTextW(isWorking ? L"..." : L"Go", 3, pTextFormat, btnRect, pTextBrush);
                }

                if (!statusMessage.empty()) {
                    D2D1_RECT_F statRect = D2D1::RectF(rowRect.left + 20, contentY + 40, rowRect.right - 10, contentY + 65);
                    pTextBrush->SetColor(statusColor);
                    pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    pRenderTarget->DrawTextW(statusMessage.c_str(), (UINT32)statusMessage.length(), pTextFormat, statRect, pTextBrush);
                    pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                }
            }
            y += height;
        }
    }
    pRenderTarget->PopAxisAlignedClip();

    if (isScanning) {
        float trackH = 3.0f;
        float trackY = (float)headerHeight; // Draw exactly on the separator line

        // Use system time for smooth looping
        float time = (float)GetTickCount64() / 1000.0f;
        float t = fmod(time * 1.5f, 1.0f); // Speed multiplier

        float barWidth = width * 0.3f; // Bar is 30% width of window
        float range = width + barWidth;
        float offset = (range * t) - barWidth;

        // Calculate raw positions
        float startX = offset;
        float endX = startX + barWidth;

        // Clip to window bounds so it doesn't draw outside
        float clStart = max(startX, 0.0f);
        float clEnd = min(endX, (float)width);

        if (clEnd > clStart) {
            D2D1_RECT_F barRect = D2D1::RectF(clStart, trackY, clEnd, trackY + trackH);
            D2D1_COLOR_F btnColor = theme.global.highlights;
            btnColor.a *= currentAlpha;
            pBgBrush->SetColor(btnColor);
            pRenderTarget->FillRectangle(barRect, pBgBrush);
        }
    }

    if (maxScroll > 0) {
        float trackH = (float)currentWindowHeight - headerHeight - 10;
        float thumbH = max(30.0f, trackH * (trackH / (trackH + maxScroll)));
        float thumbY = headerHeight + 5 + (scrollOffset / maxScroll) * (trackH - thumbH);
        D2D1_RECT_F thumb = D2D1::RectF(logicalW - 6, thumbY, logicalW - 2, thumbY + thumbH);
        pScrollBrush->SetColor(D2D1::ColorF(1, 1, 1, (isDraggingScrollbar ? 0.6f : 0.3f) * currentAlpha));
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(thumb, 2, 2), pScrollBrush);
    }

    pRenderTarget->EndDraw();
    if (isScanning) InvalidateRect(hwnd, NULL, FALSE);
}

void NetworkFlyout::OnHover(int x, int y) {
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    float lx = x / scale;
    float ly = y / scale;
    float currentY = headerHeight - scrollOffset;
    std::lock_guard<std::recursive_mutex> lock(networkMutex);
    int newHover = -1;
    for (int i = 0; i < (int)cachedNetworks.size(); i++) {
        float h = (i == selectedIndex) ? expandedHeight : itemHeight;
        if (ly >= currentY && ly < currentY + h) { newHover = i; break; }
        currentY += h;
    }
    if (hoveredIndex != newHover) { hoveredIndex = newHover; InvalidateRect(hwnd, NULL, FALSE); }
}

void NetworkFlyout::OnDrag(int x, int y) {
    float scale = GetDpiForWindow(hwnd) / 96.0f;
    float lx = x / scale;
    float ly = y / scale;

    if (isSelectingText && selectedIndex != -1) {
        std::lock_guard<std::recursive_mutex> lock(networkMutex);
        float currentY = headerHeight - scrollOffset;

        for (int i = 0; i < selectedIndex; i++) currentY += itemHeight;

        float contentY = currentY + 50;
        D2D1_RECT_F boxRect = D2D1::RectF(25, contentY, width - 95, contentY + 35);
        int idx = GetInputCaretFromPoint(lx, ly, boxRect.left + 5, boxRect.top);
        if (selectionFocus != idx) {
            selectionFocus = idx;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return;
    }

    if (isDraggingScrollbar) {
        float logicalY = y / scale;
        float trackH = (float)currentWindowHeight - headerHeight - 10;
        float contentH = trackH + maxScroll;
        float thumbH = trackH * (trackH / contentH);
        float usableTrack = trackH - thumbH;
        if (usableTrack > 0) {
            float deltaY = logicalY - dragStartY;
            float scrollPerPixel = maxScroll / usableTrack;
            scrollOffset = dragStartScrollOffset + (deltaY * scrollPerPixel);
            if (scrollOffset < 0) scrollOffset = 0;
            if (scrollOffset > maxScroll) scrollOffset = maxScroll;
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

void NetworkFlyout::OnClick(int x, int y) {
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    float scale = GetDpiForWindow(hwnd) / 96.0f;
    float lx = x / scale;
    float ly = y / scale;

    if (maxScroll > 0 && lx > width - 15) {
        if (GetKeyState(VK_LBUTTON) < 0 && !isDraggingScrollbar) {
            isDraggingScrollbar = true;
            dragStartY = ly;
            dragStartScrollOffset = scrollOffset;
            return;
        }
    }

    float currentY = headerHeight - scrollOffset;
    std::lock_guard<std::recursive_mutex> lock(networkMutex);

    for (int i = 0; i < (int)cachedNetworks.size(); i++) {
        float h = (i == selectedIndex) ? expandedHeight : itemHeight;

        if (ly >= currentY && ly < currentY + h) {
            if (i == selectedIndex && !cachedNetworks[i].isConnected) {
                float contentY = currentY + 50;
                float infoSize = 35.0f;
                float boxLeft = 5 + 15 + infoSize + 10; // = 65
                D2D1_RECT_F boxRect = D2D1::RectF(boxLeft, contentY, width - 95, contentY + 35);

                if (lx >= boxRect.left && lx <= boxRect.right && ly >= boxRect.top && ly <= boxRect.bottom) {
                    isSelectingText = true;
                    int idx = GetInputCaretFromPoint(lx, ly, boxRect.left + 5, boxRect.top);
                    selectionAnchor = idx;
                    selectionFocus = idx;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return;
                }
            }

            if (i == selectedIndex) {
                float contentY = currentY + 50;
                float infoSize = 35.0f;

                if (cachedNetworks[i].isConnected) {
                    float btnWidth = 120.0f;
                    float gap = 10.0f;
                    float totalGroupW = btnWidth + gap + infoSize;
                    float startX = 5 + ((width - 10) - totalGroupW) / 2.0f;

                    if (lx >= startX && lx <= startX + btnWidth && ly >= contentY && ly <= contentY + 35) {
                        backend.Disconnect();
                        selectedIndex = -1;
                        InvalidateRect(hwnd, NULL, FALSE);
                        return;
                    }

                    float infoLeft = startX + btnWidth + gap;
                    if (lx >= infoLeft && lx <= infoLeft + infoSize && ly >= contentY && ly <= contentY + infoSize) {
                        ShellExecute(NULL, L"open", L"ms-settings:network-properties", NULL, NULL, SW_SHOWNORMAL);
                        return;
                    }
                }
                else {
                    float infoLeft = 5 + 15; // rowRect.left + 15
                    if (lx >= infoLeft && lx <= infoLeft + infoSize && ly >= contentY && ly <= contentY + infoSize) {
                        ShellExecute(NULL, L"open", L"ms-settings:network-wifi", NULL, NULL, SW_SHOWNORMAL);
                        return;
                    }

                    if (lx > width - 85 && lx < width - 10 && ly >= contentY && ly <= contentY + 35) {
                        if (isWorking) return;
                        isWorking = true;
                        statusMessage = L"Verifying...";
                        statusColor = D2D1::ColorF(1, 1, 1, 0.7f);
                        InvalidateRect(hwnd, NULL, FALSE);

                        std::thread([this, i]() {
                            std::wstring passCopy = passwordBuffer;
                            WifiNetwork netCopy = cachedNetworks[i];
                            std::wstring result = backend.ConnectTo(netCopy, passCopy);
                            {
                                std::lock_guard<std::recursive_mutex> uiLock(networkMutex);
                                statusMessage = result;
                                if (result == L"Connected") statusColor = D2D1::ColorF(0.3f, 1.0f, 0.3f, 1.0f);
                                else statusColor = D2D1::ColorF(1.0f, 0.3f, 0.3f, 1.0f);
                                isWorking = false;
                            }
                            if (IsWindow(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
                            }).detach();
                    }
                }
            }
            else {
                selectedIndex = i;
                passwordBuffer = L"";
                statusMessage = L"";
                isWorking = false;
                selectionAnchor = 0; selectionFocus = 0;
                PositionWindow(currentAnchor);
                SetWindowPos(hwnd, NULL, targetX, targetY, (int)(width * scale), (int)(currentWindowHeight * scale), SWP_NOZORDER | SWP_NOACTIVATE);
                if (pRenderTarget) pRenderTarget->Resize(D2D1::SizeU((int)(width * scale), (int)(currentWindowHeight * scale)));
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
        currentY += h;
    }

    isSelectingText = false;

    if (selectedIndex != -1) {
        selectedIndex = -1;
        PositionWindow(currentAnchor);
        SetWindowPos(hwnd, NULL, targetX, targetY, (int)(width * scale), (int)(currentWindowHeight * scale), SWP_NOZORDER | SWP_NOACTIVATE);
        if (pRenderTarget) pRenderTarget->Resize(D2D1::SizeU((int)(width * scale), (int)(currentWindowHeight * scale)));
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void NetworkFlyout::OnChar(wchar_t c) {
    std::lock_guard<std::recursive_mutex> lock(networkMutex);
    if (selectedIndex == -1 || isWorking) return;

    int start = min(selectionAnchor, selectionFocus);
    int end = max(selectionAnchor, selectionFocus);
    if (c == 1) { // CTRL+A
        selectionAnchor = 0;
        selectionFocus = (int)passwordBuffer.length();
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (c == 3) { // CTRL+C
        if (start != end) {
            std::wstring sub = passwordBuffer.substr(start, end - start);
            SetClipboardText(sub);
        }
        return;
    }

    bool textChanged = false;

    if (c == 22) { // CTRL+V
        std::wstring clip = GetClipboardText();
        if (!clip.empty()) {
            if (start != end) passwordBuffer.erase(start, end - start); // Delete selection
            passwordBuffer.insert(start, clip);
            selectionAnchor = selectionFocus = start + (int)clip.length();
            textChanged = true;
        }
    }
    else if (c == VK_BACK) {
        if (start != end) {
            passwordBuffer.erase(start, end - start);
            selectionAnchor = selectionFocus = start;
            textChanged = true;
        }
        else if (start > 0) {
            passwordBuffer.erase(start - 1, 1);
            selectionAnchor = selectionFocus = start - 1;
            textChanged = true;
        }
    }
    else if (c >= 32) {
        if (start != end) passwordBuffer.erase(start, end - start);
        passwordBuffer.insert(start, 1, c);
        selectionAnchor = selectionFocus = start + 1;
        textChanged = true;
    }

    if (textChanged) {
        if (pInputLayout) { pInputLayout->Release(); pInputLayout = nullptr; }
        InvalidateRect(hwnd, NULL, FALSE);
    }
}