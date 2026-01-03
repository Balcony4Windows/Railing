#pragma once
#include "Module.h"

class AppIconModule : public Module
{
public:
	AppIconModule(const ModuleConfig &cfg) : Module(cfg) {}

	float GetContentWidth(RenderContext &ctx) override
	{
		return 32.0f
			+ config.baseStyle.padding.left + config.baseStyle.padding.right
			+ config.baseStyle.margin.left + config.baseStyle.margin.right;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		Style s = GetEffectiveStyle();
		if (s.has_bg) {
			D2D1_RECT_F bgRect = D2D1::RectF(
				x + s.margin.left, y + s.margin.top,
				x + w - s.margin.right, y + h - s.margin.bottom);
			D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(bgRect, s.radius, s.radius);
			ctx.bgBrush->SetColor(s.bg);
			ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
		}
		if (ctx.appIcon) {
			float availableH = h - s.margin.top - s.margin.bottom;
			float iconSize = availableH * 0.8f;
			float contentX = x + s.margin.left + s.padding.left;
			float contentW = w - s.margin.left - s.margin.right - s.padding.left - s.padding.right;
			float offX = contentX + (contentW - iconSize) / 2.0f;
			float offY = y + s.margin.top + (availableH - iconSize) / 2.0f;

			D2D1_RECT_F dest = D2D1::RectF(offX, offY, offX + iconSize, offY + iconSize);
			ctx.rt->DrawBitmap(ctx.appIcon, dest);
		}
	}
};