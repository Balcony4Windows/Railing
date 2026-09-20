#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Enumerates the top-level windows a taskbar would normally list --
// real, user-facing application windows -- and provides the two actions
// a taskbar button needs: bring one to the foreground, or ask it to
// close. See CLAUDE.md section 13.
//
// A window is identified by `id`, its HWND reinterpreted as an integer
// so it survives crossing into Lua as a plain number; ActivateWindow/
// CloseWindow take that same id back rather than a real HWND.
namespace balcony::core
{
    struct RunningWindowInfo
    {
        uint64_t id = 0;
        std::wstring title;

        // The owning process's executable path, for matching a taskbar
        // pin (identified by path) against a running window. Empty if
        // it couldn't be queried -- e.g. an elevated process this
        // (unelevated) DE can't inspect; callers must treat empty as
        // "can't match a pin," not an error.
        std::wstring path;
    };

    // Excludes `selfId` (Balcony's own window) so the taskbar never
    // lists itself, along with anything that isn't a normal top-level
    // application window: invisible, owned (dialogs/tool palettes --
    // their owner is the real entry), a tool window, DWM-cloaked (the
    // hidden windows some UWP app hosts leave lying around), or
    // title-less.
    std::vector<RunningWindowInfo> EnumerateRunningWindows(uint64_t selfId);

    // Brings a window to the foreground, restoring it first if
    // minimized. Windows normally blocks a background process from
    // stealing foreground focus; this uses the standard
    // AttachThreadInput workaround so it actually works from a DE
    // process that (deliberately) never takes focus itself. Returns
    // whether it ended up in the foreground.
    bool ActivateWindow(uint64_t id);

    // Politely asks a window to close (WM_CLOSE) -- the same thing its
    // own title-bar close button does, not a forced termination.
    void CloseWindow(uint64_t id);
}
