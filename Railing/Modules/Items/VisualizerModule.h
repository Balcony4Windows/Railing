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
		return targetWidth;
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

		float barSpacing = config.viz.spacing;
		float totalSpacing = (smoothFrequencies.size() - 1) * barSpacing;
		float barWidth = (w - totalSpacing) / smoothFrequencies.size();

		ctx.bgBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Cyan));
		if (config.states.count("default") && config.states.at("default").has_bg) {
			ctx.bgBrush->SetColor(config.states.at("default").bg);
		}

		for (size_t i = 0; i < smoothFrequencies.size(); i++) {
			float val = smoothFrequencies[i];
			if (val > 1.0f) val = 1.0f;

			float barH = val * h;
			if (barH < 1.0f && val > 0.001f) barH = 1.0f;
			else if (barH < 0.1f) continue;
			float barY = y + h - barH;
			float barX = x + i * (barWidth + barSpacing);
			if (barH < 1.0f) continue;

			D2D1_RECT_F rect = D2D1::RectF(barX, barY, barX + barWidth, y + h);  
			ctx.rt->FillRectangle(rect, ctx.bgBrush);
		}
	}
};