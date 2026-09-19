#pragma once

#include "balcony/components/Desktop.h"
#include "balcony/components/Taskbar.h"
#include "balcony/core/SystemMonitors.h"
#include "balcony/lua/LuaRuntime.h"
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

        balcony::lua::LuaRuntime& Lua() { return _lua; }

    private:
        Desktop _desktop;
        Taskbar _taskbar;

        balcony::ui::Text _quitItem;
        balcony::ui::Tooltip _desktopMenu;

        // At most one context menu is open at a time.
        balcony::ui::Tooltip* _activeMenu = nullptr;

        balcony::lua::LuaRuntime _lua;
        balcony::core::SystemMonitors _systemMonitors;
    };
}
