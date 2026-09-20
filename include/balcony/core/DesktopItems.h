#pragma once

#include <string>
#include <vector>

// Enumerates the real contents of the Windows desktop (both the
// per-user and the all-users/public desktop folders, same as Explorer
// merges), so scripts/desktop.lua can build icons from what's actually
// there instead of a hardcoded list. See CLAUDE.md section 13's spirit
// applied to the desktop, and TODO.md.
namespace balcony::core
{
    struct DesktopItemInfo
    {
        // Absolute path -- also the identity Persistence keys a saved
        // icon position by (see scripts/desktop.lua). Works directly
        // with both Shell.Launch (ShellExecuteW resolves a .lnk's
        // target itself, no manual shortcut resolution needed) and
        // Image:SetSystemIcon (SHGetFileInfoW likewise resolves a
        // .lnk's icon on its own).
        std::wstring path;

        // Filename with its extension stripped -- matches Explorer's
        // default "hide extensions for known file types" look closely
        // enough for a desktop icon label.
        std::wstring displayName;
    };

    // Skips hidden/system entries (desktop.ini, Thumbs.db, ...); does
    // not recurse into subfolders (a folder on the desktop becomes one
    // icon, opened via Shell.Launch like Explorer does, not expanded).
    std::vector<DesktopItemInfo> EnumerateDesktopItems();
}
