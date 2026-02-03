#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <algorithm>
#include <Windows.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

struct DockPreviewItem {
    HWND hwnd;
    std::wstring title;
};

struct DockPreviewColors {
    D2D1_COLOR_F bg = { 0.12f, 0.12f, 0.12f, 1.0f };
    D2D1_COLOR_F text = { 1.0f, 1.0f, 1.0f, 1.0f };
    D2D1_COLOR_F border = { 1.0f, 1.0f, 1.0f, 0.2f };
    D2D1_COLOR_F hover = { 1.0f, 1.0f, 1.0f, 0.1f };

    bool operator==(const DockPreviewColors &other) const {
        return bg.r == other.bg.r && bg.g == other.bg.g && bg.b == other.bg.b && bg.a == other.bg.a &&
            text.r == other.text.r && border.r == other.border.r;
    }
};

class DockPreviewWindow {
private:
    HWND m_hwnd;
    HWND m_hParent;
    ID2D1Factory *m_pFactory;
    ID2D1HwndRenderTarget *m_pRenderTarget;
    ID2D1SolidColorBrush *m_pBrush;
    IDWriteTextFormat *m_pLocalTextFormat;
    IWICImagingFactory *m_pWicFactory = nullptr;
    HICON m_hIcon = NULL;
    ID2D1Bitmap *m_pIconBitmap = nullptr;
    IDWriteFactory *m_pDWriteFactory = nullptr;

    struct PreviewData {
        std::wstring title;
        std::vector<DockPreviewItem> windows;
        int selectedIndex;
    } m_data;

    DockPreviewColors m_colors;
    bool m_isVisible;
    bool m_isTrackingMouse;
    float m_scale = 1.0f;
    ULONGLONG m_lastUpdateTime = 0;

    RECT m_safeIconRect = {};
    RECT m_lastTargetRect = {};
    const UINT_PTR MONITOR_TIMER_ID = 1001;

    struct LayoutMetrics {
        float headerHeight; float rowHeight; float paddingY;
        float iconSize; float iconMargin; float cornerRadius;
        float rowSideMargin; float separatorY; float closeBtnSize;
    };

    LayoutMetrics GetMetrics() const {
        return {
            34.0f * m_scale, 28.0f * m_scale, 4.0f * m_scale,
            20.0f * m_scale, 8.0f * m_scale, 8.0f * m_scale,
            4.0f * m_scale, 34.0f * m_scale, 20.0f * m_scale
        };
    }

    const int MAX_ITEMS_SHOWN = 12;

    void CreateDeviceResources() {
        if (!m_pRenderTarget && m_hwnd) {
            RECT rc; GetClientRect(m_hwnd, &rc);
            D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
            m_pFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(m_hwnd, size), &m_pRenderTarget);
            if (m_pRenderTarget) m_pRenderTarget->SetDpi(96.0f, 96.0f);
            m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_pBrush);
        }
    }

    void UpdateTextFormat() {
        if (m_pLocalTextFormat) { m_pLocalTextFormat->Release(); m_pLocalTextFormat = nullptr; }
        if (m_pDWriteFactory) {
            float fontSize = 12.0f * m_scale;
            m_pDWriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", &m_pLocalTextFormat);
            if (m_pLocalTextFormat) {
                m_pLocalTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                m_pLocalTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                m_pLocalTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        }
    }

    void CreateIconBitmap() {
        if (!m_pRenderTarget || !m_hIcon || !m_pWicFactory) return;
        if (m_pIconBitmap) return;
        IWICBitmap *wicBitmap = nullptr;
        if (SUCCEEDED(m_pWicFactory->CreateBitmapFromHICON(m_hIcon, &wicBitmap))) {
            IWICFormatConverter *converter = nullptr;
            m_pWicFactory->CreateFormatConverter(&converter);
            if (converter) {
                converter->Initialize(wicBitmap, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut);
                m_pRenderTarget->CreateBitmapFromWicBitmap(converter, nullptr, &m_pIconBitmap);
                converter->Release();
            }
            wicBitmap->Release();
        }
    }

    void DiscardDeviceResources() {
        if (m_pRenderTarget) { m_pRenderTarget->Release(); m_pRenderTarget = nullptr; }
        if (m_pBrush) { m_pBrush->Release(); m_pBrush = nullptr; }
        if (m_pIconBitmap) { m_pIconBitmap->Release(); m_pIconBitmap = nullptr; }
    }

    void Render() {
        if (!m_pRenderTarget) CreateDeviceResources();
        if (!m_pRenderTarget) return;
        RECT rc; GetClientRect(m_hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
        if (size.width == 0 || size.height == 0) return;

        m_pRenderTarget->Resize(size);
        m_pRenderTarget->BeginDraw();
        m_pRenderTarget->Clear(m_colors.bg);

        if (!m_pIconBitmap && m_hIcon) CreateIconBitmap();

        float w = (float)size.width;
        float h = (float)size.height;
        LayoutMetrics lm = GetMetrics();

        D2D1_ROUNDED_RECT bg = D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, w - 0.5f, h - 0.5f), lm.cornerRadius, lm.cornerRadius);
        m_pBrush->SetColor(m_colors.bg);
        m_pRenderTarget->FillRoundedRectangle(bg, m_pBrush);
        m_pBrush->SetColor(m_colors.border);
        m_pRenderTarget->DrawRoundedRectangle(bg, m_pBrush, 1.0f);

        float iconY = (lm.headerHeight - lm.iconSize) / 2.0f;
        if (m_pIconBitmap) {
            m_pRenderTarget->DrawBitmap(m_pIconBitmap, D2D1::RectF(lm.iconMargin, iconY, lm.iconMargin + lm.iconSize, iconY + lm.iconSize));
        }

        if (m_pLocalTextFormat) {
            m_pBrush->SetColor(m_colors.text);
            float textX = lm.iconMargin + lm.iconSize + lm.iconMargin;
            m_pRenderTarget->PushAxisAlignedClip(D2D1::RectF(textX, 0, w - lm.iconMargin, lm.headerHeight), D2D1_ANTIALIAS_MODE_ALIASED);
            m_pRenderTarget->DrawTextW(m_data.title.c_str(), (UINT32)m_data.title.length(), m_pLocalTextFormat, D2D1::RectF(textX, 0, w - lm.iconMargin, lm.headerHeight), m_pBrush);
            m_pRenderTarget->PopAxisAlignedClip();
        }

        m_pBrush->SetColor(m_colors.border);
        float lineY = floor(lm.separatorY) + 0.5f;
        m_pRenderTarget->DrawLine(D2D1::Point2F(0, lineY), D2D1::Point2F(w, lineY), m_pBrush, 1.0f);

        float currentY = lm.headerHeight + lm.paddingY;
        for (size_t i = 0; i < m_data.windows.size(); i++) {
            if (i >= MAX_ITEMS_SHOWN) break;
            float rightEdge = w - lm.rowSideMargin;
            D2D1_RECT_F rowRect = D2D1::RectF(lm.rowSideMargin, currentY, rightEdge, currentY + lm.rowHeight);

            if (m_data.selectedIndex == (int)i || m_data.selectedIndex == (int)(1000+i)) {
                float btnSize = 12.0f * m_scale; // scale of X symbol
				float margin = (lm.rowHeight - btnSize) / 2.0f;
                D2D1_RECT_F btnRect = D2D1::RectF(
                    rowRect.right - lm.closeBtnSize, rowRect.top,
                    rowRect.right, rowRect.bottom);

                if (m_data.selectedIndex == (int)(1000 + i)) {
                    m_pBrush->SetColor(D2D1::ColorF(0.8f, 0.2f, 0.2f, 0.4f));
					m_pRenderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(btnRect, 4.0f, 4.0f), m_pBrush);
                }
                m_pBrush->SetColor(m_colors.text);
                float centerX = btnRect.left + (lm.closeBtnSize / 2.0f);
                float centerY = btnRect.top + (lm.rowHeight / 2.0f);
                float offset = 4.0f * m_scale;

                m_pRenderTarget->DrawLine(
                    D2D1::Point2F(centerX - offset, centerY - offset),
                    D2D1::Point2F(centerX + offset, centerY + offset),
                    m_pBrush, 1.5f);
                m_pRenderTarget->DrawLine(
                    D2D1::Point2F(centerX + offset, centerY - offset),
                    D2D1::Point2F(centerX - offset, centerY + offset),
                    m_pBrush, 1.5f);
            }

            if (m_pLocalTextFormat) {
                m_pBrush->SetColor(m_colors.text);
                std::wstring t = m_data.windows[i].title;
                if (t.empty()) t = L"Untitled";
                float rowTextX = rowRect.left + lm.iconMargin;
                m_pRenderTarget->PushAxisAlignedClip(rowRect, D2D1_ANTIALIAS_MODE_ALIASED);
                m_pRenderTarget->DrawTextW(t.c_str(), (UINT32)t.length(), m_pLocalTextFormat, D2D1::RectF(rowTextX, rowRect.top, rowRect.right - lm.iconMargin, rowRect.bottom), m_pBrush);
                m_pRenderTarget->PopAxisAlignedClip();
            }
            currentY += lm.rowHeight;
        }
        if (m_pRenderTarget->EndDraw() == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
    }

    int HitTest(int screenX, int screenY) {
        POINT pt = { screenX, screenY };
        ScreenToClient(m_hwnd, &pt);
        LayoutMetrics lm = GetMetrics();
        RECT rc; GetClientRect(m_hwnd, &rc);

        float y = (float)pt.y;
        float listStartY = lm.headerHeight + lm.paddingY;
        if (y < listStartY) return -1;

        int idx = (int)((y - listStartY) / lm.rowHeight);
        if (idx >= 0 && idx < (int)m_data.windows.size() && idx < MAX_ITEMS_SHOWN) {
            float rowTop = listStartY + (idx * lm.rowHeight);
            if (y >= rowTop && y < (rowTop + lm.rowHeight)) {
                float btnLeft = (rc.right - rc.left) - lm.rowSideMargin - lm.closeBtnSize;
                if (pt.x >= btnLeft) return 1000 + idx;
                return idx;
            }
        }
        return -1;
    }

    bool CheckMouseInSafeZone() const {
        if (!m_isVisible && m_safeIconRect.right == 0) return false;

        POINT pt; GetCursorPos(&pt);
        RECT rcWin; GetWindowRect(m_hwnd, &rcWin);

        RECT rcUnion;
        UnionRect(&rcUnion, &rcWin, &m_safeIconRect);
        InflateRect(&rcUnion, 10, 10);

        return PtInRect(&rcUnion, pt);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        DockPreviewWindow *pThis = (DockPreviewWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        if (msg == WM_TIMER && wParam == pThis->MONITOR_TIMER_ID && pThis) {
            if (GetKeyState(VK_LBUTTON) & 0x8000) {
                if (!pThis->CheckMouseInSafeZone()) {
                    pThis->Hide();
                    if (pThis->m_hParent) InvalidateRect(pThis->m_hParent, NULL, FALSE);
                    return 0;
                }
            }

            ULONGLONG now = GetTickCount64();
            if ((now - pThis->m_lastUpdateTime) < 200) {
                return 0; // Recent update, trust the parent
            }

            if (!pThis->CheckMouseInSafeZone()) {
                pThis->Hide();
                if (pThis->m_hParent) InvalidateRect(pThis->m_hParent, NULL, FALSE);
            }
            return 0;
        }

        if (msg == WM_MOUSEMOVE && pThis) {
            if (!pThis->m_isTrackingMouse) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                pThis->m_isTrackingMouse = true;
            }
            POINT pt; GetCursorPos(&pt);
            int newIdx = pThis->HitTest(pt.x, pt.y);
            if (pThis->m_data.selectedIndex != newIdx) {
                pThis->m_data.selectedIndex = newIdx;
                pThis->Render();
            }
            return 0;
        }

        if (msg == WM_MOUSELEAVE && pThis) {
            pThis->m_isTrackingMouse = false;
            pThis->m_data.selectedIndex = -1;
            pThis->Render();
            return 0;
        }

        if (msg == WM_LBUTTONUP && pThis) {
            int hit = pThis->m_data.selectedIndex;
            if (hit >= 1000) {
                int idx = hit - 1000;
				HWND target = pThis->m_data.windows[idx].hwnd;
                PostMessage(target, WM_CLOSE, 0, 0);
                pThis->m_data.windows.erase(pThis->m_data.windows.begin() + idx);
                if (pThis->m_data.windows.empty()) pThis->Hide();
				else pThis->Render();
            }
            else if (hit >= 0) {
                HWND target = pThis->m_data.windows[pThis->m_data.selectedIndex].hwnd;
                if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
                SetForegroundWindow(target);
                pThis->Hide();
            }
            return 0;
        }

        if (msg == WM_PAINT && pThis) {
            pThis->Render();
            ValidateRect(hwnd, NULL);
            return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

public:
    DockPreviewWindow(ID2D1Factory *factory, IDWriteFactory *dwriteFactory, IWICImagingFactory *wicFactory)
        : m_pFactory(factory), m_pWicFactory(wicFactory), m_pDWriteFactory(dwriteFactory) {
        m_pRenderTarget = nullptr; m_pBrush = nullptr; m_hwnd = nullptr; m_hParent = nullptr;
        m_isVisible = false; m_isTrackingMouse = false;
        m_data.selectedIndex = -1;
        m_pLocalTextFormat = nullptr;
        m_lastUpdateTime = 0;

        WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"DockPreviewClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassEx(&wc);

        m_hwnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"DockPreviewClass", L"", WS_POPUP,
            0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    }

    ~DockPreviewWindow() {
        DiscardDeviceResources();
        if (m_hIcon) DestroyIcon(m_hIcon);
        if (m_hwnd) DestroyWindow(m_hwnd);
        if (m_pLocalTextFormat) m_pLocalTextFormat->Release();
    }

    bool IsMouseOver() const {
        return CheckMouseInSafeZone();
    }

    void Update(HWND parentBar, const std::wstring &title, const std::vector<HWND> &windows, HICON hIcon,
        RECT iconScreenRect, DockPreviewColors colors, const std::string &dockPosition)
    {
        m_hParent = parentBar;
        m_safeIconRect = iconScreenRect;

        if (!CheckMouseInSafeZone()) {
            if (m_isVisible) Hide();
            return;
        }

        m_lastUpdateTime = GetTickCount64();

        bool sameWindows = (windows.size() == m_data.windows.size());
        if (sameWindows) {
            for (size_t i = 0; i < windows.size(); i++) {
                if (windows[i] != m_data.windows[i].hwnd) { sameWindows = false; break; }
            }
        }
        if (m_isVisible && sameWindows && m_data.title == title && m_colors == colors &&
            iconScreenRect.left == m_lastTargetRect.left)
        {
            return;
        }
        m_lastTargetRect = iconScreenRect;
        m_data.title = title;
        m_colors = colors;

        UINT dpi = GetDpiForWindow(m_hParent ? m_hParent : m_hwnd);
        if (dpi == 0) dpi = 96;
        float newScale = (float)dpi / 96.0f;
        if (newScale != m_scale || !m_pLocalTextFormat) {
            m_scale = newScale;
            UpdateTextFormat();
        }

        if (m_pIconBitmap) { m_pIconBitmap->Release(); m_pIconBitmap = nullptr; }
        if (m_hIcon) DestroyIcon(m_hIcon);
        m_hIcon = hIcon ? CopyIcon(hIcon) : NULL;

        m_data.windows.clear();
        std::vector<HWND> seen;
        for (HWND w : windows) {
            if (std::find(seen.begin(), seen.end(), w) != seen.end()) continue;
            seen.push_back(w);
            wchar_t buf[256]; GetWindowTextW(w, buf, 256);
            m_data.windows.push_back({ w, (wcslen(buf) > 0 ? buf : L"Untitled") });
        }

        LayoutMetrics lm = GetMetrics();
        float maxTextW = (float)title.length() * (9.0f * m_scale);
        for (const auto &item : m_data.windows) {
            float w = (float)item.title.length() * (9.0f * m_scale);
            if (w > maxTextW) maxTextW = w;
        }
        float calculatedWidth = maxTextW + (60.0f * m_scale);
        if (calculatedWidth < 200.0f * m_scale) calculatedWidth = 200.0f * m_scale;
        if (calculatedWidth > 450.0f * m_scale) calculatedWidth = 450.0f * m_scale;

        int showCount = (int)m_data.windows.size();
        if (showCount > MAX_ITEMS_SHOWN) showCount = MAX_ITEMS_SHOWN;
        float calculatedHeight = lm.headerHeight + lm.paddingY + (showCount * lm.rowHeight) + lm.paddingY;

        int x, y;
        int gap = 10;
        HMONITOR hMon = MonitorFromRect(&iconScreenRect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);

        if (dockPosition == "left") {
            x = iconScreenRect.right + gap;
            y = iconScreenRect.top + ((iconScreenRect.bottom - iconScreenRect.top) / 2) - (int)(calculatedHeight / 2);
        }
        else if (dockPosition == "right") {
            x = iconScreenRect.left - (int)calculatedWidth - gap;
            y = iconScreenRect.top + ((iconScreenRect.bottom - iconScreenRect.top) / 2) - (int)(calculatedHeight / 2);
        }
        else if (dockPosition == "top") {
            x = iconScreenRect.left + ((iconScreenRect.right - iconScreenRect.left) / 2) - (int)(calculatedWidth / 2);
            y = iconScreenRect.bottom + gap;
        }
        else {
            x = iconScreenRect.left + ((iconScreenRect.right - iconScreenRect.left) / 2) - (int)(calculatedWidth / 2);
            y = iconScreenRect.top - (int)calculatedHeight - gap;
        }

        if (y < mi.rcWork.top) y = mi.rcWork.top + gap;
        if (y + (int)calculatedHeight > mi.rcWork.bottom) y = mi.rcWork.bottom - (int)calculatedHeight - gap;
        if (x < mi.rcWork.left) x = mi.rcWork.left + gap;
        if (x + (int)calculatedWidth > mi.rcWork.right) x = mi.rcWork.right - (int)calculatedWidth - gap;
        
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, (int)calculatedWidth, (int)calculatedHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);

        HRGN hRgn = CreateRoundRectRgn(0, 0, (int)calculatedWidth + 1, (int)calculatedHeight + 1, (int)lm.cornerRadius, (int)lm.cornerRadius);
        SetWindowRgn(m_hwnd, hRgn, TRUE);

        if (!m_isVisible) {
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
            m_isVisible = true;
            SetTimer(m_hwnd, MONITOR_TIMER_ID, 100, NULL);
        }
        Render();
    }

    void Hide() {
        if (m_isVisible) {
            KillTimer(m_hwnd, MONITOR_TIMER_ID);
            ShowWindow(m_hwnd, SW_HIDE);
            m_isVisible = false;
            m_data.selectedIndex = -1;
            m_lastTargetRect = {};
        }
    }

    bool IsVisible() const { return m_isVisible; }
    HWND GetHwnd() const { return m_hwnd; }
    int GetHoveredIndex() const { return m_data.selectedIndex; }
};