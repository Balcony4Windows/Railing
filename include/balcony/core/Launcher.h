#pragma once

#include <string_view>

// A thin ShellExecuteW wrapper -- not CreateProcessW -- so this also
// handles opening arbitrary documents/URLs and elevation prompts, not
// just launching .exe files. That's what a desktop icon's or taskbar
// entry's "open this" action actually needs. See CLAUDE.md section 6:
// this is an action, kept separate from the read-only `System` state
// table.
namespace balcony::core
{
    // Returns false if the shell reports failure (see ShellExecute's
    // documented HINSTANCE-as-error-code convention in Launcher.cpp).
    bool Launch(std::wstring_view path, std::wstring_view arguments = L"");
}
