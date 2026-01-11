#pragma once
#include "ThemeTypes.h"
#include <fstream>
#include <sstream>
#include "Windows.h"
#include "Railing.h"

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
			config.global.monitor = g.value("monitor", "primary");
			config.global.font = g.value("font", "Segoe UI");
			config.global.fontSize = g.value("font_size", 14.0f);
			config.global.blur = g.value("blur", true);
			config.global.autoHide = g.value("auto_hide", false);
			config.global.autoHideDelay = g.value("hide_delay", 500);

			if (g.contains("animation")) {
				auto &a = g["animation"];
				config.global.animation.enabled = a.value("enabled", true);
				config.global.animation.duration = a.value("duration", 300);
				config.global.animation.startScale = a.value("start_scale", 0.1f);
				config.global.animation.fps = a.value("fps", 60);
			}
			if (g.contains("style")) {
				Style s = ParseStyle(g["style"]);
				if (s.has_bg) config.global.background = s.bg;
				if (s.has_radius) config.global.radius = s.radius;
				if (s.has_margin) config.global.margin = s.margin;
				if (s.has_border) {
					config.global.borderWidth = s.borderWidth;
					config.global.borderColor = s.borderColor;
				}
			}
		}
		if (j.contains("layout")) {
			auto &l = j["layout"];
			if (l.contains("left")) config.layout.left = l["left"].get<std::vector<std::string>>();
			if (l.contains("center")) config.layout.center = l["center"].get<std::vector<std::string>>();
			if (l.contains("right")) config.layout.right = l["right"].get<std::vector<std::string>>();
		}

		for (auto &[key, val] : j.items()) {
			if (key == "global" || key == "layout" || key == "pinned") continue;

			ModuleConfig mod;
			mod.id = key;
			mod.type = val.value("type", "custom");
			mod.format = val.value("format", "");
			mod.interval = val.value("interval", 0);
			mod.orientation = val.value("orientation", "horizontal");
			mod.latitude = val.value("latitude", "");
			mod.longitude = val.value("longitude", "");
			mod.tempFormat = val.value("temp_format", "fahrenheit");
			mod.baseStyle = Style();
			if (val.contains("target")) mod.target = val["target"].get<std::string>();
			if (val.contains("on_click")) mod.onClick = val["on_click"].get<std::string>();
			if (val.contains("style")) {
				auto &s = val["style"];
				mod.baseStyle = ParseStyle(s);
				mod.dockIconSize = s.value("icon_size", 24.0f);
				mod.dockSpacing = s.value("spacing", 8.0f);
				mod.dockAnimSpeed = s.value("anim_speed", 0.25f);
			}
			if (val.contains("item_style")) mod.itemStyle = ParseStyle(val["item_style"]);
			if (val.contains("modules")) mod.groupModules = val["modules"].get<std::vector<std::string>>();
			if (val.contains("icon")) mod.icon = val["icon"].get<std::string>();
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

		if (j.contains("pinned") && j["pinned"].is_array()) {
			for (auto &p : j["pinned"]) {
				std::string pathStr = p.get<std::string>();
				config.pinnedPaths.push_back(std::wstring(pathStr.begin(), pathStr.end()));
			}
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
		Style s; // Handle cascading
		if (j.contains("bg")) { s.bg = ParseColor(j["bg"]); s.has_bg = true; }
		if (j.contains("fg")) { s.fg = ParseColor(j["fg"]); s.has_fg = true; }
		if (j.contains("radius")) { s.radius = j["radius"]; s.has_radius = true; }
		if (j.contains("padding")) { s.padding = Padding::FromJSON(j["padding"]); s.has_padding = true; }
		if (j.contains("margin")) { s.margin = Padding::FromJSON(j["margin"]); s.has_margin = true; }
		if (j.contains("font_weight")) { s.font_weight = j["font_weight"]; s.has_font_weight = true; }
		if (j.contains("border_width")) { s.borderWidth = j.value("border_width", 0.0f); s.has_border = true; }
		if (j.contains("border_color")) s.borderColor = ParseColor(j["border_color"]);
		if (j.contains("indicator")) s.indicator = ParseColor(j["indicator"]);
		return s;
	}
};