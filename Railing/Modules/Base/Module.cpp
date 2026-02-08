#include "Module.h"

void Module::Draw(RenderContext &ctx, float x, float y, float constraintSize) {
    float paddingX = config.baseStyle.padding.left + config.baseStyle.padding.right;
    float paddingY = config.baseStyle.padding.top + config.baseStyle.padding.bottom;
    float contentSize = GetContentWidth(ctx);

    float finalX, finalY, finalW  , finalH;

    if (!ctx.isVertical) {
        // Horizontal
        finalX = x + config.baseStyle.margin.left;
        finalY = y + config.baseStyle.margin.top;
        finalW = contentSize + paddingX;
        finalH = constraintSize - (config.baseStyle.margin.top + config.baseStyle.margin.bottom);
    }
    else {
        // Vertical
        finalX = x + config.baseStyle.margin.left;
        finalY = y + config.baseStyle.margin.top;
        finalW = constraintSize - (config.baseStyle.margin.left + config.baseStyle.margin.right);

        // FIX: Use contentSize for Height. If 0 (uncalculated), fallback to square.
        if (contentSize > 0) finalH = contentSize + paddingY;
        else finalH = finalW;
    }

    // Draw Background
    if (config.baseStyle.has_bg) {
        D2D1_RECT_F bgRect = D2D1::RectF(finalX, finalY, finalX + finalW, finalY + finalH);
        D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(bgRect, config.baseStyle.radius, config.baseStyle.radius);
        ctx.bgBrush->SetColor(config.baseStyle.bg);
        ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);

        if (config.baseStyle.borderWidth > 0) {
            ctx.borderBrush->SetColor(config.baseStyle.borderColor);
            ctx.rt->DrawRoundedRectangle(rounded, ctx.borderBrush, config.baseStyle.borderWidth);
        }
    }

    // Cache the rect for click detection
    this->cachedRect = D2D1::RectF(finalX, finalY, finalX + finalW, finalY + finalH);

    RenderContent(ctx,
        finalX + config.baseStyle.padding.left,
        finalY + config.baseStyle.padding.top,
        finalW - paddingX,
        finalH - paddingY
    );
}

void Module::CalculateWidth(RenderContext &ctx)
{
    float contentSz = GetContentWidth(ctx); // This is "Main Axis Size"

    if (!ctx.isVertical) {
        float paddingW = config.baseStyle.padding.left + config.baseStyle.padding.right;
        float marginW = config.baseStyle.margin.left + config.baseStyle.margin.right;
        width = contentSz + paddingW + marginW;
    }
    else {
        float paddingH = config.baseStyle.padding.top + config.baseStyle.padding.bottom;
        float marginH = config.baseStyle.margin.top + config.baseStyle.margin.bottom;
        width = contentSz + paddingH + marginH;
    }
}