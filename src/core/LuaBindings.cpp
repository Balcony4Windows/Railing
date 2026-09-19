#include "balcony/core/LuaBindings.h"

#include "balcony/core/Launcher.h"
#include "balcony/core/SystemMonitors.h"
#include "balcony/core/WindowEnumerator.h"

#include <sol/sol.hpp>

#include <Windows.h>

namespace balcony::core
{
    namespace
    {
        // Mirrors balcony::ui::LuaBindings.cpp's own private helper --
        // core doesn't currently share a string-conversion utility with
        // ui, and one two-consumer helper isn't worth promoting to a
        // shared header yet.
        std::wstring Utf8ToWide(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }

            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }

        std::string WideToUtf8(const std::wstring& wide)
        {
            if (wide.empty())
            {
                return {};
            }

            const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            std::string narrow(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), length, nullptr, nullptr);
            return narrow;
        }
    }

    void RegisterLuaBindings(sol::state& lua, SystemMonitors& monitors, uint64_t selfWindowId)
    {
        sol::table system = lua.create_table();
        system.set_function("Time", [&monitors] { return monitors.TimeString(); });
        system.set_function("CpuUsage", [&monitors] { return monitors.CpuUsageString(); });
        system.set_function("MemoryUsage", [&monitors] { return monitors.MemoryUsageString(); });
        system.set_function("GpuUsage", [&monitors] { return monitors.GpuUsageString(); });
        system.set_function("GpuTemperature", [&monitors] { return monitors.GpuTemperatureString(); });
        system.set_function("NetworkStatus", [&monitors] { return monitors.NetworkStatusString(); });
        system.set_function("BatteryStatus", [&monitors] { return monitors.BatteryStatusString(); });
        lua["System"] = system;

        // Kept separate from System (pure read-only state, per CLAUDE.md
        // section 6) -- launching a process is an action.
        sol::table shell = lua.create_table();
        shell.set_function("Launch", [](const std::string& path, sol::optional<std::string> arguments)
        {
            return Launch(Utf8ToWide(path), arguments ? Utf8ToWide(*arguments) : L"");
        });
        lua["Shell"] = shell;

        // Running-window listing/control for a taskbar -- see CLAUDE.md
        // section 13 ("Taskbar consumes RunningWindows") and
        // WindowEnumerator.h. `id` is an opaque number a script passes
        // back to Activate/Close/Image:SetWindowIcon; it isn't
        // meaningful to inspect otherwise.
        sol::table windows = lua.create_table();
        windows.set_function("Running", [selfWindowId](sol::this_state s)
        {
            sol::state_view luaState(s);
            const std::vector<RunningWindowInfo> running = EnumerateRunningWindows(selfWindowId);

            sol::table result = luaState.create_table(static_cast<int>(running.size()), 0);
            int index = 1;
            for (const RunningWindowInfo& info : running)
            {
                sol::table entry = luaState.create_table();
                entry["id"] = info.id;
                entry["title"] = WideToUtf8(info.title);
                result[index++] = entry;
            }
            return result;
        });
        windows.set_function("Activate", [](uint64_t id) { return ActivateWindow(id); });
        windows.set_function("Close", [](uint64_t id) { CloseWindow(id); });
        lua["Windows"] = windows;
    }
}
