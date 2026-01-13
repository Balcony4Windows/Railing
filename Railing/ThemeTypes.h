#pragma once
#include <string>
#include <vector>
#include <map>
#include <d2d1.h>
#include <d2d1_1.h>
#include <nlohmann/json.hpp>
#include "Types.h"

struct Padding {
    float top = 0, right = 0, bottom = 0, left = 0;

    // Helper to parse [val] or [vertical, horizontal] or [t, r, b, l]
    static Padding FromJSON(const nlohmann::json &j) {
        Padding p;
        if (j.is_array()) {
            if (j.size() == 1) { p.top = p.bottom = p.left = p.right = j[0]; }
            else if (j.size() == 2) { p.top = p.bottom = j[0]; p.left = p.right = j[1]; }
            else if (j.size() == 4) { p.top = j[0]; p.right = j[1]; p.bottom = j[2]; p.left = j[3]; }
        }
        else if (j.is_number()) {
            p.top = p.bottom = p.left = p.right = j.get<float>();
        }
        return p;
    }
};

struct Style {
    D2D1_COLOR_F bg = D2D1::ColorF(0, 0, 0, 0); // Default Transparent
    D2D1_COLOR_F fg = D2D1::ColorF(1, 1, 1, 1); // Default White
    D2D1_COLOR_F indicator = { 0, 0, 0, 0 };
    float radius = 0.0f;
    Padding padding;
    Padding margin;
    std::string font_weight = "normal";
    float borderWidth = 0.0f;
    D2D1_COLOR_F borderColor = D2D1::ColorF(0, 0, 0, 0);
    bool has_bg = false;
    bool has_fg = false;
    bool has_radius = false;
    bool has_padding = false;
    bool has_margin = false;
    bool has_border = false;
    bool has_font_weight = false;

    Style Merge(const Style &other) const {
        Style result = *this;
        if (other.has_bg) { result.bg = other.bg; result.has_bg = true; }
        if (other.has_fg) { result.fg = other.fg; result.has_fg = true; }
        if (other.has_radius) { result.radius = other.radius; result.has_radius = true; }
        if (other.has_padding) { result.padding = other.padding; result.has_padding = true; }
        if (other.has_margin) { result.margin = other.margin; result.has_margin = true; }
        if (other.has_border) {
            result.borderWidth = other.borderWidth;
            if (other.borderColor.a > 0) result.borderColor = other.borderColor;
            result.has_border = true;
        }
        if (other.has_font_weight) { result.font_weight = other.font_weight; result.has_font_weight = true; }
        return result;
    }
};

struct Threshold {
    int val;
    Style style;
};
// Used in VisualizerModule:
struct VisualizerSettings {
    int numBars = 32; // Number of bars to display
    float sensitivity = 4.0f; // Multiplier (Gain)
    float decay = 0.05f; // Fall speed (Gravity)
    int offset = 2; // Bins to skip (Low frequency rumble)
    float spacing = 2.0f; // Space between bars
};

// This struct is a "Superset" that can hold data for ANY module type.
struct ModuleConfig {
    std::string id; // e.g., "cpu", "workspaces"
    std::string type; // e.g., "cpu", "group", "clock"
    std::string format; // e.g., "{icon} {vol}%"
    std::string onClick; // e.g., "open my app"
    std::string target; // For ping module specifically
    std::string icon; // icon location or glyph
    int interval = 0;

    Style baseStyle;
    VisualizerSettings viz;

    std::string latitude; /* For weather */
    std::string longitude;
    std::string tempFormat = "fahrenheit";

    std::vector<std::string> groupModules; // If type == "group"
    std::vector<Threshold> thresholds; // If type == "cpu" or "battery"
    std::map<std::string, Style> states; // If type == "workspaces" (active/urgent)
    Style itemStyle; // For workspace buttons
    std::string orientation = "horizontal";

    float dockIconSize = 24.0f;
    float dockSpacing = 8.0f;
    float dockAnimSpeed = 0.25f;
};

// The Root Configuration
struct ThemeConfig {
    struct Animation {
        bool enabled = true;
        int duration = 300;
        float startScale = 0.1f;
        int fps = 60;
    };
    struct Global {
        int height = 40;
        std::string position = "top";
        Padding margin;
        std::string font = "Segoe UI";
        std::string monitor = "primary";
        bool autoHide = false;
        int autoHideDelay = 500;
        float fontSize = 14.0f;
        D2D1_COLOR_F background = D2D1::ColorF(0, 0, 0, 0);
        bool blur = true;

        float radius = 0.0f;
        float borderWidth = 0.0f;
        D2D1_COLOR_F borderColor = D2D1::ColorF(0, 0, 0, 0);
        Animation animation;
    } global;

    struct Layout {
        std::vector<std::string> left;
        std::vector<std::string> center;
        std::vector<std::string> right;
    } layout;

    std::map<std::string, ModuleConfig> modules;
    std::vector<std::wstring> pinnedPaths;
};