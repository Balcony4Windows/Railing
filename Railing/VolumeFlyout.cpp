#include "VolumeFlyout.h"
#include <windowsx.h>
#include "RailingRenderer.h"
#include <dwmapi.h>
#include "Railing.h"
#include <functiondiscoverykeys.h>
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

ID2D1Factory *VolumeFlyout::pFactory = nullptr;
IDWriteFactory *VolumeFlyout::pWriteFactory = nullptr;
IDWriteTextFormat *VolumeFlyout::pTextFormat = nullptr;
ULONGLONG VolumeFlyout::lastAutoCloseTime = 0;
ULONGLONG VolumeFlyout::lastAnimTime = 0;

VolumeFlyout::VolumeFlyout(HINSTANCE hInst) {
    if (!pFactory) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory);
    if (!pWriteFactory) DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&pWriteFactory);

    if (!pTextFormat && pWriteFactory) {
        pWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &pTextFormat);
    }
    hwnd = nullptr;
}

VolumeFlyout::~VolumeFlyout() {
    if (pRenderTarget) { pRenderTarget->Release(); pRenderTarget = nullptr; }
    if (pBgBrush) { pBgBrush->Release(); pBgBrush = nullptr; }
    if (pFgBrush) { pFgBrush->Release(); pFgBrush = nullptr; }
    if (pAccentBrush) { pAccentBrush->Release(); pAccentBrush = nullptr; }
    hwnd = nullptr;
}

void VolumeFlyout::Toggle(int anchorX, int anchorY) {
    ULONGLONG now = GetTickCount64();
    if (now - lastAutoCloseTime < 200) return;

    // LAZY CREATION
    if (!hwnd) {
        WNDCLASS wc = { 0 };
        if (!GetClassInfo(GetModuleHandle(NULL), L"VolumeFlyoutClass", &wc)) {
            wc.lpfnWndProc = VolumeFlyout::WindowProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = L"VolumeFlyoutClass";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClass(&wc);
        }

        hwnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            L"VolumeFlyoutClass", L"VolumeFlyout",
            WS_POPUP,
            0, 0, 340, 180,
            nullptr, nullptr, GetModuleHandle(NULL), this
        );

        // Apply Round Corners & Blur
        int cornerPreference = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        RailingRenderer::EnableBlur(hwnd, 0x00000000);

        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }

    // ANIMATION LOGIC
    if (animState == AnimationState::Visible || animState == AnimationState::Entering) {
        animState = AnimationState::Exiting;
        lastAnimTime = now;
        InvalidateRect(hwnd, NULL, FALSE);
    }
    else {
        int width = 340, height = 180;
        targetX = anchorX - (width / 2);
        targetY = anchorY + 15;

        int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (targetY + height > screenH) targetY = anchorY - height - 15;

        animState = AnimationState::Entering;
        currentAlpha = 0.0f;
        currentOffset = 20.0f;

        lastAnimTime = GetTickCount64();
        SetWindowPos(hwnd, HWND_TOPMOST, targetX, targetY + (int)currentOffset, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        RefreshDevices();
    }
}

bool VolumeFlyout::IsVisible() {
    return IsWindowVisible(hwnd);
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

            if (wParam & MK_LBUTTON) {
                self->OnDrag(x, y);
            }

            // Hover Logic
            D2D1_RECT_F hitRect = self->sliderRect;
            hitRect.top -= 10; hitRect.bottom += 10;
            bool hovering = (x >= hitRect.left && x <= hitRect.right && y >= hitRect.top && y <= hitRect.bottom);

            if (hovering != self->isHoveringSlider) {
                self->isHoveringSlider = hovering;
                InvalidateRect(hwnd, NULL, FALSE);
            }
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

                bool showHand = false;
                if (pt.x >= self->sliderRect.left - 10 && pt.x <= self->sliderRect.right + 10 &&
                    pt.y >= self->sliderRect.top - 15 && pt.y <= self->sliderRect.bottom + 15) {
                    showHand = true;
                }
                else if (pt.x >= self->switchRect.left && pt.x <= self->switchRect.right &&
                    pt.y >= self->switchRect.top && pt.y <= self->switchRect.bottom) {
                    showHand = true;
                }
                else if (pt.x >= self->dropdownRect.left && pt.x <= self->dropdownRect.right &&
                    pt.y >= self->dropdownRect.top && pt.y <= self->dropdownRect.bottom) {
                    showHand = true;
                }
                else if (self->isDropdownOpen) {
                    for (const auto &r : self->deviceItemRects) {
                        if (pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom) {
                            showHand = true;
                            break;
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
            self->audio.SetVolume(self->cachedVolume);
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (self->animState != AnimationState::Hidden && self->animState != AnimationState::Exiting) {
                    self->animState = AnimationState::Exiting;
                    self->lastAnimTime = GetTickCount64();
                    self->lastAutoCloseTime = GetTickCount64();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_KILLFOCUS:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (self->pRenderTarget) { self->pRenderTarget->Release(); self->pRenderTarget = nullptr; }
            if (self->pBgBrush) { self->pBgBrush->Release(); self->pBgBrush = nullptr; }
            if (self->pAccentBrush) { self->pAccentBrush->Release(); self->pAccentBrush = nullptr; }
            if (self->pFgBrush) { self->pFgBrush->Release(); self->pFgBrush = nullptr; }

            self->hwnd = nullptr;
            // Unlink from Main App
            if (Railing::instance && Railing::instance->flyout == self) {
                Railing::instance->flyout = nullptr;
            }
            return 0;

        case WM_NCDESTROY:
            // FINAL CLEANUP: Delete the C++ Object
            delete self;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
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

    if (!pRenderTarget) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        pFactory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, size), &pRenderTarget);
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.4f), &pBgBrush);
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pFgBrush);
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::CornflowerBlue), &pAccentBrush);
    }
    else {
        D2D1_SIZE_F currentSize = pRenderTarget->GetSize();
        if (currentSize.width != size.width || currentSize.height != size.height) {
            pRenderTarget->Resize(size);
        }
    }
    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0));

    pRenderTarget->SetTransform(D2D1::Matrix3x2F::Translation(0, currentOffset));
    pBgBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.4f * currentAlpha));
    pFgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f * currentAlpha));
    pAccentBrush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue, 1.0f * currentAlpha));


    float width = pRenderTarget->GetSize().width;
    float height = pRenderTarget->GetSize().height; // Get dynamic height
    float padding = 20.0f;
    float y = 20.0f;

    float vol = 0.0f;
    if (isDraggingSlider) {
        vol = cachedVolume;
    }
    else {
        vol = audio.GetVolume();
        cachedVolume = vol;
    }
    pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.1f));
    sliderRect = D2D1::RectF(padding, y + 30, width - padding, y + 26);
    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(sliderRect, 3, 3), pBgBrush);

    D2D1_RECT_F fillRect = sliderRect;
    fillRect.right = fillRect.left + (fillRect.right - fillRect.left) * vol;
    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 3, 3), pAccentBrush);

    pRenderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillRect.right, (sliderRect.top + sliderRect.bottom) / 2), 8, 8), pFgBrush);

    if (isDraggingSlider || isHoveringSlider) {
        float thumbX = fillRect.right;
        float tipWidth = 38.0f;
        float tipHeight = 22.0f;

        D2D1_RECT_F tipRect = D2D1::RectF(
            thumbX - (tipWidth / 2),
            sliderRect.top - 28.0f,
            thumbX + (tipWidth / 2),
            sliderRect.top - 6.0f
        );
        pBgBrush->SetColor(D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f));
        pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(tipRect, 4, 4), pBgBrush);
        ID2D1SolidColorBrush *pBorder = nullptr;
        pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f), &pBorder);
        if (pBorder) {
            pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(tipRect, 4, 4), pBorder, 1.0f);
            pBorder->Release();
        }
        wchar_t volText[8];
        swprintf_s(volText, L"%d%%", (int)(vol * 100));

        pFgBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
        pRenderTarget->DrawTextW(volText, (UINT32)wcslen(volText), pTextFormat,
            D2D1::RectF(tipRect.left, tipRect.top - 1, tipRect.right, tipRect.bottom),
            pFgBrush);
    }

    pRenderTarget->DrawTextW(L"Volume", 6, pTextFormat, D2D1::RectF(padding, y, width, y + 20), pFgBrush);

    y += 60.0f;

    switchRect = D2D1::RectF(width - padding - 40, y, width - padding, y + 20);
    pRenderTarget->DrawTextW(L"Sound Settings", 14, pTextFormat, D2D1::RectF(padding, y, width, y + 20), pFgBrush);
    switchRect = D2D1::RectF(width - padding - 80, y, width - padding, y + 26);

    pBgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue, 0.5f));
    pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(switchRect, 4, 4), pBgBrush);
    D2D1_RECT_F btnTextRect = switchRect;
    btnTextRect.top += 3;
    pRenderTarget->DrawTextW(L"Open", 4, pTextFormat, D2D1::RectF(switchRect.left + 22, switchRect.top + 3, switchRect.right, switchRect.bottom), pFgBrush);
    
    y += 50.0f;

    // DEVICE DROPDOWN
    dropdownRect = D2D1::RectF(padding, y, width - padding, y + 30);
    pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.15f));
    pRenderTarget->FillRectangle(dropdownRect, pBgBrush);
    std::wstring currentName = audio.GetCurrentDeviceName();
    pRenderTarget->DrawTextW(currentName.c_str(), (UINT32)currentName.length(), pTextFormat,
        D2D1::RectF(padding + 10, y + 5, width - 40, y + 30), pFgBrush);

    // Draw arrow
    pRenderTarget->DrawTextW(isDropdownOpen ? L"\u2B0E" : L"\u2B0F", 1, pTextFormat,
        D2D1::RectF(width - padding - 25, y + 5, width, y + 30), pFgBrush);

    if (isDropdownOpen) {
        y += 35.0f;
        deviceItemRects.clear();

        for (const auto &dev : devices) {
            D2D1_RECT_F itemRect = D2D1::RectF(padding, y, width - padding, y + 30);
            deviceItemRects.push_back(itemRect);

            if (dev.name == currentName) pBgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::CornflowerBlue, 0.4f));
            else pBgBrush->SetColor(D2D1::ColorF(1, 1, 1, 0.05f));

            pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(itemRect, 2, 2), pBgBrush);
            pRenderTarget->DrawTextW(dev.name.c_str(), (UINT32)dev.name.length(), pTextFormat,
                D2D1::RectF(padding + 10, y + 5, width, y + 30), pFgBrush);

            y += 35.0f;
        }
    }

    ID2D1SolidColorBrush *border = nullptr;
    pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.1f), &border);
    if (border) {
        pRenderTarget->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f), 8.0f, 8.0f), border);
        border->Release();
    }
    pRenderTarget->EndDraw();
}

void VolumeFlyout::OnClick(int x, int y) {
    // Slider
    D2D1_RECT_F hitRect = sliderRect; hitRect.left -= 10; hitRect.right += 10; hitRect.top -= 15; hitRect.bottom += 15;
    if (x >= hitRect.left && x <= hitRect.right && y >= hitRect.top && y <= hitRect.bottom) {
        isDraggingSlider = true;
        OnDrag(x, y);
        return;
    }

    // Settings Button
    if (x >= switchRect.left && x <= switchRect.right && y >= switchRect.top && y <= switchRect.bottom) {
        audio.OpenSoundSettings();
        return;
    }

    // Dropdown Header
    if (x >= dropdownRect.left && x <= dropdownRect.right && y >= dropdownRect.top && y <= dropdownRect.bottom) {
        isDropdownOpen = !isDropdownOpen;
        if (isDropdownOpen) {
            RefreshDevices();
            SetWindowPos(hwnd, NULL, 0, 0, 340, 180 + (int)(devices.size() * 35), SWP_NOMOVE | SWP_NOZORDER);
        }
        else {
            SetWindowPos(hwnd, NULL, 0, 0, 340, 180, SWP_NOMOVE | SWP_NOZORDER);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (isDropdownOpen) {
        for (size_t i = 0; i < deviceItemRects.size(); ++i) {
            D2D1_RECT_F r = deviceItemRects[i];
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                audio.SetDefaultDevice(devices[i].id);
                isDropdownOpen = false;
                SetWindowPos(hwnd, NULL, 0, 0, 340, 180, SWP_NOMOVE | SWP_NOZORDER);
                InvalidateRect(hwnd, NULL, FALSE);
                break;
            }
        }
    }
}

void VolumeFlyout::OnDrag(int x, int y) {
    if (isDraggingSlider) {
        float width = sliderRect.right - sliderRect.left;
        float localX = (float)x - sliderRect.left;
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
            SetWindowPos(hwnd, NULL, 0, 0, 340, 180, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        currentOffset = (1.0f - currentAlpha) * 20.0f;
        needsMove = true;
    }

    // APPLY PHYSICAL MOVE
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