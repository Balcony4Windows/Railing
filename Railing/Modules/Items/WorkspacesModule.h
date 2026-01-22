#pragma once
#include "Module.h"
#include <string>
#include <algorithm>

class WorkspacesModule : public Module
{
public:
    float itemWidth = 20.0f;
    float itemPadding = 0.0f;

    int activeIndex = 0;
    int hoveredIndex = -1;
    int count = 5;

    WorkspacesModule(const ModuleConfig &cfg) : Module(cfg) {}

    float GetContentWidth(RenderContext &ctx) override
    {
        if (ctx.workspaces) {
            HWND targetWindow = ctx.foregroundWindow;
            DWORD targetPID = 0;
            GetWindowThreadProcessId(targetWindow, &targetPID);

            DWORD myPID = GetCurrentProcessId();

            // ONLY update activeIndex if the focused window is NOT the Bar/Flyout
            if (targetPID != myPID) {
                if (ctx.workspaces->managedWindows.count(targetWindow)) {
                    this->activeIndex = ctx.workspaces->managedWindows[targetWindow];
                }
                else {
                    this->activeIndex = ctx.workspaces->currentWorkspace;
                }
            }
        }

        float totalWidth = 0.0f;
        for (int i = 0; i < count; i++) {
            Style s = ResolveStyle(i);
            float width = s.margin.left + s.padding.left + 20.0f + s.padding.right + s.margin.right;
            totalWidth += width;
        }

        if (count > 0) {
            float avgTotal = totalWidth / count;
            itemWidth = 20.0f;
            itemPadding = avgTotal - itemWidth;
        }

        return totalWidth;
    }

    void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
    {
        float cursor = (ctx.isVertical) ? y : x;

        for (int i = 0; i < count; i++) {
            Style s = ResolveStyle(i);

            float padL = s.padding.left;
            float padR = s.padding.right;
            float margL = s.margin.left;
            float margR = s.margin.right;
            float contentW = 20.0f;

            float layoutStep = margL + padL + contentW + padR + margR;

            D2D1_RECT_F boxRect;

            if (!ctx.isVertical) {
                float startX = cursor + margL; // Skip left margin
                float visualW = padL + contentW + padR; // Width of color box

                float boxTop = y + s.margin.top;
                float boxBottom = y + h - s.margin.bottom;

                boxRect = D2D1::RectF(startX, boxTop, startX + visualW, boxBottom);
            }
            else {
                float startY = cursor + s.margin.top;
                boxRect = D2D1::RectF(x + s.margin.left, startY, x + w - s.margin.right, startY + s.padding.top + contentW + s.padding.bottom);
            }

            if (s.has_bg || s.has_border || i == activeIndex || i == hoveredIndex) {
                D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(boxRect, s.radius, s.radius);

                if (s.has_bg) {
                    ctx.bgBrush->SetColor(s.bg);
                    ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
                }
                if (s.has_border && s.borderWidth > 0.0f) {
                    ctx.bgBrush->SetColor(s.borderColor);
                    ctx.rt->DrawRoundedRectangle(rounded, ctx.bgBrush, s.borderWidth);
                }
            }

            wchar_t buf[4];
            swprintf_s(buf, L"%d", i + 1);

            ctx.textBrush->SetColor(s.fg);
            IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ctx.rt->DrawTextW(buf, (UINT32)wcslen(buf), fmt, boxRect, ctx.textBrush);
            cursor += layoutStep;
        }
    }

    void SetActiveIndex(int index) { activeIndex = index; }
    void SetHoveredIndex(int index) { hoveredIndex = index; }

private:
    Style ResolveStyle(int i) {
        Style s = config.itemStyle; // Base
        if (i == activeIndex && config.states.count("active")) {
            s = s.Merge(config.states.at("active"));
        }
        if (i == hoveredIndex && config.states.count("hover")) {
            s = s.Merge(config.states.at("hover"));
        }
        return s;
    }
};