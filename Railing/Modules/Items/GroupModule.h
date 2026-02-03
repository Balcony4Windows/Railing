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
		bool isVertical = (config.position == "left" || config.position == "right");
		Style s = config.baseStyle;

		float totalSize = 0.0f;
		totalSize += isVertical ? s.padding.top : s.padding.left;

		for (auto *child : children) {
			child->config.position = config.position;

			child->CalculateWidth(ctx);
			totalSize += child->width;
		}

		totalSize += isVertical ? s.padding.bottom : s.padding.right;
		totalSize += isVertical ? (s.margin.top + s.margin.bottom) : (s.margin.left + s.margin.right);

		return totalSize;
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		bool isVertical = (config.position == "left" || config.position == "right");
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

		if (isVertical) {
			float cursor = y + s.margin.top + s.padding.top;
			float fixedX = x + s.margin.left + s.padding.left;
			float childW = w - s.margin.left - s.margin.right - s.padding.left - s.padding.right;

			for (Module *child : children) {
				child->cachedRect = D2D1::RectF(fixedX, cursor, fixedX + childW, cursor + child->width);
				child->RenderContent(ctx, fixedX, cursor, childW, child->width);
				cursor += child->width;
			}
		}
		else {
			float cursor = x + s.margin.left + s.padding.left;
			float fixedY = y + s.margin.top + s.padding.top;
			float childH = h - s.margin.top - s.margin.bottom - s.padding.top - s.padding.bottom;

			for (Module *child : children) {
				child->cachedRect = D2D1::RectF(cursor, fixedY, cursor + child->width, fixedY + childH);
				child->RenderContent(ctx, cursor, fixedY, child->width, childH);
				cursor += child->width;
			}
		}
	}
};