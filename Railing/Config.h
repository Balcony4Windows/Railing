// Config.h
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <string>

struct AppConfig {
    // Layout & Dimensions
    int barHeight = 48;
    int screenMargin = 12;
    float scale = 1.0f;
    float moduleGap = 10.0f;
    float innerPadding = 8.0f;

    // Decoration
    float rounding = 6.0f;
    float barOpacity = 0.75f;
    float pillOpacity = 0.3f;
    bool enableBlur = true;

    // Typography
    std::wstring fontFamily = L"JetBrains Mono";
    float fontSize = 14.0f;
    float iconSize = 16.0f;

    // Colors
    D2D1_COLOR_F barBackgroundColor = D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.75f);
    D2D1_COLOR_F accentColor = D2D1::ColorF(D2D1::ColorF::Cyan);
    D2D1_COLOR_F textColor = D2D1::ColorF(D2D1::ColorF::White);
    D2D1_COLOR_F urgentColor = D2D1::ColorF(D2D1::ColorF::Red);
    D2D1_COLOR_F inactiveTextColor = D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f);

    // Input / Misc
    bool use24HourTime = false;

    static D2D1_COLOR_F FromHex(std::wstring hex, float alpha = 1.0f) {
        if (hex.empty() || hex[0] != '#') return D2D1::ColorF(D2D1::ColorF::White);
        hex = hex.substr(1);
        unsigned int rgb = 0;
        try { rgb = std::stoul(hex, nullptr, 16); }
        catch (...) { return D2D1::ColorF(D2D1::ColorF::White); }
        float r = ((rgb >> 16) & 0xFF) / 255.0f;
        float g = ((rgb >> 8) & 0xFF) / 255.0f;
        float b = (rgb & 0xFF) / 255.0f;
        return D2D1::ColorF(r, g, b, alpha);
    }
};

class ConfigLoader {
public:
    static AppConfig Load();
};

static inline DWORD D2D1ColorFToBlurColor(const D2D1_COLOR_F &c)
{
    BYTE a = (BYTE)(c.a * 255.0f + 0.5f);
    BYTE r = (BYTE)(c.r * 255.0f + 0.5f);
    BYTE g = (BYTE)(c.g * 255.0f + 0.5f);
    BYTE b = (BYTE)(c.b * 255.0f + 0.5f);
    return (DWORD)((a << 24) | (b << 16) | (g << 8) | r);
}