#pragma once
#include "RenderContext.h"

class Workspaces {
public:
    Workspaces();
    ~Workspaces();

    void SetActiveIndex(int index) {
        if (index >= 0 && index < count) {
            activeIndex = index;
        }
    }

    void Draw(const RenderContext &ctx);
    D2D1_RECT_F GetRect() const { return bounds; }
    int GetCount() const { return count; }

    float itemWidth = 50.0f;
    float itemHeight = 29.0f;
    float padding = 5.0f;

private:
    // State
    int activeIndex = 0;
    int count = 4;

    // Layout
    D2D1_RECT_F bounds = {};

    // Resources
    ID2D1SolidColorBrush *pActiveBrush = nullptr;
    ID2D1SolidColorBrush *pTextBrush = nullptr;
    ID2D1SolidColorBrush *pHoverBrush = nullptr;

    // Helpers
    int GetActiveVirtualDesktop(); // Returns 0, 1, 2...
};

class Clock {
    int lastMinute = -1;
    IDWriteTextLayout *pTextLayout = nullptr;
    D2D1_RECT_F lastRect = {};
public:
    D2D1_RECT_F GetRect() const { return lastRect; }
    void Draw(const RenderContext &ctx);
    ~Clock() { pTextLayout->Release(); }
};
