#pragma once

#include "balcony/components/Desktop.h"
#include "balcony/components/Taskbar.h"
#include "balcony/core/SystemMonitors.h"
#include "balcony/lua/LuaRuntime.h"
#include "balcony/persistence/Persistence.h"
#include "balcony/ui/Text.h"
#include "balcony/ui/Tooltip.h"

namespace balcony::renderer
{
    class Renderer;
}

namespace balcony::windows
{
    class Window;
}

// Owns and wires together the DE's top-level components (desktop,
// taskbar, ...) plus the small amount of glue between them -- e.g. the
// desktop's right-click "Quit Balcony" menu. main.cpp just creates one
// of these, forwards window/render-loop events to it, and stays
// boilerplate; this is where actual composition lives.
//
// This is a hand-written stand-in for what a Lua-driven composition
// layer will eventually do -- see CLAUDE.md sections 2, 3, and 7.
namespace balcony::components
{
    class DesktopEnvironment
    {
    public:
        void Initialize(balcony::windows::Window& window, balcony::renderer::Renderer& renderer);

        void Update(float deltaSeconds);
        void Draw(balcony::renderer::PrimitiveRenderer& primitives);

        // Forward these directly from Window's mouse callbacks; the
        // coordinates are already client pixels.
        void HandleRightClick(int x, int y);
        void HandleLeftClick(int x, int y);
        void HandleLeftButtonDown(int x, int y);
        void HandleMouseMove(int x, int y);
        void HandleCaptureLost();

        balcony::lua::LuaRuntime& Lua() { return _lua; }

    private:
        Desktop _desktop;
        Taskbar _taskbar;

        balcony::ui::Text _quitItem;
        balcony::ui::Tooltip _desktopMenu;

        // At most one context menu is open at a time.
        balcony::ui::Tooltip* _activeMenu = nullptr;

        // Click-vs-drag state machine: a mouse-down just remembers the
        // hit draggable component and where the button went down;
        // `dragging` only flips true once the cursor has moved past
        // Windows' own click threshold (GetSystemMetrics(SM_CXDRAG/
        // SM_CYDRAG)), so an ordinary click-without-movement never
        // triggers a drag. See HandleLeftButtonDown/HandleMouseMove/
        // HandleLeftClick.
        struct DragState
        {
            balcony::ui::Component* target = nullptr;
            float startX = 0.0f;
            float startY = 0.0f;
            float lastX = 0.0f;
            float lastY = 0.0f;
            bool dragging = false;
        };
        DragState _drag;

        balcony::lua::LuaRuntime _lua;
        balcony::core::SystemMonitors _systemMonitors;
        balcony::persistence::StateStore _stateStore;
    };
}
