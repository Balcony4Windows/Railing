#include "Widgets.h"
#include <windows.h>
#include <string>

Workspaces::Workspaces() {}
Workspaces::~Workspaces() {
    if (pActiveBrush) pActiveBrush->Release();
    if (pTextBrush) pTextBrush->Release();
    if (pHoverBrush) pHoverBrush->Release();
}

int Workspaces::GetActiveVirtualDesktop() {
    return activeIndex;
}

void Workspaces::Draw(const RenderContext &ctx) {
    if (!pActiveBrush) ctx.rt->CreateSolidColorBrush(ctx.textBrush->GetColor(), &pActiveBrush);
    if (!pTextBrush) ctx.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray), &pTextBrush);

    float startX = 45.0f;
    float startY = (ctx.logicalHeight - itemHeight) / 2.0f;
    float totalWidth = (count * itemWidth) + ((count - 1) * padding);
    bounds = D2D1::RectF(startX, startY, startX + totalWidth, startY + itemHeight);

    for (int i = 0; i < count; i++) {
        float x = startX + (i * (itemWidth + padding));
        D2D1_RECT_F itemRect = D2D1::RectF(x, startY, x + itemWidth, startY + itemHeight);

        std::wstring displayText;

        if (i == activeIndex) {
            // ACTIVE STATE:
            displayText = L"[" + std::to_wstring(i + 1) + L"]";
            ctx.rt->DrawTextW(
                displayText.c_str(),
                (UINT32)displayText.length(),
                ctx.textFormat,
                itemRect,
                pActiveBrush
            );
        }
        else {
            // INACTIVE STATE:
            displayText = std::to_wstring(i + 1);
            ctx.rt->DrawTextW(
                displayText.c_str(),
                (UINT32)displayText.length(),
                ctx.textFormat,
                itemRect,
                pTextBrush
            );
        }
    }
}

const wchar_t *months[] = { L"", L"JAN", L"FEB", L"MAR", L"APR", L"MAY", L"JUN",
                            L"JUL", L"AUG", L"SEP", L"OCT", L"NOV", L"DEC" };

void Clock::Draw(const RenderContext &ctx)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    // 12-hour time is wider ("10:00 PM" vs "22:00"), so we give it more space
    float clockWidth = ctx.use24HourTime ? 55.0f : 85.0f;

    float paddingRight = ctx.moduleGap;
    float x = ctx.logicalWidth - clockWidth - paddingRight;
    float height = ctx.logicalHeight * 0.6f;
    const float y = (ctx.logicalHeight - height) / 2.0f;

    D2D1_RECT_F rect = D2D1::RectF(x, y, x + clockWidth, y + height);
    lastRect = rect;

    // Background
    ctx.bgBrush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, ctx.pillOpacity));
    ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(rect, ctx.rounding, ctx.rounding), ctx.bgBrush);

    // Border
    if (ctx.borderBrush) {
        ctx.rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, ctx.rounding, ctx.rounding), (ID2D1Brush *)ctx.borderBrush, 1.0f);
    }

    wchar_t buf[64];

    if (ctx.use24HourTime) {
        // Military: "21:00"
        swprintf_s(buf, L"%02d:%02d", st.wHour, st.wMinute);
    }
    else {
        // Standard: "9:00 PM"
        int hour = st.wHour % 12;
        if (hour == 0) hour = 12;
        const wchar_t *suffix = (st.wHour >= 12) ? L"PM" : L"AM";

        swprintf_s(buf, L"%d:%02d %s", hour, st.wMinute, suffix);
    }

    ctx.textBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    ctx.rt->DrawTextW(
        buf,
        (UINT32)wcslen(buf),
        ctx.textFormat,
        rect,
        ctx.textBrush,
        D2D1_DRAW_TEXT_OPTIONS_NONE,
        DWRITE_MEASURING_MODE_NATURAL
    );
}