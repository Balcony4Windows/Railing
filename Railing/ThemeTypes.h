#pragma once
#include <string>
#include <vector>
#include <map>
#include <d2d1.h>
#include <nlohmann/json.hpp>

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

// Style Definition (Background, Radius, Margin, etc.)
struct Style {
    D2D1_COLOR_F bg = D2D1::ColorF(0, 0, 0, 0); // Default Transparent
    D2D1_COLOR_F fg = D2D1::ColorF(1, 1, 1, 1); // Default White
    float radius = 0.0f;
    Padding padding;
    Padding margin;
    std::string font_weight = "normal";

	float borderWidth = 0.0f;
	D2D1_COLOR_F borderColor = D2D1::ColorF(0, 0, 0, 0);
    bool has_bg = false; // Flag to know if we should draw BG or not
};

// Conditional Thresholds (for CPU/Battery colors)
struct Threshold {
    int val;
    Style style;
};

// This struct is a "Superset" that can hold data for ANY module type.
struct ModuleConfig {
    std::string id; // e.g., "cpu", "workspaces"
    std::string type; // e.g., "cpu", "group", "clock"
    std::string format; // e.g., "{icon} {vol}%"
    int interval = 0;

    Style baseStyle;

    std::vector<std::string> groupModules; // If type == "group"
    std::vector<Threshold> thresholds; // If type == "cpu" or "battery"
    std::map<std::string, Style> states; // If type == "workspaces" (active/urgent)
    Style itemStyle; // For workspace buttons
    std::string orientation = "horizontal";
};

// The Root Configuration
struct ThemeConfig {
    struct Global {
        int height = 40;
        std::string position = "top";
        Padding margin;
        std::string font = "Segoe UI";
        float fontSize = 14.0f;
        D2D1_COLOR_F background = D2D1::ColorF(0, 0, 0, 0);
        bool blur = true;

        float radius = 0.0f;
        float borderWidth = 0.0f;
        D2D1_COLOR_F borderColor = D2D1::ColorF(0, 0, 0, 0);
    } global;

    struct Layout {
        std::vector<std::string> left;
        std::vector<std::string> center;
        std::vector<std::string> right;
    } layout;

    std::map<std::string, ModuleConfig> modules;
};