#pragma once

namespace sol
{
    class state;
}

namespace balcony::persistence
{
    class StateStore;

    // Exposes a small, purely data-shaped `Persistence` table -- Lua has
    // no io/os access by design (see LuaRuntime::Initialize), so this is
    // the only way a script can make something survive a restart.
    // Deliberately not a generic query/save surface: every mutator
    // persists itself immediately, and callers never see a cereal type
    // or a raw file path. See CLAUDE.md sections 7-9.
    void RegisterLuaBindings(sol::state& lua, StateStore& store);
}
