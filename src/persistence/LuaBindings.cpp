#include "balcony/persistence/LuaBindings.h"
#include "balcony/persistence/Persistence.h"

#include <sol/sol.hpp>

#include <algorithm>

namespace balcony::persistence
{
    void RegisterLuaBindings(sol::state& lua, StateStore& store)
    {
        sol::table persistence = lua.create_table();

        persistence.set_function("DesktopIconPosition", [&store](sol::this_state s, const std::string& key) -> sol::object
        {
            sol::state_view luaState(s);
            auto& positions = store.State().desktopIconPositions;
            const auto it = std::find_if(positions.begin(), positions.end(),
                [&key](const DesktopIconPosition& p) { return p.key == key; });
            if (it == positions.end())
            {
                return sol::lua_nil;
            }

            sol::table result = luaState.create_table();
            result["x"] = it->x;
            result["y"] = it->y;
            return result;
        });

        persistence.set_function("SetDesktopIconPosition", [&store](const std::string& key, float x, float y)
        {
            auto& positions = store.State().desktopIconPositions;
            const auto it = std::find_if(positions.begin(), positions.end(),
                [&key](const DesktopIconPosition& p) { return p.key == key; });
            if (it != positions.end())
            {
                it->x = x;
                it->y = y;
            }
            else
            {
                positions.push_back(DesktopIconPosition{key, x, y});
            }
            store.Save();
        });

        persistence.set_function("PinnedApps", [&store](sol::this_state s)
        {
            sol::state_view luaState(s);
            const auto& pinned = store.State().pinnedApps;

            sol::table result = luaState.create_table(static_cast<int>(pinned.size()), 0);
            int index = 1;
            for (const PinnedApp& app : pinned)
            {
                sol::table entry = luaState.create_table();
                entry["path"] = app.path;
                entry["displayName"] = app.displayName;
                result[index++] = entry;
            }
            return result;
        });

        persistence.set_function("SetPinnedApps", [&store](sol::table list)
        {
            std::vector<PinnedApp> apps;
            apps.reserve(list.size());
            for (auto& kv : list)
            {
                sol::table entry = kv.second;
                apps.push_back(PinnedApp{
                    entry.get_or<std::string>("path", ""),
                    entry.get_or<std::string>("displayName", "")});
            }
            store.State().pinnedApps = std::move(apps);
            store.Save();
        });

        lua["Persistence"] = persistence;
    }
}
