#pragma once

#include <cstdint>

#include <sol/forward.hpp>

// Registers `System` as a Lua global table of functions returning
// display-ready strings -- System.Time(), System.CpuUsage(), and so on
// -- plus `Windows`, for enumerating/activating/closing top-level
// windows (see WindowEnumerator.h), and `Audio`/`Network`, for the
// volume/Wi-Fi quick-settings flyouts (see AudioBackend.h/
// NetworkBackend.h). See CLAUDE.md section 6. `System` reads straight
// from `monitors`, which must outlive the Lua state; something needs to
// call monitors.Update() once per tick for those values to actually
// refresh (see SystemMonitors). `selfWindowId` is Balcony's own window
// (as a HWND reinterpreted to an integer, matching WindowEnumerator's
// convention) so Windows.Running() can exclude it.
namespace balcony::core
{
    class SystemMonitors;
    class AudioBackend;
    class NetworkBackend;

    void RegisterLuaBindings(sol::state& lua, SystemMonitors& monitors, AudioBackend& audio, NetworkBackend& network, uint64_t selfWindowId);
}
