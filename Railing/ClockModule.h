#pragma once
#include "Module.h"

class ClockModule : public Module
{
public:
	std::wstring cacheStr;
	ClockModule(const ModuleConfig &cfg) : Module(cfg) {}

	void Update() override { cacheStr = GetTimeStr(); }

	float GetContentWidth(RenderContext &ctx) override
	{
		Style s = GetEffectiveStyle();
		std::wstring text = GetTimeStr();
		IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
		IDWriteTextLayout *layout = GetLayout(ctx, text, fmt);
		DWRITE_TEXT_METRICS metrics;
		layout->GetMetrics(&metrics);
		return metrics.width + 4.0f + config.baseStyle.padding.left + config.baseStyle.padding.right
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

		std::wstring text = GetTimeStr();
		D2D1_RECT_F rect = D2D1::RectF(
			x + s.margin.left + s.padding.left,
			y + s.margin.top + s.padding.top,
			x + w - s.margin.right - s.padding.right,
			y + h - s.margin.bottom - s.padding.bottom);
		ctx.textBrush->SetColor(s.fg);
		IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
		ctx.rt->DrawTextW(text.c_str(), (UINT32)text.length(), fmt, rect, ctx.textBrush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}
private:
	std::wstring GetTimeStr()
	{
		time_t now = time(0);
		struct tm tstruct;
		localtime_s(&tstruct, &now);
		wchar_t buf[128];

		std::string fmt = config.format.empty() ? "%H:%M" : config.format;
		size_t pos = 0;
		while ((pos = fmt.find("{:")) != std::string::npos) {
			fmt.replace(pos, 2, "");
		}
		pos = 0;
		while ((pos = fmt.find("}")) != std::string::npos) {
			fmt.replace(pos, 1, "");
		}
		std::wstring wfmt(fmt.begin(), fmt.end());
		if (wcsftime(buf, std::size(buf), wfmt.c_str(), &tstruct) == 0) {
			return L"Format Error";
		}

		return buf;
	}
};