#pragma once
#include "Module.h"

class GpuModule : public Module
{
	int lastTemp = -1;
	std::wstring cachedStr;
public:
	GpuModule(const ModuleConfig &cfg) : Module(cfg) {}
	float GetContentWidth(RenderContext &ctx) override {
		if (ctx.gpuTemp != lastTemp) {
			lastTemp = ctx.gpuTemp;
			std::string fmt = config.format.empty() ? "GPU: {temp}\u00B0C" : config.format;
			cachedStr = FormatOutput(fmt, "{temp}", std::to_wstring(lastTemp));
		}

		Style s = GetEffectiveStyle();
		IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
		IDWriteTextLayout *layout = GetLayout(ctx, cachedStr, fmt);

		DWRITE_TEXT_METRICS metrics;
		layout->GetMetrics(&metrics);
		return metrics.width + 4.0f + s.padding.left + s.padding.right + s.margin.left + s.margin.right;
	}
	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		Style s = GetEffectiveStyle();
		D2D1_COLOR_F color = config.baseStyle.fg;
		// Thresholds: Orange > 75C, Red > 85C
		for (const auto &th : config.thresholds) {
			if (ctx.gpuTemp >= th.val) {
				color = th.style.fg;
				if (th.style.has_bg) s.bg = th.style.bg;
			}
		}

		DrawProgressBar(ctx, x, y, w, h, ctx.gpuTemp / 100.0f, color);
		std::string fmt = config.format.empty() ? "GPU: {temp}\u00B0C" : config.format;
		std::wstring text = FormatOutput(fmt, "{temp}", std::to_wstring(ctx.gpuTemp));

		ctx.textBrush->SetColor(D2D1::ColorF(1, 1, 1, 1)); // White text overlay
		ctx.rt->DrawTextW(
			cachedStr.c_str(),
			(UINT32)cachedStr.length(),
			ctx.textFormat,
			D2D1::RectF(x, y, x + w, y + h),
			ctx.textBrush
		);
	}
};