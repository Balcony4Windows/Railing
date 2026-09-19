#pragma once

#include <sol/sol.hpp>

#include <string_view>

// Owns the Lua VM. This is deliberately the only thing this module
// knows about -- bindings for a given module's types live next to
// those types (e.g. balcony::ui::RegisterLuaBindings) and are
// registered into State() by whoever composes the app, so LuaRuntime
// itself stays usable regardless of what ends up exposed to it.
// See CLAUDE.md sections 7 and 8.
namespace balcony::lua
{
    class LuaRuntime
    {
    public:
        // Opens a deliberately small standard-library subset (base,
        // string, table, math) -- not io/os/debug, which would let a
        // script touch the filesystem or the process directly. See
        // CLAUDE.md section 17 on sandboxing; this is a first pass, not
        // a real trust boundary yet.
        void Initialize();

        bool RunFile(std::string_view path);
        bool RunString(std::string_view code);

        // Runs one registered per-frame handler after another (see the
        // `Balcony.OnUpdate` bootstrap in LuaRuntime.cpp) instead of a
        // single global `Update` function -- this lets multiple
        // independently loaded scripts (the main desktop script plus any
        // auto-loaded plugins) each hook the frame loop without
        // clobbering one another. A handler that errors is logged and
        // skipped; it does not stop the remaining handlers from running.
        void Update(float deltaSeconds);

        sol::state& State() { return _state; }

    private:
        sol::state _state;
    };
}
