#include "balcony/lua/LuaRuntime.h"

#include <Windows.h>

namespace balcony::lua
{
    namespace
    {
        void LogError(const sol::protected_function_result& result)
        {
            const sol::error error = result;
            OutputDebugStringA(error.what());
            OutputDebugStringA("\n");
        }

        // Pure Lua-state plumbing -- LuaRuntime still knows nothing about
        // ui/core types. Defines Balcony.OnUpdate(fn), which appends to a
        // plain array so any number of independently loaded scripts can
        // each register their own per-frame handler without overwriting
        // each other's (unlike reassigning a single global `Update`
        // function, which is what this replaces).
        constexpr std::string_view kBootstrap = R"lua(
            Balcony = Balcony or {}
            Balcony.UpdateHandlers = {}
            function Balcony.OnUpdate(fn)
                table.insert(Balcony.UpdateHandlers, fn)
            end
        )lua";
    }

    void LuaRuntime::Initialize()
    {
        _state.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math);
        _state.safe_script(std::string(kBootstrap), sol::script_pass_on_error);
    }

    bool LuaRuntime::RunFile(std::string_view path)
    {
        const sol::protected_function_result result = _state.safe_script_file(std::string(path), sol::script_pass_on_error);
        if (!result.valid())
        {
            LogError(result);
            return false;
        }
        return true;
    }

    bool LuaRuntime::RunString(std::string_view code)
    {
        const sol::protected_function_result result = _state.safe_script(std::string(code), sol::script_pass_on_error);
        if (!result.valid())
        {
            LogError(result);
            return false;
        }
        return true;
    }

    void LuaRuntime::Update(float deltaSeconds)
    {
        sol::table balcony = _state["Balcony"];
        if (!balcony.valid())
        {
            return;
        }

        sol::table handlers = balcony["UpdateHandlers"];
        if (!handlers.valid())
        {
            return;
        }

        for (auto& entry : handlers)
        {
            sol::protected_function handler = entry.second;
            if (!handler.valid())
            {
                continue;
            }

            const sol::protected_function_result result = handler(deltaSeconds);
            if (!result.valid())
            {
                LogError(result);
            }
        }
    }
}
