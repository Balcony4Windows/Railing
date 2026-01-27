#pragma once
#include "Module.h"
#include "SimpleFFT.h"
#include "AudioCapture.h"

class VisualizerModule : public Module
{
	AudioCapture *capture = nullptr;
	SimpleFFT fft;
	std::vector<float> lastFrequencies;
	std::vector<float> smoothFrequencies;

	float targetWidth = 200.0f;
public:
	VisualizerModule(const ModuleConfig &config, AudioCapture *sharedCapture)
		: Module(config) {
		this->capture = sharedCapture;
		smoothFrequencies.resize(config.viz.numBars, 0.0f);
	}

	float GetContentWidth(RenderContext &ctx) override {
		size_t count = smoothFrequencies.size();
		if (count == 0) return 0.0f;

		float totalContent = (count * config.viz.thickness) + ((count - 1) * config.viz.spacing);
		Style s = GetEffectiveStyle();
		return totalContent + s.padding.left + s.padding.right + s.margin.left + s.margin.right;
	}

	void Update() override {
		if (!capture) return;

		std::vector<float> rawAudio;
		capture->GetAudioData(rawAudio);
		if (rawAudio.empty()) return;

		std::vector<float> freqs;
		fft.Compute(rawAudio, freqs);

		int numBars = config.viz.numBars;
		if (numBars < 4) numBars = 4;

		if (smoothFrequencies.size() != numBars) smoothFrequencies.resize(numBars, 0.0f);

		int offset = config.viz.offset;
		// Safety check to prevent divide by zero or out of bounds
		int availableBins = (int)freqs.size() - offset;
		if (availableBins <= 0) availableBins = 1;

		int binSize = availableBins / numBars;
		if (binSize < 1) binSize = 1;

		float sensitivity = config.viz.sensitivity; // 1.0 is now "Standard"
		float decay = config.viz.decay;

		for (int i = 0; i < numBars; i++) {
			float avg = 0;
			int count = 0;
			for (int j = 0; j < binSize; j++) {
				int idx = offset + (i * binSize) + j;
				if (idx < freqs.size()) {
					avg += freqs[idx];
					count++;
				}
			}
			if (count > 0) avg /= count;

			float weighting = 1.0f + ((float)i / numBars) * 0.5f; // Slight treble boost
			float targetValue = avg * sensitivity * weighting;
			if (targetValue > smoothFrequencies[i]) {
				smoothFrequencies[i] = (smoothFrequencies[i] * 0.6f) + (targetValue * 0.4f);
			}
			else {
				smoothFrequencies[i] -= decay;
				if (smoothFrequencies[i] < 0) smoothFrequencies[i] = 0;
			}

			if (smoothFrequencies[i] > 1.0f) smoothFrequencies[i] = 1.0f;
		}
	}

	void RenderContent(RenderContext &ctx, float x, float y, float w, float h) override
	{
		if (smoothFrequencies.empty()) return;

		Style s = GetEffectiveStyle();

		// FIX: Use effective styling for background
		if (s.has_bg) {
			D2D1_RECT_F bgRect = D2D1::RectF(
				x + s.margin.left, y + s.margin.top,
				x + w - s.margin.right, y + h - s.margin.bottom);

			ctx.bgBrush->SetColor(s.bg);
			ctx.rt->FillRoundedRectangle(D2D1::RoundedRect(bgRect, s.radius, s.radius), ctx.bgBrush);
		}

		// Set Brush to Foreground Color (Red in your config)
		ctx.bgBrush->SetColor(s.fg); // Use s.fg instead of hardcoded/config logic for consistency
		if (config.states.count("default") && config.states.at("default").has_bg) {
			ctx.bgBrush->SetColor(config.states.at("default").bg);
		}

		// Start drawing content inside padding
		float startX = x + s.margin.left + s.padding.left;
		float drawY = y + s.margin.top + s.padding.top;
		float drawH = h - s.margin.top - s.margin.bottom - s.padding.top - s.padding.bottom;

		float barSpacing = config.viz.spacing;

		for (size_t i = 0; i < smoothFrequencies.size(); i++) {
			float val = smoothFrequencies[i];
			if (val > 1.0f) val = 1.0f;

			float barH = val * drawH;

			// Min height logic
			if (barH < 1.0f && val > 0.001f) barH = 1.0f;

			if (barH >= 1.0f) {
				float barY = drawY + drawH - barH;

				// FIX: Simple incremental positioning
				// x + (i * (Width + Gap))
				float barX = startX + (i * (config.viz.thickness + barSpacing));

				D2D1_RECT_F rect = D2D1::RectF(barX, barY, barX + config.viz.thickness, drawY + drawH);
				ctx.rt->FillRectangle(rect, ctx.bgBrush);
			}
		}
	}
};