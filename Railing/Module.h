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
protected:
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