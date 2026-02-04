#pragma once
#include "ThemeTypes.h"
#include <fstream>
#include <sstream>
#include "Windows.h"
#include "Railing.h"

class ThemeLoader
{
public:
	static ThemeConfig CreateDefaultConfig() {
		ThemeConfig c;
		c.global.height = 45;
		c.global.position = "bottom";
		c.global.background = D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.95f);
		c.global.font = "Segoe UI";
		c.global.fontSize = 14.0f;
		c.global.margin = { 0,0,0,0 };
		c.global.radius = 0.0f;

		// Add a default clock so the user knows it's working
		ModuleConfig clock;
		clock.id = "default_clock";
		clock.type = "clock";
		clock.format = "%I:%M %p";
		clock.baseStyle.padding = { 10,0,10,0 };
		c.modules["default_clock"] = clock;
		c.layout.right.push_back("default_clock");

		return c;
	}

    static ThemeConfig Load(const std::string &filename)
    {
        // 1. Start with a guaranteed valid state
        ThemeConfig config = CreateDefaultConfig();

        // 2. Resolve Path Robustly (Fixes the C:/User/.../C:/User crash)
        std::filesystem::path inputPath(filename);
        std::filesystem::path finalPath;

        try {
            if (inputPath.is_absolute()) {
                finalPath = inputPath;
            }
            else {
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
                finalPath = exeDir / inputPath;
            }

            // Debug Output
            std::string pathStr = finalPath.string();
            char debugBuf[512];
            sprintf_s(debugBuf, "[Railing] Loading Config: %s\n", pathStr.c_str());
            OutputDebugStringA(debugBuf);

            if (!std::filesystem::exists(finalPath)) {
                OutputDebugStringA("[Railing] ERROR: Config file not found. Using defaults.\n");
                return config; // Return the safe default we created earlier
            }

            std::ifstream f(finalPath);
            nlohmann::json j;
            f >> j;

            // --- Parsing Global ---
            if (j.contains("global")) {
                auto &g = j["global"];
                // .value() is safe - it handles missing keys or wrong types by returning the default
                config.global.height = g.value("height", 40);
                config.global.position = g.value("position", "bottom");
                config.global.monitor = g.value("monitor", "primary");
                config.global.font = g.value("font", "Segoe UI");
                config.global.fontSize = g.value("font_size", 14.0f);
                config.global.blur = g.value("blur", true);
                config.global.highlights = ParseColor(g.value("highlights", "#FF6495ED"));
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

            // --- Parsing Layout ---
            // Clear default layout if we found a new one
            if (j.contains("layout")) {
                config.layout.left.clear();
                config.layout.center.clear();
                config.layout.right.clear();

                auto &l = j["layout"];
                if (l.contains("left") && l["left"].is_array())
                    config.layout.left = l["left"].get<std::vector<std::string>>();

                if (l.contains("center") && l["center"].is_array())
                    config.layout.center = l["center"].get<std::vector<std::string>>();

                if (l.contains("right") && l["right"].is_array())
                    config.layout.right = l["right"].get<std::vector<std::string>>();
            }

            // --- Parsing Modules ---
            // Clear default modules if we have new ones to load
            bool hasModules = false;

            for (auto &[key, val] : j.items()) {
                if (key == "global" || key == "layout" || key == "pinned") continue;

                if (!hasModules) {
                    config.modules.clear(); // Only clear defaults if we actually found a module definition
                    hasModules = true;
                }

                ModuleConfig mod;
                mod.id = key;
                mod.type = val.value("type", "custom");
                mod.format = val.value("format", "");
                mod.interval = val.value("interval", 1000);
                mod.position = val.value("position", config.global.position); // inherit global pos
                mod.latitude = val.value("latitude", "");
                mod.longitude = val.value("longitude", "");
                mod.tempFormat = val.value("temp_format", "fahrenheit");
                mod.tooltip = val.value("tooltip", "");

                if (val.contains("target") && val["target"].is_string())
                    mod.target = val.value("target", "");

                if (val.contains("on_click") && val["on_click"].is_string())
                    mod.onClick = val.value("on_click", "");

                // Dock Specifics
                if (val.contains("style")) {
                    auto &s = val["style"];
                    mod.baseStyle = ParseStyle(s);
                    mod.dockIconSize = s.value("icon_size", 24.0f);
                    mod.dockSpacing = s.value("spacing", 8.0f);
                    mod.dockAnimSpeed = s.value("anim_speed", 0.25f);
                }

                if (val.contains("item_style"))
                    mod.itemStyle = ParseStyle(val["item_style"]);

                if (val.contains("modules") && val["modules"].is_array())
                    mod.groupModules = val["modules"].get<std::vector<std::string>>();

                if (val.contains("icon") && val["icon"].is_string())
                    mod.icon = val.value("icon", "");

                if (val.contains("thresholds") && val["thresholds"].is_array()) {
                    for (auto &t : val["thresholds"]) {
                        Threshold th;
                        th.val = t.value("val", 0);
                        if (t.contains("style")) th.style = ParseStyle(t["style"]);
                        mod.thresholds.push_back(th);
                    }
                }

                if (val.contains("states") && val["states"].is_object()) {
                    for (auto &[stateName, stateStyle] : val["states"].items()) {
                        mod.states[stateName] = ParseStyle(stateStyle);
                    }
                }

                // Visualizer
                if (val.contains("visualizer")) {
                    auto &v = val["visualizer"];
                    mod.viz.numBars = v.value("bars", 20);
                    mod.viz.sensitivity = v.value("sensitivity", 2.0f);
                    mod.viz.decay = v.value("decay", 0.85f);
                    mod.viz.offset = v.value("offset", 0);
                    mod.viz.spacing = v.value("spacing", 2.0f);
                    mod.viz.thickness = v.value("thickness", 3.0f);
                }

                config.modules[key] = mod;
            }

            // --- Parsing Pinned Apps ---
            if (j.contains("pinned") && j["pinned"].is_array()) {
                config.pinnedPaths.clear();
                for (auto &p : j["pinned"]) {
                    if (p.is_string()) {
                        std::string pathStr = p.get<std::string>();
                        config.pinnedPaths.push_back(std::wstring(pathStr.begin(), pathStr.end()));
                    }
                }
            }

        }
        catch (const nlohmann::json::exception &e) {
            char buf[512];
            sprintf_s(buf, "[Railing] JSON Error: %s. Using default config.\n", e.what());
            OutputDebugStringA(buf);
            return CreateDefaultConfig(); // Fallback on parse error
        }
        catch (const std::exception &e) {
            char buf[512];
            sprintf_s(buf, "[Railing] General Error loading config: %s\n", e.what());
            OutputDebugStringA(buf);
            return CreateDefaultConfig(); // Fallback on general error
        }
        catch (...) {
            OutputDebugStringA("[Railing] Unknown error loading config.\n");
            return CreateDefaultConfig();
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