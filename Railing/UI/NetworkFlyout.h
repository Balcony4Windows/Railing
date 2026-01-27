#pragma once
#include "NetworkBackend.h"
#include "ThemeTypes.h"
#include <dwrite.h>
#include <windowsx.h>
#include <mutex>

#define WM_NET_SCAN_COMPLETE (WM_USER + 101)

class NetworkFlyout
{
public:
	HWND hwnd = NULL;
    HINSTANCE hInst;

    bool IsVisible() { return IsWindowVisible(hwnd); }

	NetworkBackend backend;
	std::vector<WifiNetwork> cachedNetworks;
	ThemeConfig &theme;

    enum class AnimationState { Hidden, Entering, Visible, Exiting };
    AnimationState animState = AnimationState::Hidden;

    static ULONGLONG lastAutoCloseTime;
    ULONGLONG lastScanTime = 0;
    RECT currentAnchor = { 0 };

    // Shared Resources (I DO NOT OWN THESE - DO NOT RELEASE)
    ID2D1Factory *pFactory = nullptr;
    IDWriteFactory *pWriteFactory = nullptr;
    IDWriteTextFormat *pTextFormat = nullptr;
    IDWriteTextFormat *pIconFormat = nullptr;

    // Local Resources (I own these)
    ID2D1HwndRenderTarget *pRenderTarget = nullptr;
    ID2D1SolidColorBrush *pTextBrush = nullptr;
    ID2D1SolidColorBrush *pBgBrush = nullptr;
    ID2D1SolidColorBrush *pBorderBrush = nullptr;
    ID2D1SolidColorBrush *pHighlightBrush = nullptr;
    ID2D1SolidColorBrush *pScrollBrush = nullptr;
    ID2D1SolidColorBrush *pScrollBrushActive = nullptr;

    int selectedIndex = -1;
    std::wstring passwordBuffer; // What has the user typed?
    std::wstring statusMessage;
    D2D1_COLOR_F statusColor = D2D1::ColorF(1, 1, 1, 1);
    bool isWorking = false;
    bool isScanning = false;

    int hoveredIndex = -1;
    float scrollOffset = 0.0f;
    float maxScroll = 0.0f;

    float currentAlpha = 0.0f;
    float currentOffset = 0.0f;
    ULONGLONG lastAnimTime = 0;
    std::recursive_mutex networkMutex;

    int targetX = 0;
    int targetY = 0;
    bool isDraggingScrollbar = false;
    float dragStartY = 0.0f;
    float dragStartScrollOffset = 0.0f;

    bool isAllSelected = false; // Ctrl + A
    std::wstring GetClipboardText();
    void SetClipboardText(const std::wstring &text);
    bool HitTestButton(int index, float lx, float ly);
    bool OnSetCursor(int x, int y);

    void PositionWindow(RECT anchorRect);
    void UpdateAnimation();
    void OnClick(int x, int y);
    void OnDrag(int x, int y);
    void OnHover(int x, int y);
    void OnChar(wchar_t c);

	const int itemHeight = 45;
    const int expandedHeight = 125;
	const int headerHeight = 40;
	const int width = 360;
    const int maxWindowHeight = 450;
	int currentWindowHeight = 200;

    IDWriteTextLayout *pInputLayout = nullptr;

    int selectionAnchor = 0; // Where the mouse was pressed
    int selectionFocus = 0;  // Where the mouse currently is (the caret)
    bool isSelectingText = false;
    void UpdateInputLayout(float maxWidth);
    int GetInputCaretFromPoint(float x, float y, float offsetX, float offsetY);

    float GetScale() { return (float)GetDpiForWindow(hwnd) / 96.0f; }

    NetworkFlyout(HINSTANCE hInst,
        ID2D1Factory *pFact,
        IDWriteFactory *pWFact,
        IDWriteTextFormat *pTxt,
        IDWriteTextFormat *pIcon,
        ThemeConfig &theme);

    ~NetworkFlyout();

    void Toggle(RECT anchorRect);

    void CreateDeviceResources();

    void Draw();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        NetworkFlyout *self = (NetworkFlyout *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (msg == WM_NCCREATE) {
            CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
            self = (NetworkFlyout *)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        }

        if (self) {
            switch (msg) {
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint(hwnd, &ps);
                self->Draw();
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_NET_SCAN_COMPLETE:
            {
                self->PositionWindow(self->currentAnchor);

                float dpi = (float)GetDpiForWindow(hwnd);
                float scale = dpi / 96.0f;
                int w = (int)(self->width * scale);
                int h = (int)(self->currentWindowHeight * scale);

                int r = (int)self->theme.global.radius;
                HRGN hRgn = (r > 0) ? CreateRoundRectRgn(0, 0, w, h, r * 2, r * 2) : CreateRectRgn(0, 0, w, h);
                SetWindowRgn(hwnd, hRgn, TRUE);

                SetWindowPos(hwnd, NULL, self->targetX, self->targetY, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

                if (self->pRenderTarget) self->pRenderTarget->Resize(D2D1::SizeU(w, h));
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            case WM_LBUTTONDOWN:
                self->OnClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                SetCapture(hwnd);
                return 0;
            case WM_LBUTTONUP:
                ReleaseCapture(); 
                self->isDraggingScrollbar = false;
                return 0;
            case WM_MOUSEMOVE:
                POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
                if (wParam & MK_LBUTTON) self->OnDrag(pt.x, pt.y); // Continue selection
                else self->OnHover(pt.x, pt.y);
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT) {
                    POINT pt; GetCursorPos(&pt);
                    ScreenToClient(hwnd, &pt);
                    if (self->OnSetCursor(pt.x, pt.y)) return TRUE;
                }
                break;
            case WM_MOUSEWHEEL:
                if (self->maxScroll > 0) {
                    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                    self->scrollOffset -= (delta / (float)WHEEL_DELTA) * 45.0f;
                    if (self->scrollOffset < 0) self->scrollOffset = 0;
                    if (self->scrollOffset > self->maxScroll) self->scrollOffset = self->maxScroll;
                    InvalidateRect(hwnd, NULL, FALSE);
                    POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
                    SendMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
                }
                return 0;
            case WM_CHAR:
                self->OnChar((wchar_t)wParam);
                return 0;
            case WM_ACTIVATE:
            case WM_KILLFOCUS:
                if (LOWORD(wParam) == WA_INACTIVE && self->animState != AnimationState::Hidden) {
                    self->animState = AnimationState::Exiting;
                    self->lastAnimTime = GetTickCount64();
                    self->lastAutoCloseTime = GetTickCount64();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            case WM_DESTROY:
                return 0;
            }
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
};

