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

const float LOGICAL_WIDTH = 280.0f;
const float LOGICAL_FULL_HEIGHT = 300.0f;

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
        wc.hbrBackground = nullptr;
        RegisterClass(&wc);
    }
}

VolumeFlyout::~VolumeFlyout() {
    FlyoutManager::Get().Unregister(this);
    if (mmTimerId) {
        timeKillEvent(mmTimerId);
        mmTimerId = 0;
    }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }

    // Release Device Dependent Resources
    if (pRenderTarget) { pRenderTarget->Release(); pRenderTarget = nullptr; }
    if (pBgBrush) { pBgBrush->Release(); pBgBrush = nullptr; }
    if (pFgBrush) { pFgBrush->Release(); pFgBrush = nullptr; }
    if (pAccentBrush) { pAccentBrush->Release(); pAccentBrush = nullptr; }
    if (pBorderBrush) { pBorderBrush->Release(); pBorderBrush = nullptr; }
}

void VolumeFlyout::CreateDeviceResources() {
    if (!hwnd) return;

    if (!pRenderTarget) {
        RECT rc; GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU((std::max)(1L, rc.right - rc.left), (std::max)(1L, rc.bottom - rc.top));

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            0, 0,
            D2D1_RENDER_TARGET_USAGE_NONE,
            D2D1_FEATURE_LEVEL_DEFAULT
        );

        pFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &pRenderTarget);
    }

    if (pRenderTarget) {
        // Create Brushes if they don't exist
        if (!pBgBrush) pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBgBrush);
        if (!pFgBrush) pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &pFgBrush);

        if (!pAccentBrush) {
            D2D1_COLOR_F highlightCol = style.highlights;
            pRenderTarget->CreateSolidColorBrush(highlightCol, &pAccentBrush);
        }
        if (!pBorderBrush) pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &pBorderBrush);
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
            WS_POPUP, 0, 0, 300, 180,
            nullptr, nullptr, hInst, this
        );
        // Don't apply DWM effects during creation
    }

    if (IsVisible()) {
        Hide();
    }
    else {
        FlyoutManager::Get().CloseOthers(this);
        audio.EnsureInitialized(hwnd);
        RefreshDevices();
        PositionWindow(iconRect);

        animState = AnimationState::Visible;

        float dpi = (float)GetDpiForWindow(hwnd);
        float scale = dpi / 96.0f;
        int w = (int)(LOGICAL_WIDTH * scale);
        int h = (int)(LOGICAL_FULL_HEIGHT * scale);

        CreateDeviceResources();

        SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY, w, h, SWP_NOACTIVATE);

        fadeAlpha = 0.0f;
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        // Start fade-in with multimedia timer
        mmTimerId = timeSetEvent(10, 1, [](UINT, UINT, DWORD_PTR dwUser, DWORD_PTR, DWORD_PTR) {
            VolumeFlyout *self = (VolumeFlyout *)dwUser;
            self->fadeAlpha += 0.08f;
            if (self->fadeAlpha >= 1.0f) {
                self->fadeAlpha = 1.0f;
                if (self->mmTimerId) {
                    timeKillEvent(self->mmTimerId);
                    self->mmTimerId = 0;
                }
            }
            SetLayeredWindowAttributes(self->hwnd, 0, (BYTE)(self->fadeAlpha * 255.0f), LWA_ALPHA);
            InvalidateRect(self->hwnd, NULL, FALSE);
            }, (DWORD_PTR)this, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);

        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        SetFocus(hwnd);

        // Apply DWM effects after showing
        static bool dwmSetupDone = false;
        if (!dwmSetupDone) {
            dwmSetupDone = true;

            if (style.blur) RailingRenderer::EnableBlur(hwnd, 0x00000000);
            else {
                MARGINS margins = { -1 };
                DwmExtendFrameIntoClientArea(hwnd, &margins);
            }

            DWM_WINDOW_CORNER_PREFERENCE preference = (style.radius > 0.0f) ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        }
    }
}

void VolumeFlyout::Hide()
{
    if (animState == AnimationState::Visible) {
        // Kill timer and hide instantly
        if (mmTimerId) {
            timeKillEvent(mmTimerId);
            mmTimerId = 0;
        }

        ReleaseCapture();
        animState = AnimationState::Hidden;
        ShowWindow(hwnd, SW_HIDE);
        fadeAlpha = 0.0f;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        lastAutoCloseTime = now.QuadPart;
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
        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;
        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            
            SetCapture(hwnd);
            self->OnClick(pt.x, pt.y);
            return 0;
        }
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
        case WM_KILLFOCUS: {
            if (!self->isDraggingSlider && !self->isDraggingScrollbar) {
                self->Hide();
            }
            return 0;
        }
        case WM_ACTIVATE: {
            WORD state = LOWORD(wParam);

            if (state == WA_INACTIVE) {
                // Don't hide if we're dragging
                if (self->isDraggingSlider || self->isDraggingScrollbar) {
                    return 0;
                }

                if (self->animState == AnimationState::Visible) self->Hide();
                return 0;
            }
            break;
        }
        case WM_DESTROY: {
            if (self->pRenderTarget) { self->pRenderTarget->Release(); self->pRenderTarget = nullptr; }
            if (self->pBgBrush) { self->pBgBrush->Release();      self->pBgBrush = nullptr; }
            if (self->pAccentBrush) { self->pAccentBrush->Release();  self->pAccentBrush = nullptr; }
            if (self->pFgBrush) { self->pFgBrush->Release();      self->pFgBrush = nullptr; }
            if (self->pBorderBrush) { self->pBorderBrush->Release();  self->pBorderBrush = nullptr; }
            self->hwnd = nullptr; // Mark as closed locally
            return 0;
        }
        case WM_NCDESTROY:
            return 0;
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void VolumeFlyout::Draw() {
    CreateDeviceResources();
    if (!pRenderTarget) return;
    if (animState == AnimationState::Hidden) return;

    RECT rc; GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
    if (pRenderTarget->GetPixelSize().width != size.width) pRenderTarget->Resize(size);

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    if (pBgBrush && pFgBrush) {
        // Use Constants
        float width = LOGICAL_WIDTH;
        float visualHeight = LOGICAL_FULL_HEIGHT;

        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // --- Background ---
        D2D1_COLOR_F bgColor = style.background;
        if (bgColor.a == 0.0f) bgColor = D2D1::ColorF(0.08f, 0.08f, 0.1f, 0.6f);
        if (style.blur && bgColor.a > 0.6f) bgColor.a = 0.6f;
        pBgBrush->SetColor(bgColor);

        D2D1_RECT_F bgRect = D2D1::RectF(0, 0, width, visualHeight);
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(bgRect, style.radius, style.radius), pBgBrush);

        if (style.borderWidth > 0.0f) {
            D2D1_COLOR_F bColor = style.borderColor;
            bColor.a *= fadeAlpha;
            pBorderBrush->SetColor(bColor);
            float inset = style.borderWidth / 2.0f;
            D2D1_RECT_F bRect = D2D1::RectF(inset, inset, width - inset, visualHeight - inset);
            pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(bRect, style.radius, style.radius), pBorderBrush, style.borderWidth);
        }

        // --- CONTENT (COMPACT SPACING) ---
        float padding = 16.0f;
        float y = 16.0f;
        float vol = isDraggingSlider ? cachedVolume : audio.GetVolume();
        if (!isDraggingSlider) cachedVolume = vol;

        // Label
        pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
        pRenderTarget->DrawTextW(L"Volume", 6, pTextFormat, D2D1::RectF(padding, y, width, y + 20), pFgBrush);

        // Slider (Thinner: 4px)
        float sliderH = 4.0f;
        float sliderY = y + 24.0f;

        pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f));
        sliderRect = D2D1::RectF(padding, sliderY, width - padding, sliderY + sliderH);
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(sliderRect, 2, 2), pBgBrush);

        D2D1_RECT_F fillRect = sliderRect;
        D2D1_COLOR_F highlightCol = style.highlights;
        highlightCol.a = 1.0f;
        fillRect.right = fillRect.left + (fillRect.right - fillRect.left) * vol;
        pAccentBrush->SetColor(highlightCol);
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 2, 2), pAccentBrush);

        // Thumb (Smaller: 6px radius)
        pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRect.right, (sliderRect.top + sliderRect.bottom) / 2), 6, 6), pFgBrush);

        // Tooltip
        if (isDraggingSlider || isHoveringSlider) {
            float thumbX = fillRect.right;
            D2D1_RECT_F tipRect = D2D1::RectF(thumbX - 16, sliderRect.top - 24, thumbX + 16, sliderRect.top - 4);
            pBgBrush->SetColor(D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f));
            pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tipRect, 3, 3), pBgBrush);
            pBorderBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.2f));
            pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(tipRect, 3, 3), pBorderBrush, 1.0f);

            wchar_t volText[8];
            swprintf_s(volText, L"%d%%", (int)(vol * 100));
            pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pFgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
            pRenderTarget->DrawTextW(volText, (UINT32)wcslen(volText), pTextFormat, tipRect, pFgBrush);
            pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        y += 45.0f; // Gap reduced

        // Settings Label
        pFgBrush->SetColor(D2D1::ColorF(1, 1, 1, 1));
        pRenderTarget->DrawTextW(L"Sound Settings", 14, pTextFormat, D2D1::RectF(padding, y, width, y + 20), pFgBrush);

        // Smaller Button (22px high)
        switchRect = D2D1::RectF(width - padding - 60, y - 1, width - padding, y + 21);
        pBgBrush->SetColor(highlightCol);
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(switchRect, 3, 3), pBgBrush);

        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pRenderTarget->DrawTextW(L"Open", 4, pTextFormat, D2D1::RectF(switchRect.left, switchRect.top + 2, switchRect.right, switchRect.bottom), pFgBrush);
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        y += 40.0f; // Gap reduced

        // Device Dropdown (30px high)
        float dropdownH = 30.0f;
        dropdownRect = D2D1::RectF(padding, y, width - padding, y + dropdownH);
        pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.15f));
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(dropdownRect, 4, 4), pBgBrush);

        std::wstring currentName = audio.GetCurrentDeviceName();
        pRenderTarget->PushAxisAlignedClip(D2D1::RectF(padding, y, width - 30, y + dropdownH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        D2D1_RECT_F textR = D2D1::RectF(padding + 10, y + 5, width - 40, y + dropdownH);
        pRenderTarget->DrawTextW(currentName.c_str(), (UINT32)currentName.length(), pTextFormat, textR, pFgBrush);
        pRenderTarget->PopAxisAlignedClip();

        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        pRenderTarget->DrawTextW(isDropdownOpen ? L"\u25B2" : L"\u25BC", 1, pTextFormat,
            D2D1::RectF(width - padding - 30, y + 5, width - padding - 10, y + dropdownH), pFgBrush);
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

        // List
        if (isDropdownOpen) {
            y += dropdownH + 4.0f;
            deviceItemRects.clear();
            float availableHeight = LOGICAL_FULL_HEIGHT - y - 10.0f;
            float itemH = 30.0f;

            D2D1_RECT_F clipRect = D2D1::RectF(0, y, width, y + availableHeight);
            float totalContentHeight = (float)devices.size() * itemH;
            maxScroll = (std::max)(0.0f, totalContentHeight - availableHeight);

            pRenderTarget->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            float currentY = y - scrollOffset;

            for (const auto &dev : devices) {
                D2D1_RECT_F itemRect = D2D1::RectF(padding, currentY, width - padding, currentY + itemH - 2);
                deviceItemRects.push_back(itemRect);

                if (itemRect.bottom > clipRect.top && itemRect.top < clipRect.bottom) {
                    if (dev.name == currentName) {
                        D2D1_COLOR_F devCol = style.highlights;
                        devCol.a *= 0.4f;
                        pBgBrush->SetColor(devCol);
                        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 3, 3), pBgBrush);
                    }
                    else {
                        pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));
                        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 3, 3), pBgBrush);
                    }
                    pRenderTarget->DrawTextW(dev.name.c_str(), (UINT32)dev.name.length(), pTextFormat,
                        D2D1::RectF(itemRect.left + 8, itemRect.top + 5, itemRect.right, itemRect.bottom), pFgBrush);
                }
                currentY += itemH;
            }
            pRenderTarget->PopAxisAlignedClip();

            if (maxScroll > 0) {
                float trackH = availableHeight;
                float thumbH = (std::max)(20.0f, (availableHeight / totalContentHeight) * trackH);
                float thumbY = y + (scrollOffset / maxScroll) * (trackH - thumbH);

                scrollTrackRect = D2D1::RectF(width - 14, y, width - 4, y + trackH);
                scrollThumbRect = D2D1::RectF(width - 12, thumbY, width - 6, thumbY + thumbH);

                pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.3f));
                pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(scrollThumbRect, 2, 2), pBgBrush);
            }
        }
    }
    pRenderTarget->EndDraw();
}

void VolumeFlyout::OnClick(int x, int y)
{
    // x, y are already in physical pixels, rects are in logical pixels
    // So we need to convert x,y to logical space
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
        // Convert physical coords to logical for track rect comparison
        if (lx >= scrollTrackRect.left - 5 && lx <= scrollTrackRect.right + 5 &&
            ly >= scrollTrackRect.top && ly <= scrollTrackRect.bottom) {

            isDraggingScrollbar = true;
            dragStartY = ly;  // Store logical Y
            dragStartScrollOffset = scrollOffset;

            if (ly < scrollThumbRect.top || ly > scrollThumbRect.bottom) {
                float trackHeight = scrollTrackRect.bottom - scrollTrackRect.top;
                float thumbHeight = scrollThumbRect.bottom - scrollThumbRect.top;
                float clickPosRelative = ly - scrollTrackRect.top - (thumbHeight / 2.0f);
                scrollOffset = (clickPosRelative / (trackHeight - thumbHeight)) * maxScroll;
            }
            if (scrollOffset < 0) scrollOffset = 0;
            if (scrollOffset > maxScroll) scrollOffset = maxScroll;
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
    }

    if (isDropdownOpen) {
        for (size_t i = 0; i < deviceItemRects.size(); ++i) {
            D2D1_RECT_F r = deviceItemRects[i];
            if (lx >= r.left && lx <= r.right && ly >= r.top && ly <= r.bottom) {
                audio.SetDefaultDevice(devices[i].id);
                isDropdownOpen = false;
                scrollOffset = 0.0f;
                InvalidateRect(hwnd, NULL, FALSE);
                return;
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
            float deltaY = ly - dragStartY;  // Both in logical space now
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