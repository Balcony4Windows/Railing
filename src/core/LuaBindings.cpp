#include "balcony/core/LuaBindings.h"

#include "balcony/core/AudioBackend.h"
#include "balcony/core/DesktopItems.h"
#include "balcony/core/Invalidation.h"
#include "balcony/core/Launcher.h"
#include "balcony/core/NetworkBackend.h"
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

    void RegisterLuaBindings(sol::state& lua, SystemMonitors& monitors, AudioBackend& audio, NetworkBackend& network, uint64_t selfWindowId)
    {
        sol::table system = lua.create_table();
        system.set_function("Time", [&monitors] { return monitors.TimeString(); });
        system.set_function("CpuUsage", [&monitors] { return monitors.CpuUsageString(); });
        system.set_function("MemoryUsage", [&monitors] { return monitors.MemoryUsageString(); });
        system.set_function("GpuUsage", [&monitors] { return monitors.GpuUsageString(); });
        system.set_function("GpuTemperature", [&monitors] { return monitors.GpuTemperatureString(); });
        system.set_function("NetworkStatus", [&monitors] { return monitors.NetworkStatusString(); });
        system.set_function("BatteryStatus", [&monitors] { return monitors.BatteryStatusString(); });
        system.set_function("CpuUsagePercent", [&monitors] { return monitors.CpuUsagePercent(); });
        system.set_function("MemoryUsagePercent", [&monitors] { return monitors.MemoryUsagePercent(); });
        system.set_function("GpuUsagePercent", [&monitors] { return monitors.GpuUsagePercent(); });
        lua["System"] = system;

        // Kept separate from System (pure read-only state, per CLAUDE.md
        // section 6) -- launching a process is an action.
        sol::table shell = lua.create_table();
        shell.set_function("Launch", [](const std::string& path, sol::optional<std::string> arguments)
        {
            return Launch(Utf8ToWide(path), arguments ? Utf8ToWide(*arguments) : L"");
        });
        // What's actually sitting on the real Windows desktop right now
        // -- see DesktopItems.h. `path` works directly with both
        // Shell.Launch and Image:SetSystemIcon (a .lnk's target/icon is
        // resolved by ShellExecuteW/SHGetFileInfoW themselves).
        shell.set_function("DesktopItems", [](sol::this_state s)
        {
            sol::state_view luaState(s);
            const std::vector<DesktopItemInfo> items = EnumerateDesktopItems();

            sol::table result = luaState.create_table(static_cast<int>(items.size()), 0);
            int index = 1;
            for (const DesktopItemInfo& item : items)
            {
                sol::table entry = luaState.create_table();
                entry["path"] = WideToUtf8(item.path);
                entry["displayName"] = WideToUtf8(item.displayName);
                result[index++] = entry;
            }
            return result;
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
                entry["path"] = WideToUtf8(info.path);
                result[index++] = entry;
            }
            return result;
        });
        windows.set_function("Activate", [](uint64_t id) { return ActivateWindow(id); });
        windows.set_function("Close", [](uint64_t id) { CloseWindow(id); });
        lua["Windows"] = windows;

        // Default audio output device -- backs the volume flyout. See
        // AudioBackend.h. An action surface (can change the system's
        // actual audio config), same reasoning as Windows/Shell above,
        // not read-only System state.
        sol::table audioTable = lua.create_table();
        audioTable.set_function("GetVolume", [&audio] { return audio.GetVolume(); });
        audioTable.set_function("SetVolume", [&audio](float volume) { audio.SetVolume(volume); });
        audioTable.set_function("GetMute", [&audio] { return audio.GetMute(); });
        audioTable.set_function("ToggleMute", [&audio] { audio.ToggleMute(); });
        audioTable.set_function("GetCurrentDeviceName", [&audio] { return WideToUtf8(audio.GetCurrentDeviceName()); });
        audioTable.set_function("ListOutputDevices", [&audio](sol::this_state s)
        {
            sol::state_view luaState(s);
            const std::vector<AudioDeviceInfo> devices = audio.EnumerateOutputDevices();

            sol::table result = luaState.create_table(static_cast<int>(devices.size()), 0);
            int index = 1;
            for (const AudioDeviceInfo& device : devices)
            {
                sol::table entry = luaState.create_table();
                entry["id"] = WideToUtf8(device.id);
                entry["name"] = WideToUtf8(device.name);
                result[index++] = entry;
            }
            return result;
        });
        audioTable.set_function("SetDefaultDevice", [&audio](const std::string& id) { audio.SetDefaultDevice(Utf8ToWide(id)); });
        lua["Audio"] = audioTable;

        // Wi-Fi scan/connect -- backs the network flyout. See
        // NetworkBackend.h. ConnectTo takes the network table as
        // returned by ScanNetworks() (not just an SSID string) so the
        // auth/cipher fields it read at scan time round-trip back into
        // the profile-creation logic without a redundant re-scan.
        sol::table networkTable = lua.create_table();
        networkTable.set_function("GetStatus", [&network](sol::this_state s)
        {
            sol::state_view luaState(s);
            const NetworkStatus status = network.GetCurrentStatus();
            sol::table result = luaState.create_table();
            result["connected"] = status.connected;
            result["signalQuality"] = status.signalQuality;
            return result;
        });
        networkTable.set_function("RequestScan", [&network] { network.RequestScan(); });
        networkTable.set_function("ScanNetworks", [&network](sol::this_state s)
        {
            sol::state_view luaState(s);
            const std::vector<WifiNetworkInfo> networks = network.ScanNetworks();

            sol::table result = luaState.create_table(static_cast<int>(networks.size()), 0);
            int index = 1;
            for (const WifiNetworkInfo& net : networks)
            {
                sol::table entry = luaState.create_table();
                entry["ssid"] = WideToUtf8(net.ssid);
                entry["signalQuality"] = net.signalQuality;
                entry["connected"] = net.connected;
                entry["secure"] = net.secure;
                // Opaque to Lua -- only meaningful round-tripped back
                // into ConnectTo below.
                entry["authAlgorithm"] = static_cast<int>(net.authAlgorithm);
                entry["cipherAlgorithm"] = static_cast<int>(net.cipherAlgorithm);
                result[index++] = entry;
            }
            return result;
        });
        networkTable.set_function("HasSavedProfile", [&network](const std::string& ssid) { return network.HasSavedProfile(Utf8ToWide(ssid)); });
        networkTable.set_function("ConnectTo", [&network](sol::table net, const std::string& password)
        {
            WifiNetworkInfo info;
            info.ssid = Utf8ToWide(net["ssid"].get_or<std::string>(""));
            info.signalQuality = net["signalQuality"].get_or(0);
            info.connected = net["connected"].get_or(false);
            info.secure = net["secure"].get_or(false);
            info.authAlgorithm = static_cast<DOT11_AUTH_ALGORITHM>(net["authAlgorithm"].get_or(static_cast<int>(DOT11_AUTH_ALGO_80211_OPEN)));
            info.cipherAlgorithm = static_cast<DOT11_CIPHER_ALGORITHM>(net["cipherAlgorithm"].get_or(static_cast<int>(DOT11_CIPHER_ALGO_NONE)));
            network.ConnectTo(info, Utf8ToWide(password));
        });
        networkTable.set_function("IsConnecting", [&network] { return network.IsConnecting(); });
        networkTable.set_function("TryTakeConnectResult", [&network]() -> sol::optional<std::string>
        {
            std::optional<std::wstring> result = network.TryTakeConnectResult();
            if (!result)
            {
                return sol::nullopt;
            }
            return WideToUtf8(*result);
        });
        networkTable.set_function("Disconnect", [&network] { network.Disconnect(); });
        lua["Network"] = networkTable;

        // `Balcony` itself already exists by this point -- created by
        // LuaRuntime's own bootstrap (OnUpdate/UpdateHandlers) before
        // RegisterLuaBindings ever runs. RequestRedraw is the escape
        // hatch for content C++ can't see into on its own -- e.g. a
        // Canvas whose drawn content depends on data read from outside
        // any of the mutating primitive calls that already request a
        // redraw automatically (SetText, SetPosition, ...). See
        // Invalidation.h.
        sol::table balcony = lua["Balcony"];
        balcony.set_function("RequestRedraw", [] { RequestRedraw(); });
    }
}
