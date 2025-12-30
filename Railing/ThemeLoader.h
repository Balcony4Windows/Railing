#pragma once
#include "ThemeTypes.h"
#include <fstream>
#include <sstream>
#include "Windows.h"

class ThemeLoader 
{
public:
	static ThemeConfig Load(const std::string &relativeFilename)
	{
		ThemeConfig config;
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(NULL, exePath, MAX_PATH);
		std::wstring path(exePath);
		path = path.substr(0, path.find_last_of(L"\\/"));
		std::wstring wFilename(relativeFilename.begin(), relativeFilename.end());
		std::wstring fullPath = path + L"\\" + wFilename;
		std::string finalPath(fullPath.begin(), fullPath.end());

		char debugBuf[512];
		sprintf_s(debugBuf, "Loading Config from: %s\n", finalPath.c_str());
		OutputDebugStringA(debugBuf);
		std::ifstream f(fullPath);
		if (!f.is_open()) {
			OutputDebugStringA("ERROR: File not found.\n");
			return config;
		}

		nlohmann::json j;
		try {
			f >> j;
		}
		catch (nlohmann::json::parse_error &e) {
			char buf[512];
			sprintf_s(buf, "ERROR: JSON Parse Error: %s\n", e.what());
			OutputDebugStringA(buf);
			return config; // Return empty
		}

		if (j.contains("global")) {
			auto &g = j["global"];
			config.global.height = g.value("height", 40);
			config.global.position = g.value("position", "top");
			config.global.font = g.value("font", "Segoe UI");
			config.global.fontSize = g.value("font_size", 14.0f);
			if (g.contains("margin")) config.global.margin = Padding::FromJSON(g["margin"]);
			if (g.contains("background")) config.global.background = ParseColor(g["background"]);
			if (g.contains("blur")) config.global.blur = g.value("blur", true);
			if (g.contains("radius")) config.global.radius = g.value("radius", 0.0f);
			if (g.contains("border_width")) config.global.borderWidth = g.value("border_width", 0.0f);
			if (g.contains("border_color")) config.global.borderColor = ParseColor(g["border_color"]);
			if (g.contains("animation")) {
				auto &a = g["animation"];
				config.global.animation.enabled = a.value("enabled", true);
				config.global.animation.duration = a.value("duration", 300);
				config.global.animation.startScale = a.value("start_scale", 0.1f);
				config.global.animation.fps = a.value("fps", 60);
			}
		}
		if (j.contains("layout")) {
			auto &l = j["layout"];
			if (l.contains("left")) config.layout.left = l["left"].get<std::vector<std::string>>();
			if (l.contains("center")) config.layout.center = l["center"].get<std::vector<std::string>>();
			if (l.contains("right")) config.layout.right = l["right"].get<std::vector<std::string>>();
		}

		for (auto &[key, val] : j.items()) {
			if (key == "global" || key == "layout") continue;

			ModuleConfig mod;
			mod.id = key;
			mod.type = val.value("type", "custom");
			mod.format = val.value("format", "");
			mod.interval = val.value("interval", 0);
			mod.orientation = val.value("orientation", "horizontal");
			mod.baseStyle = Style();
			if (val.contains("target")) mod.target = val["target"].get<std::string>();
			if (val.contains("style")) mod.baseStyle = ParseStyle(val["style"]);
			if (val.contains("item_style")) mod.itemStyle = ParseStyle(val["item_style"]);
			if (val.contains("modules")) mod.groupModules = val["modules"].get<std::vector<std::string>>();
			if (val.contains("thresholds")) {
				for (auto &t : val["thresholds"]) {
					Threshold th;
					th.val = t.value("val", 0);
					if (t.contains("style")) th.style = ParseStyle(t["style"]);
					mod.thresholds.push_back(th);
				}
			}
			if (val.contains("states")) {
				for (auto &[stateName, stateStyle] : val["states"].items()) mod.states[stateName] = ParseStyle(stateStyle);
			}

			config.modules[key] = mod;
		}
		return config;
	}

private:
	static D2D1_COLOR_F ParseColor(std::string hex)
	{
		if (hex == "transparent") return D2D1::ColorF(0, 0, 0, 0.0f);
		if (hex.empty() || hex[0] != '#') return D2D1::ColorF(D2D1::ColorF::Magenta);
		hex = hex.substr(1);
		unsigned int val = 0;
		try { val = std::stoul(hex, nullptr, 16); }
		catch (...) { return D2D1::ColorF(D2D1::ColorF::White); }

		float a = (hex.length() == 8) ? ((val >> 24) & 0xFF) / 255.0f : 1.0f;
		float r = ((val >> 16) & 0xFF) / 255.0f;
		float g = ((val >> 8) & 0xFF) / 255.0f;
		float b = (val & 0xFF) / 255.0f;
		return D2D1::ColorF(r, g, b, a);
	}
	static Style ParseStyle(const nlohmann::json &j)
	{
		Style s;
		if (j.contains("bg")) {
			s.bg = ParseColor(j["bg"]);
			s.has_bg = true;
		}
		if (j.contains("fg")) s.fg = ParseColor(j["fg"]);
		if (j.contains("radius")) s.radius = j["radius"];
		if (j.contains("padding")) s.padding = Padding::FromJSON(j["padding"]);
		if (j.contains("margin")) s.margin = Padding::FromJSON(j["margin"]);
		if (j.contains("font_weight")) s.font_weight = j["font_weight"];
		if (j.contains("border_width")) s.borderWidth = j.value("border_width", 0.0f);
		if (j.contains("border_color")) s.borderColor = ParseColor(j["border_color"]);
		return s;
	}
};