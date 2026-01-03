#pragma once
#include "Module.h"

class GroupModule : public Module
{
public:
	std::vector<Module *> children;
	GroupModule(const ModuleConfig &cfg) : Module(cfg) {}
	~GroupModule() { for (auto m : children) delete m; }
	void AddChild(Module *m) { children.push_back(m); }

	float GetContentWidth(RenderContext &ctx) override
	{
		Style s = config.baseStyle;
		float totalW = 0.0f;
		totalW += s.padding.left;
		for (auto *child : children) {
			child->CalculateWidth(ctx);
			totalW += child->width;
		}
		totalW += s.padding.right;
		totalW += s.margin.left + s.margin.right;
		return totalW;
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
		float cursor = x + s.margin.left + s.padding.left;
		float topY = y + s.margin.top + s.padding.top;
		float childH = h - s.margin.top - s.margin.bottom - s.padding.top - s.padding.bottom;

		for (Module *child : children) {
			child->cachedRect = D2D1::RectF(cursor, topY, cursor + child->width, topY + childH);
			child->RenderContent(ctx, cursor, topY, child->width, childH);
			cursor += child->width;
		}
	}
};