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
	IDWriteTextLayout *cachedLayout = nullptr;
	std::wstring cachedTextVal = L"";

	Module(const ModuleConfig &cfg) : config(cfg) {}
	virtual ~Module() { if (cachedLayout) cachedLayout->Release(); };

	IDWriteTextLayout *GetLayout(RenderContext &ctx, const std::wstring &text, IDWriteTextFormat *format) {
		if (!cachedLayout || text != cachedTextVal) {
			if (cachedLayout) { cachedLayout->Release(); cachedLayout = nullptr; }
			cachedTextVal = text;
			ctx.writeFactory->CreateTextLayout(
				text.c_str(), (UINT32)text.length(),
				format, 1000.0f, 1000.0f, &cachedLayout
			);
		}
		return cachedLayout;
	}

	static bool HasType(ThemeConfig cfg, std::string type)
	{
		for (const auto &[id, moduleConfig] : cfg.modules) {
			if (moduleConfig.type == type) return true;
		}
		return false;
	}

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

	// Helper to format wider characters
	static inline std::wstring Utf8ToWide(const std::string &str) {
		if (str.empty()) return L"";
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
		std::wstring wstrTo(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
		return wstrTo;
	}

	// Helper to format output strings
	static inline std::wstring FormatOutput(std::string fmt, std::string token, std::wstring value) {
		if (fmt.empty()) return value;

		if (fmt.empty()) return L"";
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, &fmt[0], (int)fmt.size(), NULL, 0);
		std::wstring wfmt(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, &fmt[0], (int)fmt.size(), &wfmt[0], size_needed);

		std::wstring wtoken(token.begin(), token.end());

		size_t pos = wfmt.find(wtoken);
		if (pos != std::wstring::npos) {
			wfmt.replace(pos, wtoken.length(), value);
		}
		return wfmt;
	}

	// Helper to draw a progress bar background
	static inline void DrawProgressBar(RenderContext &ctx, float x, float y, float w, float h, float pct, const D2D1_COLOR_F color)
	{
		if (pct > 0.0f) {
			if (pct > 1.0f) pct = 1.0f;
			D2D1_RECT_F fillRect = D2D1::RectF(x, y + h - 4.0f, x + (w * pct), y + h);
			D2D1_ROUNDED_RECT roundedBar = D2D1::RoundedRect(fillRect, 2.0f, 2.0f);
			ctx.bgBrush->SetColor(color);
			ctx.rt->FillRoundedRectangle(roundedBar, ctx.bgBrush);
		}
	}
};