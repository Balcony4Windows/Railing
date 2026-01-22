#pragma once
#include "Module.h"

class CpuModule : public Module
{
	int lastUsage = -1;
	std::wstring cachedStr;
public:
	CpuModule(const ModuleConfig &cfg) : Module(cfg) {}

    float GetContentWidth(RenderContext &ctx) override
    {
        if (ctx.cpuUsage != lastUsage) {
            lastUsage = ctx.cpuUsage;
            std::string fmt = config.format.empty() ? "CPU: {usage}%" : config.format;
            cachedStr = FormatOutput(fmt, "{usage}", std::to_wstring(lastUsage));
        }
        Style s = GetEffectiveStyle();
        IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
        IDWriteTextLayout *layout = GetLayout(ctx, cachedStr, fmt);

        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);
        return metrics.width + 4.0f 
            + s.padding.left + s.padding.right
            + s.margin.left + s.margin.right;
    }
	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		Style s = GetEffectiveStyle();

		D2D1_COLOR_F color = config.baseStyle.fg;
		for (const auto &th : config.thresholds) {
			if (ctx.cpuUsage >= th.val) color = th.style.fg;
		}
		DrawProgressBar(ctx, x, y, w, h, ctx.cpuUsage / 100.0f, color);

		ctx.textBrush->SetColor(s.fg);
		IDWriteTextFormat *fmt = (s.font_weight == "bold") ? ctx.boldTextFormat : ctx.textFormat;
		ctx.rt->DrawTextW(cachedStr.c_str(), (UINT32)cachedStr.length(), fmt, D2D1::RectF(x, y, x + w, y + h), ctx.textBrush);
	}
};