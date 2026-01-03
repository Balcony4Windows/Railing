#pragma once
#include "Module.h"

class WorkspacesModule : public Module
{
public:
	float itemWidth = 20.0f;
	float itemPadding = 0.0f;
	int activeIndex = 0;
	int hoveredIndex = -1;
	int count = 5; // default

	WorkspacesModule(const ModuleConfig &cfg) : Module(cfg) {}
	float GetContentWidth(RenderContext &ctx) override
	{
		itemPadding = config.baseStyle.padding.left + config.baseStyle.padding.right;
		if (itemPadding == 0.0f) itemPadding = 8.0f;
		return (itemPadding + itemWidth) * count;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		float cursor = (ctx.isVertical) ? y : x;
		for (int i = 0; i < count; i++) {
			Style s = config.itemStyle;
			if (i == activeIndex && config.states.count("active")) {
				const Style &activeS = config.states.at("active");
				// Manual Merge Logic
				if (activeS.has_bg) s.bg = activeS.bg;
				if (activeS.has_fg) s.fg = activeS.fg;
				if (activeS.has_radius) s.radius = activeS.radius;
				if (activeS.has_font_weight) s.font_weight = activeS.font_weight;
			}
			if (i == hoveredIndex && config.states.count("hover")) {
				const Style &hoverS = config.states.at("hover");
				if (hoverS.has_bg) s.bg = hoverS.bg;
				if (hoverS.has_fg) s.fg = hoverS.fg;
				if (hoverS.has_radius) s.radius = hoverS.radius;
			}
			D2D1_RECT_F itemRect;
			if (!ctx.isVertical) itemRect = D2D1::RectF(cursor, y, cursor + itemWidth + itemPadding, y + h);
			else itemRect = D2D1::RectF(x, cursor, x + w, cursor + itemWidth + itemPadding);

			if (i == activeIndex || i == hoveredIndex || s.has_bg) {
				D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(itemRect, s.radius, s.radius);
				ctx.bgBrush->SetColor(s.bg);
				ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
			}

			// Text
			wchar_t buf[4];
			swprintf_s(buf, L"%d", i + 1);
			ctx.textBrush->SetColor(s.fg);
			IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
			ctx.rt->DrawTextW(buf, (UINT32)wcslen(buf), fmt, itemRect, ctx.textBrush);
			cursor += (itemWidth + itemPadding);
		}
	}

	void SetActiveIndex(int index) { activeIndex = index; }
	void SetHoveredIndex(int index) { hoveredIndex = index; }
};