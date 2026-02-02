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
    else if (uMsg == WM_MOUSEMOVE) {
        if (!tracking) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            tracking = true;
        }
    }
    else if (uMsg == WM_MOUSELEAVE) {
        tracking = false;
        if (handler) handler->Hide();
    }

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
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"RailingTooltip", L"",
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, wc.hInstance, NULL
    );
    SetWindowLongPtr(hwndTooltip, GWLP_USERDATA, (LONG_PTR)this);

	SetLayeredWindowAttributes(hwndTooltip, 0, 255, LWA_ALPHA);
}

void TooltipHandler::Show(const std::wstring &text, RECT iconRect, std::string &position, float scale) {
    if (!hwndTooltip) return;
    if (text == currentText && IsWindowVisible(hwndTooltip)) return;

    currentText = text;
    SetWindowTextW(hwndTooltip, text.c_str());

    SIZE size = GetTextSize(text);
    int width = size.cx + 24;
    int height = size.cy + 16;

    int iconWidth = (int)((iconRect.right - iconRect.left) * scale);
    int iconLeft = (int)(iconRect.left * scale);
    int clientX = iconLeft + (iconWidth / 2) - (width / 2);

    POINT ptScreen = { clientX, 0 };
    ClientToScreen(hwndParent, &ptScreen);

    RECT barRect;
    GetWindowRect(hwndParent, &barRect);
    int tooltipY;

    if (position == "bottom") {
        tooltipY = barRect.top - height - 4;
    } else tooltipY = barRect.bottom + 4;

    if (ptScreen.x < 0) ptScreen.x = 0;

    HRGN hRgn = CreateRoundRectRgn(0, 0, width, height, 14, 14);
    SetWindowRgn(hwndTooltip, hRgn, TRUE);

    SetWindowPos(hwndTooltip, HWND_TOPMOST, ptScreen.x, tooltipY, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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