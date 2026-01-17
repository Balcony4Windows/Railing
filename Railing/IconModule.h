#pragma once
#include "Module.h"

class IconModule : public Module
{ // Generic Icon Module (Network, Battery, etc. Placeholder)
public:
	IconModule(const ModuleConfig &cfg) : Module(cfg) {}

	float GetContentWidth(RenderContext &ctx) override
	{
		return ctx.iconFormat->GetFontSize()
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
		std::wstring text = L"\uE774"; // Default Globe
		if (config.type == "network") {
			text = L"\uE774";
		}

		if (config.type == "audio") {
			if (ctx.isMuted || ctx.volume == 0.0f) text = L"\uE74F"; // Mute
			else if (ctx.volume < 0.33f) text = L"\uE993"; // Low
			else if (ctx.volume < 0.66f) text = L"\uE994"; // Medium
			else text = L"\uE995"; // High
		}

		if (config.type == "battery") text = L"\uE83F";
		if (config.type == "tray") text = L"\uE70E";
		if (config.type == "notification") text = L"\uEA8F";

		D2D1_RECT_F rect = D2D1::RectF(
			x + s.margin.left + s.padding.left,
			y + s.margin.top + s.padding.top,
			x + w - s.margin.right - s.padding.right,
			y + h - s.margin.bottom - s.padding.bottom);
		ctx.textBrush->SetColor(s.fg);
		ctx.rt->DrawTextW(text.c_str(), 1, ctx.iconFormat, rect, ctx.textBrush);
	}
};