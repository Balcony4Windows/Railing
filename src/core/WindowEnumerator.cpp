#include "balcony/core/WindowEnumerator.h"

#include <Windows.h>
#include <dwmapi.h>

namespace balcony::core
{
    namespace
    {
        struct EnumContext
        {
            std::vector<RunningWindowInfo>* out;
            uint64_t selfId;
        };

        bool IsListableWindow(HWND hwnd)
        {
            if (!IsWindowVisible(hwnd))
            {
                return false;
            }

            // Owned windows (dialogs, tool palettes, ...) aren't
            // independent taskbar entries -- their owner is.
            if (GetWindow(hwnd, GW_OWNER) != nullptr)
            {
                return false;
            }

            const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (exStyle & WS_EX_TOOLWINDOW)
            {
                return false;
            }

            if (GetWindowTextLengthW(hwnd) == 0)
            {
                return false;
            }

            // Modern/UWP app hosts often leave behind windows that pass
            // every check above yet are only DWM-cloaked (not actually
            // visible to the user) -- without this they'd show up as
            // ghost taskbar entries.
            BOOL cloaked = FALSE;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
            {
                return false;
            }

            return true;
        }

        BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lparam)
        {
            EnumContext& context = *reinterpret_cast<EnumContext*>(lparam);
            if (reinterpret_cast<uint64_t>(hwnd) == context.selfId)
            {
                return TRUE;
            }

            if (!IsListableWindow(hwnd))
            {
                return TRUE;
            }

            const int length = GetWindowTextLengthW(hwnd);
            std::wstring title(static_cast<size_t>(length), L'\0');
            const int actualLength = GetWindowTextW(hwnd, title.data(), length + 1);
            title.resize(static_cast<size_t>(actualLength));

            context.out->push_back({reinterpret_cast<uint64_t>(hwnd), std::move(title)});
            return TRUE;
        }
    }

    std::vector<RunningWindowInfo> EnumerateRunningWindows(uint64_t selfId)
    {
        std::vector<RunningWindowInfo> result;
        EnumContext context{&result, selfId};
        EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&context));
        return result;
    }

    bool ActivateWindow(uint64_t id)
    {
        const HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
        if (!IsWindow(hwnd))
        {
            return false;
        }

        if (IsIconic(hwnd))
        {
            ShowWindow(hwnd, SW_RESTORE);
        }

        const HWND foreground = GetForegroundWindow();
        const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
        const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
        const DWORD currentThread = GetCurrentThreadId();

        // SetForegroundWindow is normally refused for a process that
        // isn't already foreground -- attaching input state to the
        // currently-foreground thread (and the target's) is the
        // standard, documented way around that restriction.
        const bool attachForeground = foreground && foregroundThread != currentThread;
        const bool attachTarget = targetThread != currentThread;
        if (attachForeground)
        {
            AttachThreadInput(currentThread, foregroundThread, TRUE);
        }
        if (attachTarget)
        {
            AttachThreadInput(currentThread, targetThread, TRUE);
        }

        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);

        if (attachTarget)
        {
            AttachThreadInput(currentThread, targetThread, FALSE);
        }
        if (attachForeground)
        {
            AttachThreadInput(currentThread, foregroundThread, FALSE);
        }

        return GetForegroundWindow() == hwnd;
    }

    void CloseWindow(uint64_t id)
    {
        const HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(id));
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}
