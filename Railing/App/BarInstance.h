#pragma once
#include <Windows.h>
#include <string>
#include <memory>
#include <dwmapi.h>
#include "ThemeLoader.h"
#include "TooltipHandler.h"
#include "WorkspaceManager.h"
#include "Types.h"

class RailingRenderer;
class InputManager;
class VolumeFlyout;
class TrayFlyout;
class NetworkFlyout;
struct IDropTarget;

enum class InteractionMode {
    None = 0,
    Resize,
    Move,
};

class BarInstance {
public:
    BarInstance(const std::string &configFile);
    ~BarInstance();

    bool Initialize(HINSTANCE hInstance, bool makePrimary = false);
    void Reposition();
    void ReloadConfig();
    void SaveState();
    void UpdateStats(const SystemStatusData &stats);

    HWND GetHwnd() const { return hwnd; }
    bool IsPrimary() const { return isPrimary; }

    WorkspaceManager workspaces;

    // Flyouts
    VolumeFlyout *flyout = nullptr;
    TrayFlyout *trayFlyout = nullptr;
    NetworkFlyout *networkFlyout = nullptr;

    std::string configFileName;
    ThemeConfig config;

    HWND hwnd = nullptr;
    bool isPrimary = false;

    RailingRenderer *renderer = nullptr;
    std::unique_ptr<InputManager> inputManager;
    TooltipHandler tooltips;

    // Auto-hide state
    float showProgress = 1.0f;
    bool isHidden = false;
    InteractionMode interactionMode = InteractionMode::None;
    ULONGLONG lastInteractionTime = 0;
    int tickCount = 0;

    IDropTarget *pDropTarget = nullptr;

    HWND CreateBarWindow(HINSTANCE hInstance, bool makePrimary);
    void OnTimerTick();
    bool IsMouseAtEdge();

    static LRESULT CALLBACK BarWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    friend class Railing;
};
