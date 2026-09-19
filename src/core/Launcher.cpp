#include "balcony/core/Launcher.h"

#include <Windows.h>
#include <shellapi.h>

#include <string>

namespace balcony::core
{
    bool Launch(std::wstring_view path, std::wstring_view arguments)
    {
        const std::wstring pathStr(path);
        const std::wstring argsStr(arguments);

        const HINSTANCE result = ShellExecuteW(
            nullptr,
            nullptr, // default verb ("open")
            pathStr.c_str(),
            argsStr.empty() ? nullptr : argsStr.c_str(),
            nullptr,
            SW_SHOWNORMAL);

        // ShellExecuteW's return type is HINSTANCE only for historical
        // (16-bit Windows) reasons; the real contract is: > 32 means
        // success, <= 32 is one of the SE_ERR_* error codes.
        return reinterpret_cast<INT_PTR>(result) > 32;
    }
}
