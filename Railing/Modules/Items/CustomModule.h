#pragma once
#include "Module.h"

class CustomModule : public Module
{
	std::wstring cachedIcon;
	std::wstring cachedText;
	bool contentLoaded = false;
public:
	CustomModule(const ModuleConfig &cfg) : Module(cfg) {}

	float GetContentWidth(RenderContext &ctx) override
	{
		if (!contentLoaded) {
			if (!config.icon.empty()) cachedIcon = Utf8ToWide(config.icon);
			if (!config.format.empty() && config.format != " ") cachedText = Utf8ToWide(config.format);
			contentLoaded = true;
		}

		float width = 0.0f;
		if (!cachedIcon.empty()) width += 24.0f;

		if (!cachedText.empty()) {
			Style s = GetEffectiveStyle();
			IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
			IDWriteTextLayout *layout = GetLayout(ctx, cachedText, fmt);

			DWRITE_TEXT_METRICS metrics;
			layout->GetMetrics(&metrics);
			width += metrics.width;
			if (!cachedIcon.empty()) width += 4.0f;
		}

		Style s = GetEffectiveStyle();
		width += s.padding.left + s.padding.right + s.margin.left + s.margin.right;
		return width;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		Style s = GetEffectiveStyle();
		if (s.has_bg) {
			D2D1_RECT_F bgRect = D2D1::RectF(
				x + s.margin.left,
				y + s.margin.top,
				x + w - s.margin.right,
				y + h - s.margin.bottom
			);
			D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(bgRect, s.radius, s.radius);
			ctx.bgBrush->SetColor(s.bg);
			ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
		}
		float cursorX = x + s.margin.left + s.padding.left;
		ctx.textBrush->SetColor(s.fg);

		if (!config.icon.empty()) {
			std::wstring iconText = Utf8ToWide(config.icon);
			D2D1_RECT_F iconRect = D2D1::RectF(cursorX, y, cursorX + 24.0f, y + h);
			ctx.rt->DrawTextW(
				iconText.c_str(),
				(UINT32)iconText.length(),
				ctx.emojiFormat,
				iconRect,
				ctx.textBrush,
				D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT // Critical for color emojis
			);
			cursorX += 24.0f + 4.0f;
		}
		if (!config.format.empty()) {
			std::wstring text = Utf8ToWide(config.format);

			D2D1_RECT_F textRect = D2D1::RectF(
				cursorX,
				y + s.margin.top + s.padding.top,
				x + w - s.margin.right - s.padding.right,
				y + h - s.margin.bottom - s.padding.bottom
			);

			ctx.rt->DrawTextW(text.c_str(), (UINT32)text.length(), ctx.textFormat, textRect, ctx.textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
		}
	}
};