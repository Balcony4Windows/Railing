#include "VolumeFlyout.h"
#include "BarInstance.h"
#include <windowsx.h>
#include "RailingRenderer.h"
#include <dwmapi.h>
#include "Railing.h"
#include <functiondiscoverykeys.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

const float LOGICAL_WIDTH = 320.0f;
const float LOGICAL_COMPACT_HEIGHT = 200.0f;
const float LOGICAL_FULL_HEIGHT = 340.0f;

ULONGLONG VolumeFlyout::lastAutoCloseTime = 0;
ULONGLONG VolumeFlyout::lastAnimTime = 0;

VolumeFlyout::VolumeFlyout(BarInstance *owner, HINSTANCE hInst, ID2D1Factory *pSharedFactory, IDWriteFactory *pSharedWriteFactory, IDWriteTextFormat *pFormat, const ThemeConfig &config)
    : ownerBar(owner),
    hInst(hInst), pFactory(pSharedFactory), pWriteFactory(pSharedWriteFactory), pTextFormat(pFormat)
{
	FlyoutManager::Get().Register(this);
    this->style = config.global;
    WNDCLASS wc = { 0 };
    if (!GetClassInfo(hInst, L"VolumeFlyoutClass", &wc)) {
        wc.lpfnWndProc = VolumeFlyout::WindowProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"VolumeFlyoutClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
    }
}

VolumeFlyout::~VolumeFlyout() {
    FlyoutManager::Get().Unregister(this);
    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
	}
}

void VolumeFlyout::PositionWindow(RECT iconRect) {
    if (!hwnd) return;
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;

    int flyWidth = (int)(LOGICAL_WIDTH * scale);
    int flyHeight = (int)(LOGICAL_FULL_HEIGHT * scale);

    HMONITOR hMonitor = MonitorFromRect(&iconRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    GetMonitorInfo(hMonitor, &mi);

    int gap = (int)(12 * scale);

    this->targetX = iconRect.left + ((iconRect.right - iconRect.left) / 2) - (flyWidth / 2);
    this->targetY = iconRect.top - flyHeight - gap;

    if (this->targetY < mi.rcWork.top) this->targetY = iconRect.bottom + gap;
    if (this->targetX < mi.rcWork.left + gap) this->targetX = mi.rcWork.left + gap;
    if (this->targetX + flyWidth > mi.rcWork.right - gap) this->targetX = mi.rcWork.right - flyWidth - gap;
}

void VolumeFlyout::Toggle(RECT iconRect) {
    ULONGLONG now = GetTickCount64();
    if (now - lastAutoCloseTime < 200) return;

    if (!hwnd) {
        hwnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            L"VolumeFlyoutClass", L"VolumeFlyout",
            WS_POPUP,
            0, 0, 300, 180,
            nullptr, nullptr, hInst, this
        );

        if (style.blur) {
            RailingRenderer::EnableBlur(hwnd, 0x00000000);
        }
        else {
            MARGINS margins = { -1 };
            DwmExtendFrameIntoClientArea(hwnd, &margins);
        }

        DWM_WINDOW_CORNER_PREFERENCE preference = (style.radius > 0.0f) ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }

    if (IsVisible()) Hide();
    else {
		FlyoutManager::Get().CloseOthers(this);
        audio.EnsureInitialized(hwnd);
        RefreshDevices();
        PositionWindow(iconRect);

        int screenH = GetSystemMetrics(SM_CYSCREEN);
        float directionOffset = (targetY > screenH / 2) ? 20.0f : -20.0f;

        float dpi = (float)GetDpiForWindow(hwnd);
        float scale = dpi / 96.0f;
        int w = (int)(LOGICAL_WIDTH * scale);
        int h = (int)(LOGICAL_FULL_HEIGHT * scale);
        int r = (int)style.radius;
        HRGN hRgn = (r > 0) ? CreateRoundRectRgn(0, 0, w, h, r * 2, r * 2) : CreateRectRgn(0, 0, w, h);
        SetWindowRgn(hwnd, hRgn, TRUE);
        if (style.animation.enabled) {
            animState = AnimationState::Entering;
            currentAlpha = 0.01f;
            currentOffset = directionOffset;
            lastAnimTime = now;
        }
        else {
            animState = AnimationState::Visible;
            currentAlpha = 1.0f;
            currentOffset = 0.0f;
        }

        SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY + (int)currentOffset, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void VolumeFlyout::Hide()
{
    if (animState == AnimationState::Visible || animState == AnimationState::Entering) {
        ULONGLONG now = GetTickCount64();
        if (style.animation.enabled) {
            animState = AnimationState::Exiting;
            lastAnimTime = now;
            lastAutoCloseTime = now;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else {
            animState = AnimationState::Hidden;
            ShowWindow(hwnd, SW_HIDE);
            lastAutoCloseTime = now;
        }
    }
}

LRESULT CALLBACK VolumeFlyout::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    VolumeFlyout *self = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        self = (VolumeFlyout *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    }
    else {
        self = (VolumeFlyout *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (self) {
        switch (uMsg) {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            self->Draw();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (wParam & MK_LBUTTON) self->OnDrag(x, y);

            float dpi = (float)GetDpiForWindow(hwnd);
            float scale = dpi / 96.0f;
            float lx = x / scale;
            float ly = y / scale;

            // Hover Logic
            D2D1_RECT_F hitRect = self->sliderRect;
            hitRect.top -= 10; hitRect.bottom += 10;
            bool hovering = (lx >= hitRect.left && lx <= hitRect.right && ly >= hitRect.top && ly <= hitRect.bottom);
            if (hovering != self->isHoveringSlider) {
                self->isHoveringSlider = hovering;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (self->isDropdownOpen) InvalidateRect(hwnd, NULL, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_SETCURSOR:
        {
            if (self) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                float dpi = (float)GetDpiForWindow(hwnd);
                float scale = dpi / 96.0f;
                float lx = pt.x / scale;
                float ly = pt.y / scale;
                bool showHand = false;
                if (lx >= self->sliderRect.left - 10 && lx <= self->sliderRect.right + 10 &&
                    ly >= self->sliderRect.top - 15 && ly <= self->sliderRect.bottom + 15) {
                    showHand = true;
                }
                else if (lx >= self->switchRect.left && lx <= self->switchRect.right &&
                    ly >= self->switchRect.top && ly <= self->switchRect.bottom) {
                    if (!self->isHoveringOpenButton) {
                        self->isHoveringOpenButton = true;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                    showHand = true;
                }
                else if (lx >= self->dropdownRect.left && lx <= self->dropdownRect.right &&
                    ly >= self->dropdownRect.top && ly <= self->dropdownRect.bottom) {
                    showHand = true;
                }

                if (!showHand && self->isHoveringOpenButton) {
                    self->isHoveringOpenButton = false;
                    InvalidateRect(hwnd, NULL, FALSE);
                }

                if (!showHand && self->isDropdownOpen) {
                    if (lx >= self->scrollTrackRect.left - 5 && lx <= self->scrollTrackRect.right + 5 &&
                        ly >= self->scrollTrackRect.top && ly <= self->scrollTrackRect.bottom) {
                        showHand = true;
                    }
                    if (!showHand) {
                        for (const auto &r : self->deviceItemRects) {
                            if (lx >= r.left && lx <= r.right && ly >= r.top && ly <= r.bottom) {
                                showHand = true;
                                break;
                            }
                        }
                    }
                }
                if (showHand) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }
        case WM_MOUSELEAVE:
            self->isHoveringSlider = false;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_LBUTTONUP:
            ReleaseCapture();
            self->isDraggingSlider = false;
            self->isDraggingScrollbar = false;
            self->audio.SetVolume(self->cachedVolume);
            return 0;
        case WM_MOUSEWHEEL:
        {
            if (self->isDropdownOpen) {
				int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                self->scrollOffset -= (delta / (float)WHEEL_DELTA) * 35.0f; // 35=item height
                if (self->scrollOffset < 0) self->scrollOffset = 0;
				if (self->scrollOffset > self->maxScroll) self->scrollOffset = self->maxScroll;
                InvalidateRect(hwnd, NULL, FALSE);
				return 0;
            }
            break;
        }
        case WM_RAILING_AUDIO_UPDATE:
            if (self->ownerBar && self->ownerBar->renderer) {
                float vol = (float)wParam / 100.0f;
                bool mute = (bool)lParam;

                self->ownerBar->renderer->UpdateAudioStats(vol, mute);
                if (Railing::instance) {
                    Railing::instance->cachedVolume = vol;
                    Railing::instance->cachedMute = mute;
                }

                InvalidateRect(self->ownerBar->hwnd, NULL, FALSE);
            }
            return 0;
        case WM_ACTIVATE:
        case WM_KILLFOCUS:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (self->animState != AnimationState::Hidden && self->animState != AnimationState::Exiting) {
                    ULONGLONG now = GetTickCount64();
                    if (now - self->lastAutoCloseTime < 200) return 0;
                    self->animState = AnimationState::Exiting;
                    self->lastAnimTime = now;
                    self->lastAutoCloseTime = now;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        case WM_DESTROY:
            if (self->pRenderTarget) { self->pRenderTarget->Release(); self->pRenderTarget = nullptr; }
            if (self->pBgBrush) { self->pBgBrush->Release();      self->pBgBrush = nullptr; }
            if (self->pAccentBrush) { self->pAccentBrush->Release();  self->pAccentBrush = nullptr; }
            if (self->pFgBrush) { self->pFgBrush->Release();      self->pFgBrush = nullptr; }
            if (self->pBorderBrush) { self->pBorderBrush->Release();  self->pBorderBrush = nullptr; }
            self->hwnd = nullptr; // Mark as closed locally
            return 0;
        case WM_NCDESTROY:
            return 0;
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
void VolumeFlyout::Draw() {
    UpdateAnimation();
    if (animState == AnimationState::Hidden) return;

    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

    // Ensure Render Target exists (Keep your existing creation logic)
    if (!pRenderTarget) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        HRESULT hr = pFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &pRenderTarget);
        if (SUCCEEDED(hr) && pRenderTarget) {
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBgBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pFgBrush);
            D2D1_COLOR_F highlightCol = style.highlights;
            pRenderTarget->CreateSolidColorBrush(highlightCol, &pAccentBrush);
            pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBorderBrush);
        }
        else return;
    }
    else {
        D2D1_SIZE_U curSize = pRenderTarget->GetPixelSize();
        if (curSize.width != size.width || curSize.height != size.height) pRenderTarget->Resize(size);
    }

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (pBgBrush && pFgBrush && pAccentBrush && pBorderBrush && pTextFormat) {
        // FIX 1: Apply Scaling (Match NetworkFlyout)
        float dpi = (float)GetDpiForWindow(hwnd);
        float scale = dpi / 96.0f;
        pRenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));

        // FIX 2: Use Logical Width (300) so layout stays consistent
        float width = LOGICAL_WIDTH;
        float visualHeight = LOGICAL_FULL_HEIGHT;

        // Setup Text Format (Left Aligned by default)
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // --- Background ---
        D2D1_COLOR_F bgColor = style.background;
        bgColor.a *= currentAlpha;
        if (bgColor.a == 0.0f) bgColor = D2D1::ColorF(0.08f, 0.08f, 0.1f, 0.6f * currentAlpha);

        pBgBrush->SetColor(bgColor);
        D2D1_RECT_F bgRect = D2D1::RectF(0, 0, width, visualHeight); // Logical Size
        float r = style.radius;

        if (bgColor.a > 0.0f) pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(bgRect, r, r), pBgBrush);

        if (style.borderWidth > 0.0f) {
            D2D1_COLOR_F bColor = style.borderColor;
            if (bColor.a == 0.0f) bColor.a = 1.0f;
            bColor.a *= currentAlpha;
            pBorderBrush->SetColor(bColor);
            float inset = style.borderWidth / 2.0f;
            D2D1_RECT_F bRect = D2D1::RectF(inset, inset, width - inset, visualHeight - inset);
            pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(bRect, r, r), pBorderBrush, style.borderWidth);
        }

        // --- Content Layout ---
        float padding = 20.0f;
        float y = 20.0f;

        float vol = isDraggingSlider ? cachedVolume : audio.GetVolume();
        if (!isDraggingSlider) cachedVolume = vol;

        // Title
        pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1.0f * currentAlpha));
        pRenderTarget->DrawTextW(L"Volume", 6, pTextFormat, D2D1::RectF(padding, y, width, y + 20), pFgBrush);

        // Slider Track
        pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f * currentAlpha));
        // FIX 3: Fixed Slider Rect coordinates (Top was > Bottom in your code)
        sliderRect = D2D1::RectF(padding, y + 30, width - padding, y + 36);
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(sliderRect, 3, 3), pBgBrush);

        // Slider Fill
        D2D1_RECT_F fillRect = sliderRect;
        D2D1_COLOR_F highlightCol = style.highlights;
        highlightCol.a *= currentAlpha * 1.0f;
        fillRect.right = fillRect.left + (fillRect.right - fillRect.left) * vol;
        pAccentBrush->SetColor(highlightCol);
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 3, 3), pAccentBrush);

        // Slider Thumb
        pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRect.right, (sliderRect.top + sliderRect.bottom) / 2), 8, 8), pFgBrush);

        // Slider Tooltip (Dragging)
        if (isDraggingSlider || isHoveringSlider) {
            float thumbX = fillRect.right;
            D2D1_RECT_F tipRect = D2D1::RectF(thumbX - 19, sliderRect.top - 28, thumbX + 19, sliderRect.top - 6);
            pBgBrush->SetColor(D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f));
            pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tipRect, 4, 4), pBgBrush);

            pBorderBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.2f));
            pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(tipRect, 4, 4), pBorderBrush, 1.0f);

            wchar_t volText[8];
            swprintf_s(volText, L"%d%%", (int)(vol * 100));

            pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); // Center text in tooltip
            pFgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
            pRenderTarget->DrawTextW(volText, (UINT32)wcslen(volText), pTextFormat,
                D2D1::RectF(tipRect.left, tipRect.top - 1, tipRect.right, tipRect.bottom),
                pFgBrush);
            pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); // Restore
        }

        y += 60.0f; // Move down

        // Sound Settings
        pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1.0f * currentAlpha));
        pRenderTarget->DrawTextW(L"Sound Settings", 14, pTextFormat, D2D1::RectF(padding, y, width, y + 20), pFgBrush);

        // Open Button
        switchRect = D2D1::RectF(width - padding - 80, y, width - padding, y + 26);
        float btnAlpha = isHoveringOpenButton ? 0.8f : 0.5f;
        pBgBrush->SetColor(highlightCol);
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(switchRect, 4, 4), pBgBrush);

        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRenderTarget->DrawTextW(L"Open", 4, pTextFormat, D2D1::RectF(switchRect.left, switchRect.top + 1, switchRect.right, switchRect.bottom), pFgBrush);
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        y += 50.0f; // Move down

        // DEVICE DROPDOWN
        dropdownRect = D2D1::RectF(padding, y, width - padding, y + 30);
        pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.15f * currentAlpha));
        pRenderTarget->FillRectangle(dropdownRect, pBgBrush);

        std::wstring currentName = audio.GetCurrentDeviceName();
        // Limit text width to avoid overlapping arrow
        pRenderTarget->DrawTextW(currentName.c_str(), (UINT32)currentName.length(), pTextFormat, D2D1::RectF(padding + 10, y + 5, width - 40, y + 30), pFgBrush);

        // Arrow
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        pRenderTarget->DrawTextW(isDropdownOpen ? L"\u2B0E" : L"\u2B0F", 1, pTextFormat, D2D1::RectF(width - padding - 35, y + 5, width - padding - 10, y + 30), pFgBrush);
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        // Dropdown List
        if (isDropdownOpen) {
            y += 35.0f;
            deviceItemRects.clear();
            float availableHeight = LOGICAL_FULL_HEIGHT - y - 10.0f;

            // Adjust clip rect for scale logic
            D2D1_RECT_F clipRect = D2D1::RectF(0, y, width, y + availableHeight);

            float totalContentHeight = (float)devices.size() * 35.0f;
            maxScroll = max(0.0f, totalContentHeight - availableHeight);

            pRenderTarget->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

            float currentY = y - scrollOffset;

            for (const auto &dev : devices) {
                D2D1_RECT_F itemRect = D2D1::RectF(padding, currentY, width - padding, currentY + 30);
                deviceItemRects.push_back(itemRect);

                if (itemRect.bottom > clipRect.top && itemRect.top < clipRect.bottom) {
                    if (dev.name == currentName) {
                        D2D1_COLOR_F devCol = style.highlights;
                        devCol.a *= 0.4f;
                        pBgBrush->SetColor(devCol);
                    }
                    else
                        pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));

                    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 2, 2), pBgBrush);
                    pRenderTarget->DrawTextW(dev.name.c_str(), (UINT32)dev.name.length(), pTextFormat,
                        D2D1::RectF(padding + 10, currentY + 5, width - padding - 10, currentY + 30), pFgBrush);
                }
                currentY += 35.0f;
            }

            pRenderTarget->PopAxisAlignedClip();

            // Scrollbar
            if (maxScroll > 0) {
                float scrollTrackHeight = availableHeight;
                float scrollThumbHeight = max(20.0f, (availableHeight / totalContentHeight) * scrollTrackHeight);
                float scrollRatio = scrollOffset / maxScroll;
                float scrollThumbY = (y + 5.0f) + (scrollRatio * (scrollTrackHeight - scrollThumbHeight));

                scrollTrackRect = D2D1::RectF(width - padding, y + 5.0f, width - padding + 15.0f, y + 5.0f + scrollTrackHeight);
                scrollThumbRect = D2D1::RectF(width - padding + 5.0f, scrollThumbY, width - padding + 8.0f, scrollThumbY + scrollThumbHeight);

                POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
                // Adjust mouse hit test for scale
                float lx = pt.x / scale;
                float ly = pt.y / scale;

                bool isOverThumb = (lx >= scrollThumbRect.left - 5 && lx <= scrollThumbRect.right + 5 && ly >= scrollThumbRect.top && ly <= scrollThumbRect.bottom);
                float thumbAlpha = (isOverThumb || isDraggingScrollbar) ? 0.6f : 0.3f;

                pBgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, thumbAlpha));
                pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(scrollThumbRect, 1.5f, 1.5f), pBgBrush);
            }
        }
    }
    pRenderTarget->EndDraw();
}

void VolumeFlyout::OnClick(int x, int y)
{
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    float lx = x / scale;
    float ly = y / scale;
    D2D1_RECT_F hitRect = sliderRect;
    hitRect.left -= 10; hitRect.right += 10; hitRect.top -= 15; hitRect.bottom += 15;
    if (lx >= hitRect.left && lx <= hitRect.right && ly >= hitRect.top && ly <= hitRect.bottom) {
        isDraggingSlider = true;
        OnDrag(x, y);
        return;
    }
    else if (lx >= switchRect.left && lx <= switchRect.right && ly >= switchRect.top && ly <= switchRect.bottom) {
        audio.OpenSoundSettings();
        return;
    }
    else if (lx >= dropdownRect.left && lx <= dropdownRect.right && ly >= dropdownRect.top && ly <= dropdownRect.bottom) {
        isDropdownOpen = !isDropdownOpen;
        if (isDropdownOpen) {
            RefreshDevices();
            scrollOffset = 0.0f;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    if (isDropdownOpen && maxScroll > 0) {
        if (x >= scrollTrackRect.left - 5 && x <= scrollTrackRect.right + 5 &&
            y >= scrollTrackRect.top && y <= scrollTrackRect.bottom) {

            isDraggingScrollbar = true;
            dragStartY = (float)y;
            dragStartScrollOffset = scrollOffset;
            if (y < scrollThumbRect.top || y > scrollThumbRect.bottom) {
                float trackHeight = scrollTrackRect.bottom - scrollTrackRect.top;
                float thumbHeight = scrollThumbRect.bottom - scrollThumbRect.top;
                float clickPosRelative = (float)y - scrollTrackRect.top - (thumbHeight / 2.0f);
                scrollOffset = (clickPosRelative / (trackHeight - thumbHeight)) * maxScroll;
            }
            if (scrollOffset < 0) scrollOffset = 0;
            if (scrollOffset > maxScroll) scrollOffset = maxScroll;
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
    }
    if (isDropdownOpen) {
        float listTop = dropdownRect.bottom + 5.0f;
        RECT rc; GetClientRect(hwnd, &rc);
        float listBottom = LOGICAL_FULL_HEIGHT - 10.0f;
        for (size_t i = 0; i < deviceItemRects.size(); ++i) {
            D2D1_RECT_F r = deviceItemRects[i];
            if (lx >= r.left && lx <= r.right && ly >= r.top && ly <= r.bottom) {
                if (ly >= listTop && ly <= listBottom) {
                    audio.SetDefaultDevice(devices[i].id);
                    isDropdownOpen = false;
                    scrollOffset = 0.0f;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return;
                }
            }
        }
    }
}

void VolumeFlyout::OnDrag(int x, int y) {
    float dpi = (float)GetDpiForWindow(hwnd);
    float scale = dpi / 96.0f;
    float lx = x / scale;
    float ly = y / scale;

    if (isDraggingSlider) {
        float width = sliderRect.right - sliderRect.left;
        float localX = lx - sliderRect.left;
        if (localX < 0) localX = 0;
        if (localX > width) localX = width;

        float percent = localX / width;
        if (percent < 0.0f) percent = 0.0f;
        if (percent > 1.0f) percent = 1.0f;

        cachedVolume = percent;
        InvalidateRect(hwnd, NULL, FALSE);

        ULONGLONG now = GetTickCount64();
        if (now - lastAudioUpdate > 16) {
            audio.SetVolume(percent);
            lastAudioUpdate = now;
        }
    }
    else if (isDraggingScrollbar) {
        float trackHeight = scrollTrackRect.bottom - scrollTrackRect.top;
        float thumbHeight = scrollThumbRect.bottom - scrollThumbRect.top;
        float usableTrack = trackHeight - thumbHeight;

        if (usableTrack > 0) {
            float deltaY = (float)y - dragStartY; // Calculate how much scrollOffset changes per pixel of mouse movement
            float scrollPerPixel = maxScroll / usableTrack;
            scrollOffset = dragStartScrollOffset + (deltaY * scrollPerPixel);

            if (scrollOffset < 0) scrollOffset = 0;
            if (scrollOffset > maxScroll) scrollOffset = maxScroll;
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

void VolumeFlyout::RefreshDevices() {
    devices.clear();
    if (!audio.pEnumerator) return;

    IMMDeviceCollection *pCollection = nullptr;
    if (SUCCEEDED(audio.pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection))) {
        UINT count;
        pCollection->GetCount(&count);
        for (UINT i = 0; i < count; i++) {
            IMMDevice *pDevice = nullptr;
            pCollection->Item(i, &pDevice);
            if (pDevice) {
                LPWSTR id = nullptr;
                pDevice->GetId(&id);

                IPropertyStore *pProps = nullptr;
                if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
                    PROPVARIANT varName;
                    PropVariantInit(&varName);

                    pProps->GetValue(PKEY_Device_FriendlyName, &varName);

                    if (varName.pwszVal) {
                        devices.push_back({ varName.pwszVal, id });
                    }
                    PropVariantClear(&varName);
                    pProps->Release();
                }
                CoTaskMemFree(id);
                pDevice->Release();
            }
        }
        pCollection->Release();
    }
}

void VolumeFlyout::UpdateAnimation() {
    ULONGLONG now = GetTickCount64();
    float deltaTime = (float)(now - lastAnimTime) / 1000.0f;
    if (deltaTime == 0.0f) deltaTime = 0.016f;
    lastAnimTime = now;
    float animSpeed = 8.0f; // Faster speed for window movement
    bool needsMove = false;

    if (animState == AnimationState::Entering) {
        currentAlpha += deltaTime * animSpeed;
        if (currentAlpha >= 1.0f) {
            currentAlpha = 1.0f;
            animState = AnimationState::Visible;
        }

        currentOffset = (1.0f - currentAlpha) * 20.0f;
        needsMove = true;
    }
    else if (animState == AnimationState::Exiting) {
        currentAlpha -= deltaTime * animSpeed;
        if (currentAlpha <= 0.0f) {
            currentAlpha = 0.0f;
            animState = AnimationState::Hidden;
            ShowWindow(hwnd, SW_HIDE);
            isDropdownOpen = false;
            float scale = GetDpiForWindow(hwnd) / 96.0f;
            SetWindowPos(hwnd, NULL, 0, 0, (int)(300 * scale), (int)(180 * scale), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        currentOffset = (1.0f - currentAlpha) * 20.0f;
        needsMove = true;
    }
    if (needsMove && animState != AnimationState::Hidden) {
        SetWindowPos(hwnd, NULL,
            targetX,
            targetY + (int)currentOffset,
            0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        );
        InvalidateRect(hwnd, NULL, FALSE);
    }
}