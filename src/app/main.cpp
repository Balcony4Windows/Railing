#include <Windows.h>
#include <combaseapi.h>
#include <shobjidl.h>

#include "balcony/components/DesktopEnvironment.h"
#include "balcony/renderer/Renderer.h"
#include "balcony/windows/WindowsIntegration.h"

#include "Resource.h"

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    // Per-monitor DPI awareness must be declared before any window is
    // created; otherwise Windows silently bitmap-stretches our output on
    // scaled displays and GetSystemMetrics returns virtualized, not
    // physical, pixel dimensions. See CLAUDE.md section 12.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // UserNotificationListener (and Windows generally) needs an explicit
    // AppUserModelID to attribute/identify this unpackaged process.
    SetCurrentProcessExplicitAppUserModelID(L"Balcony.DesktopEnvironment");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const uint32_t screenWidth = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
    const uint32_t screenHeight = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));

    balcony::windows::Window window;
    if (!window.Create(L"Balcony", screenWidth, screenHeight, IDI_APP_ICON))
    {
        CoUninitialize();
        return -1;
    }

    balcony::renderer::Renderer renderer;
    renderer.Initialize(window.Handle(), window.Width(), window.Height());

    window.SetResizeCallback([&renderer](uint32_t width, uint32_t height)
    {
        renderer.Resize(width, height);
    });

    // All DE composition (desktop, taskbar, the quit menu, ...) lives in
    // DesktopEnvironment; this stays a thin entry point that just wires
    // Win32 events to it and runs the loop.
    balcony::components::DesktopEnvironment desktopEnvironment;
    desktopEnvironment.Initialize(window, renderer);

    window.SetRightClickCallback([&desktopEnvironment](int x, int y)
    {
        desktopEnvironment.HandleRightClick(x, y);
    });

    window.SetLeftClickCallback([&desktopEnvironment](int x, int y)
    {
        desktopEnvironment.HandleLeftClick(x, y);
    });

    window.SetLeftButtonDownCallback([&desktopEnvironment](int x, int y)
    {
        desktopEnvironment.HandleLeftButtonDown(x, y);
    });

    window.SetMouseMoveCallback([&desktopEnvironment](int x, int y)
    {
        desktopEnvironment.HandleMouseMove(x, y);
    });

    window.SetCaptureLostCallback([&desktopEnvironment]()
    {
        desktopEnvironment.HandleCaptureLost();
    });

    window.Show();

    LARGE_INTEGER frequency{};
    LARGE_INTEGER lastTime{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastTime);

    while (window.PumpMessages())
    {
        // Ordinary desktop activity (a window opening, closing, or being
        // activated) can shuffle Balcony back up the z-order; re-pin it
        // to the bottom every frame rather than relying on the one-time
        // placement from Show(). See Window::KeepAtBottom.
        window.KeepAtBottom();

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const float deltaSeconds = static_cast<float>(now.QuadPart - lastTime.QuadPart) / static_cast<float>(frequency.QuadPart);
        lastTime = now;

        desktopEnvironment.Update(deltaSeconds);

        renderer.BeginFrame();
        desktopEnvironment.Draw(renderer.Primitives());
        renderer.EndFrame();
    }

    renderer.Shutdown();
    CoUninitialize();
    return 0;
}
