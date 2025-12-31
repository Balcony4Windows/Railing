#pragma once
#include <string>
#include <unordered_map>
#include <d2d1.h>

struct Padding {
    float top = 0, right = 0, bottom = 0, left = 0;
};

struct Margin {
    float top = 0, right = 0, bottom = 0, left = 0;
};

struct Animation {
    float durationMs = 0;
    std::string easing = "linear";
};

struct ModuleStyle {
    D2D1_COLOR_F bg = D2D1::ColorF(0, 0, 0, 0);
    bool hasBg = false;
    float borderWidth = 0;
    D2D1_COLOR_F borderColor = D2D1::ColorF(D2D1::ColorF::White);
    float radius = 0;
    Padding padding;
    Margin margin;
    std::string fontFamily;
    float fontSize = 12;
    Animation anim;
    std::unordered_map<std::string, std::string> extraProperties; // For custom blur, etc
};

struct GlobalStyle : public ModuleStyle {
    std::string blurType = "none"; // none, blur, acrylic
    float blurOpacity = 1.0f; // for acrylic
    float height = 32;
    std::string position = "top"; // top, bottom, left, right
};
