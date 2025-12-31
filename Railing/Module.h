#pragma once
#include "ThemeTypes.h"
#include "RenderContext.h"
class Module
{
public:
	ModuleConfig config;
	float width = 0.0f;
	float height = 0.0f;
	D2D1_RECT_F cachedRect = {};
	bool isHovered = false;

	Module(const ModuleConfig &cfg) : config(cfg) {}
	virtual ~Module() {};

	/// <summary>
	/// Update Logic (Run ever frame, e.g. check CPU usage)
	/// </summary>
	virtual void Update() {}

	/// <summary>
	/// Measure (calculate) the content width of the module.
	/// </summary>
	/// <param name="ctx">Context</param>
	/// <returns></returns>
	virtual float GetContentWidth(RenderContext &ctx) = 0;

	/// <summary>
	/// Draw, the main render loop.
	/// </summary>
	/// <param name="ctx">Context</param>
	/// <param name="x"></param>
	/// <param name="y"></param>
	/// <param name="h"></param>
	virtual void Draw(RenderContext &ctx, float x, float y, float constraintSize);

	/// <summary>
	/// Measure content width.
	/// </summary>
	/// <param name="ctx">Context</param>
	void CalculateWidth(RenderContext &ctx);

	/// <summary>
	/// Helper method to get effective style (base + hover)
	/// </summary>
	/// <returns>style of *this* module</returns>
	Style GetEffectiveStyle() {
		Style s = config.baseStyle;
		if (isHovered && config.states.count("hover")) {
			s = s.Merge(config.states.at("hover"));
		}
		return s;
	}

	void DrawModuleBackground(RenderContext &ctx, D2D1_RECT_F rect, const Style &s) {
		if (!s.has_bg && !s.has_border) return;
		D2D1_RECT_F drawRect = D2D1::RectF(
			rect.left + s.margin.left,
			rect.top + s.margin.top,
			rect.right - s.margin.right,
			rect.bottom - s.margin.bottom
		);
		D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(drawRect, s.radius, s.radius);
		if (s.has_bg) {
			ctx.bgBrush->SetColor(s.bg);
			ctx.rt->FillRoundedRectangle(rounded, ctx.bgBrush);
		}
		if (s.has_border) {
			float strokeHalf = s.borderWidth / 2.0f;
			D2D1_ROUNDED_RECT borderShape = rounded;
			borderShape.rect.left += strokeHalf;
			borderShape.rect.top += strokeHalf;
			borderShape.rect.right -= strokeHalf;
			borderShape.rect.bottom -= strokeHalf;

			ctx.borderBrush->SetColor(s.borderColor);
			ctx.rt->DrawRoundedRectangle(borderShape, ctx.borderBrush, s.borderWidth);
		}
	}

	/// <summary>
	/// Child classes implement this to draw text/icons.
	/// </summary>
	/// <param name="ctx">Context</param>
	/// <param name="x">x Position</param>
	/// <param name="y">y Position</param>
	/// <param name="w">Width</param>
	/// <param name="h">Height</param>
	virtual void RenderContent(RenderContext &ctx, float x, float y, float w, float h) = 0;
};