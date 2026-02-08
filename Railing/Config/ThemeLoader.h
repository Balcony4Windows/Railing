#pragma once
#include "ThemeTypes.h"
#include <fstream>
#include <sstream>
#include <filesystem>
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
        ThemeConfig config = CreateDefaultConfig();
        std::filesystem::path finalPath = ResolvePath(filename);

        char debugBuf[512];
        sprintf_s(debugBuf, "[Railing] Loading Config: %s\n", finalPath.string().c_str());
        OutputDebugStringA(debugBuf);

        if (!std::filesystem::exists(finalPath)) {
            OutputDebugStringA("[Railing] ERROR: Config file not found. Using defaults.\n");
            return config;
        }

        try {
            std::ifstream f(finalPath);
            nlohmann::json j;
            f >> j;

            // --- Parsing Global ---
            if (j.contains("global")) {
                auto &g = j["global"];
                config.global.height = g.value("height", 40);
                config.global.position = g.value("position", "bottom");
                config.global.monitor = g.value("monitor", "primary");
                config.global.font = g.value("font", "Segoe UI");
                config.global.fontSize = g.value("font_size", 14.0f);
                config.global.blur = g.value("blur", true);
                config.global.highlights = ParseColor(g.value("highlights", "#FF6495ED"));
                config.global.autoHide = g.value("auto_hide", false);

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
            bool hasModules = false;
            for (auto &[key, val] : j.items()) {
                if (key == "global" || key == "layout" || key == "pinned") continue;

                if (!hasModules) {
                    config.modules.clear();
                    hasModules = true;
                }

                ModuleConfig mod;
                mod.id = key;
                mod.type = val.value("type", "custom");
                mod.format = val.value("format", "");
                mod.interval = val.value("interval", 1000);
                mod.position = val.value("position", config.global.position);
                mod.latitude = val.value("latitude", "");
                mod.longitude = val.value("longitude", "");
                mod.tempFormat = val.value("temp_format", "fahrenheit");
                mod.tooltip = val.value("tooltip", "");
                mod.target = val.value("target", "");
                mod.onClick = val.value("on_click", "");
                mod.icon = val.value("icon", "");

                if (val.contains("style")) {
                    auto &s = val["style"];
                    mod.baseStyle = ParseStyle(s);
                    mod.dockIconSize = s.value("icon_size", 24.0f);
                    mod.dockSpacing = s.value("spacing", 8.0f);
                    mod.dockAnimSpeed = s.value("anim_speed", 0.25f);
                }

                if (val.contains("item_style"))
                    mod.itemStyle = ParseStyle(val["item_style"]);

                if (val.contains("states") && val["states"].is_object()) {
                    for (auto &[stateName, stateVal] : val["states"].items()) {
                        mod.states[stateName] = ParseStyle(stateVal);
                    }
                }

                if (val.contains("modules") && val["modules"].is_array())
                    mod.groupModules = val["modules"].get<std::vector<std::string>>();

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
        catch (...) {
            OutputDebugStringA("[Railing] Error parsing JSON. Defaults loaded.\n");
            return CreateDefaultConfig();
        }

        return config;
    }

    static void Save(const std::string &filename, const ThemeConfig &config)
    {
        nlohmann::json j;

        // --- Global ---
        j["global"]["height"] = config.global.height;
        j["global"]["position"] = config.global.position;
        j["global"]["monitor"] = config.global.monitor;
        j["global"]["font"] = config.global.font;
        j["global"]["font_size"] = config.global.fontSize;
        j["global"]["blur"] = config.global.blur;
        j["global"]["auto_hide"] = config.global.autoHide;
        j["global"]["highlights"] = ColorToHex(config.global.highlights);

        j["global"]["animation"]["enabled"] = config.global.animation.enabled;
        j["global"]["animation"]["duration"] = config.global.animation.duration;
        j["global"]["animation"]["start_scale"] = config.global.animation.startScale;
        j["global"]["animation"]["fps"] = config.global.animation.fps;

        nlohmann::json globalStyle;
        globalStyle["bg"] = ColorToHex(config.global.background);
        globalStyle["radius"] = config.global.radius;
        globalStyle["margin"] = PaddingToJson(config.global.margin);
        if (config.global.borderWidth > 0) {
            globalStyle["border_width"] = config.global.borderWidth;
            globalStyle["border_color"] = ColorToHex(config.global.borderColor);
        }
        j["global"]["style"] = globalStyle;

        // --- Layout ---
        j["layout"]["left"] = config.layout.left;
        j["layout"]["center"] = config.layout.center;
        j["layout"]["right"] = config.layout.right;

        // --- Pinned ---
        std::vector<std::string> pinned;
        for (const auto &path : config.pinnedPaths)
            pinned.push_back(std::string(path.begin(), path.end()));
        j["pinned"] = pinned;

        // --- Modules ---
        for (const auto &[id, mod] : config.modules)
        {
            nlohmann::json m;
            m["type"] = mod.type;
            m["format"] = mod.format;
            m["interval"] = mod.interval;
            m["position"] = mod.position;
            m["latitude"] = mod.latitude;
            m["longitude"] = mod.longitude;
            m["temp_format"] = mod.tempFormat;
            m["tooltip"] = mod.tooltip;
            m["target"] = mod.target;
            m["on_click"] = mod.onClick;
            m["icon"] = mod.icon;

            // --- Base Style ---
            nlohmann::json styleJson = StyleToJson(mod.baseStyle);
            if (!styleJson.empty()) {
                styleJson["icon_size"] = mod.dockIconSize;
                styleJson["spacing"] = mod.dockSpacing;
                styleJson["anim_speed"] = mod.dockAnimSpeed;
                m["style"] = styleJson;
            }

            m["item_style"] = StyleToJson(mod.itemStyle);

            if (!mod.states.empty()) {
                nlohmann::json statesJson;
                for (const auto &[stateName, style] : mod.states) {
                    statesJson[stateName] = StyleToJson(style);
                }
                m["states"] = statesJson;
            }

            if (!mod.groupModules.empty()) {
                m["modules"] = mod.groupModules;
            }

            j[id] = m;
        }

        // --- Write ---
        std::filesystem::path finalPath = ResolvePath(filename);
        std::ofstream o(finalPath);
        if (o.is_open()) {
            o << j.dump(4);
            o.close();
            OutputDebugStringA("[Railing] Config successfully saved.\n");
        }
    }


private:
    static std::filesystem::path ResolvePath(const std::string &filename) {
        std::filesystem::path inputPath(filename);
        if (inputPath.is_absolute()) return inputPath;

        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        return exeDir / inputPath;
    }

    // --- Helpers for Serialization ---

    static std::string ColorToHex(D2D1_COLOR_F c) {
        char buf[10];
        // Output format: #AARRGGBB
        int a = (int)(c.a * 255.0f);
        int r = (int)(c.r * 255.0f);
        int g = (int)(c.g * 255.0f);
        int b = (int)(c.b * 255.0f);
        sprintf_s(buf, "#%02X%02X%02X%02X", a, r, g, b);
        return std::string(buf);
    }

    static nlohmann::json PaddingToJson(const Padding &p) {
        return { p.left, p.top, p.right, p.bottom };
    }

    static nlohmann::json StyleToJson(const Style &s) {
        nlohmann::json j;
        if (s.has_bg) j["bg"] = ColorToHex(s.bg);
        if (s.has_fg) j["fg"] = ColorToHex(s.fg);
        if (s.has_radius) j["radius"] = s.radius;
        if (s.has_padding) j["padding"] = PaddingToJson(s.padding);
        if (s.has_margin) j["margin"] = PaddingToJson(s.margin);
        if (s.has_border) {
            j["border_width"] = s.borderWidth;
            j["border_color"] = ColorToHex(s.borderColor);
        }
        return j;
    }

    // --- Helpers for Deserialization (Existing) ---

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