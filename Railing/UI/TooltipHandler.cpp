#include "TooltipHandler.h"

LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static bool tracking = false;
	TooltipHandler *handler = reinterpret_cast<TooltipHandler *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HBRUSH bgBrush = CreateSolidBrush(RGB(86, 86, 86));
        HPEN hPen = (HPEN)GetStockObject(NULL_PEN);

        RECT rc; GetClientRect(hwnd, &rc);

        HGDIOBJ oldBrush = SelectObject(hdc, bgBrush);
        HGDIOBJ oldPen = SelectObject(hdc, hPen);

        // Note: With NULL_PEN, the outline is not drawn, only the fill.
        RoundRect(hdc, rc.left, rc.top, rc.right + 1, rc.bottom + 1, 14, 14);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);

        wchar_t text[256];
        GetWindowTextW(hwnd, text, 256);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(240, 240, 240));

        HFONT hFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        InflateRect(&rc, -12, -8);
        DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_NOPREFIX);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        DeleteObject(bgBrush);
        // Do NOT delete hPen because it is a Stock Object
        EndPaint(hwnd, &ps);
        return 0;
    }
    else if (uMsg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    else if (uMsg == WM_NCHITTEST) return HTTRANSPARENT;
    else if (uMsg == WM_ERASEBKGND) return 1;
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

TooltipHandler::TooltipHandler() {}
TooltipHandler::~TooltipHandler() { if (hwndTooltip) DestroyWindow(hwndTooltip); }

void TooltipHandler::Initialize(HWND hParent) {
    hwndParent = hParent;

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = TooltipWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"RailingTooltip";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
	wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClass(&wc);

    hwndTooltip = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"RailingTooltip", L"",
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, wc.hInstance, NULL
    );
    SetWindowLongPtr(hwndTooltip, GWLP_USERDATA, (LONG_PTR)this);

	SetLayeredWindowAttributes(hwndTooltip, 0, 255, LWA_ALPHA);
}

void TooltipHandler::Show(const std::wstring &text, RECT iconRect, const std::string &position, float scale) {
    if (!hwndTooltip) return;
    if (text == currentText && IsWindowVisible(hwndTooltip)) return;

    currentText = text;
    SetWindowTextW(hwndTooltip, text.c_str());

    SIZE size = GetTextSize(text);
    int tipW = size.cx + 24;
    int tipH = size.cy + 16;

    RECT physRect = {
        (LONG)(iconRect.left * scale),
        (LONG)(iconRect.top * scale),
        (LONG)(iconRect.right * scale),
        (LONG)(iconRect.bottom * scale)
    };

    POINT tl = { physRect.left, physRect.top };
    POINT br = { physRect.right, physRect.bottom };
    ClientToScreen(hwndParent, &tl);
    ClientToScreen(hwndParent, &br);

    int iconScreenCenterX = tl.x + ((br.x - tl.x) / 2);
    int iconScreenCenterY = tl.y + ((br.y - tl.y) / 2);
    RECT barRect;
    GetWindowRect(hwndParent, &barRect);

    int finalX = 0;
    int finalY = 0;
    int gap = 10;

    if (position == "left") {
        finalX = barRect.right + gap;
        finalY = iconScreenCenterY - (tipH / 2);
    }
    else if (position == "right") {
        finalX = barRect.left - tipW - gap;
        finalY = iconScreenCenterY - (tipH / 2);
    }
    else if (position == "top") {
        finalX = iconScreenCenterX - (tipW / 2);
        finalY = barRect.bottom + gap;
    }
    else {
        finalX = iconScreenCenterX - (tipW / 2);
        finalY = barRect.top - tipH - gap;
    }

    HMONITOR hMon = MonitorFromRect(&barRect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    int padding = 4;
    if (finalX < mi.rcWork.left + padding) finalX = mi.rcWork.left + padding;
    if (finalX + tipW > mi.rcWork.right - padding) finalX = mi.rcWork.right - tipW - padding;

    if (finalY < mi.rcWork.top + padding) finalY = mi.rcWork.top + padding;
    if (finalY + tipH > mi.rcWork.bottom - padding) finalY = mi.rcWork.bottom - tipH - padding;

    HRGN hRgn = CreateRoundRectRgn(0, 0, tipW, tipH, 14, 14);
    SetWindowRgn(hwndTooltip, hRgn, TRUE);

    SetWindowPos(hwndTooltip, HWND_TOPMOST, finalX, finalY, tipW, tipH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwndTooltip, NULL, TRUE);
}

void TooltipHandler::Hide() {
    if (hwndTooltip && IsWindowVisible(hwndTooltip)) {
        ShowWindow(hwndTooltip, SW_HIDE);
        currentText = L"";
    }
}

SIZE TooltipHandler::GetTextSize(const std::wstring &text) {
    SIZE size = { 0, 0 };
    HDC hdc = GetDC(hwndTooltip);
    if (hdc) {
        HFONT hFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

		RECT rcCalc = { 0, 0, 0, 0 };
		DrawTextW(hdc, text.c_str(), (int)text.length(), &rcCalc, DT_LEFT | DT_NOPREFIX | DT_CALCRECT);
        size.cx = rcCalc.right - rcCalc.left;
        size.cy = rcCalc.bottom - rcCalc.top;

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        ReleaseDC(hwndTooltip, hdc);
    }
    return size;
}