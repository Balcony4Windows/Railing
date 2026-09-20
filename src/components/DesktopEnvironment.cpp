#include "balcony/components/DesktopEnvironment.h"

#include "balcony/core/LuaBindings.h"
#include "balcony/persistence/LuaBindings.h"
#include "balcony/renderer/Renderer.h"
#include "balcony/ui/LuaBindings.h"
#include "balcony/windows/WindowsIntegration.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

namespace balcony::components
{
    namespace
    {
        constexpr float kTaskbarHeight = 48.0f;
        constexpr float kMenuPaddingX = 12.0f;
        constexpr float kMenuPaddingY = 8.0f;
    }

    void DesktopEnvironment::Initialize(balcony::windows::Window& window, balcony::renderer::Renderer& renderer)
    {
        // See CLAUDE.md sections 6-8: Desktop/components are bound as
        // Lua composition targets, and System exposes live monitor state
        // (below) so a script can react to it each frame via Update().
        _lua.Initialize();
        balcony::ui::RegisterLuaBindings(_lua.State(), renderer);
        balcony::core::RegisterLuaBindings(_lua.State(), _systemMonitors, reinterpret_cast<uint64_t>(window.Handle()));

        // Must happen before any script runs below: desktop.lua reads
        // persisted icon positions synchronously while building icons.
        _stateStore.Load();
        balcony::persistence::RegisterLuaBindings(_lua.State(), _stateStore);

        const float screenWidth = static_cast<float>(window.Width());
        const float screenHeight = static_cast<float>(window.Height());

        _desktop.SetBounds({0.0f, 0.0f, screenWidth, screenHeight});

        // Empty placeholder -- no widgets exist yet to put on it.
        // Background/border are inherited from VisualComponent (every
        // component has them, off by default); this just turns them on.
        _taskbar.Initialize(renderer.Device(), renderer.Queue(), renderer.Primitives());
        _taskbar.SetBounds({0.0f, screenHeight - kTaskbarHeight, screenWidth, kTaskbarHeight});
        _taskbar.SetBackgroundColor({0.05f, 0.05f, 0.05f, 0.85f});
        _taskbar.SetBorderColor({0.25f, 0.25f, 0.25f, 1.0f});
        _taskbar.SetBorderWidth(1.0f);

        _quitItem.SetText(renderer.Device(), renderer.Queue(), renderer.Primitives(), L"Quit Balcony");
        _quitItem.SetPosition(kMenuPaddingX, kMenuPaddingY);
        _quitItem.SetOnClick([&window]()
        {
            DestroyWindow(window.Handle());
        });

        _desktopMenu.Initialize(renderer.Device(), renderer.Queue(), renderer.Primitives());
        // Sized from the item's own measured bounds plus padding -- not a
        // guessed constant, so the panel actually fits its content.
        // Positioned at the origin here; Tooltip::Show translates both
        // the menu and _quitItem together to wherever it's shown, so
        // this relative layout (item padded from the menu's top-left)
        // stays correct no matter where that ends up.
        _desktopMenu.SetBounds({
            0.0f, 0.0f,
            _quitItem.Bounds().width + kMenuPaddingX * 2.0f,
            _quitItem.Bounds().height + kMenuPaddingY * 2.0f});
        _desktopMenu.Add(&_quitItem);

        _desktop.SetTooltip(&_desktopMenu);

        // Exposed as their base type (Container) -- Desktop/Taskbar add
        // nothing of their own, so there's no separate Lua registration
        // for either. Non-owning, same as every C++ Container::Add caller.
        _lua.State()["Desktop"] = static_cast<balcony::ui::Container*>(&_desktop);
        _lua.State()["Taskbar"] = static_cast<balcony::ui::Container*>(&_taskbar);

        _lua.RunFile(BALCONY_SCRIPTS_DIR "desktop.lua");

        // Auto-load every .lua file under scripts/plugins/, in a
        // deterministic order, so adding a new feature is "drop a script
        // in this folder" rather than new C++ each time -- see CLAUDE.md
        // sections 7-8 and TODO.md's extensibility ask. Tolerates the
        // directory being empty or absent.
        namespace fs = std::filesystem;
        std::error_code dirEc;
        const fs::path pluginsDir = fs::path(BALCONY_SCRIPTS_DIR) / "plugins";
        if (fs::is_directory(pluginsDir, dirEc))
        {
            std::vector<fs::path> pluginFiles;
            std::error_code iterEc;
            for (const auto& entry : fs::directory_iterator(pluginsDir, iterEc))
            {
                if (entry.is_regular_file() && entry.path().extension() == L".lua")
                {
                    pluginFiles.push_back(entry.path());
                }
            }
            std::sort(pluginFiles.begin(), pluginFiles.end());
            for (const auto& pluginFile : pluginFiles)
            {
                _lua.RunFile(pluginFile.string());
            }
        }
    }

    void DesktopEnvironment::Update(float deltaSeconds)
    {
        _systemMonitors.Update(deltaSeconds);
        _lua.Update(deltaSeconds);

        _desktop.Update(deltaSeconds);
        _taskbar.Update(deltaSeconds);
        if (_activeMenu)
        {
            _activeMenu->Update(deltaSeconds);
        }
    }

    void DesktopEnvironment::Draw(balcony::renderer::PrimitiveRenderer& primitives)
    {
        _desktop.Draw(primitives);
        _taskbar.Draw(primitives);
        if (_activeMenu)
        {
            _activeMenu->Draw(primitives);
        }
    }

    void DesktopEnvironment::HandleRightClick(int x, int y)
    {
        if (_drag.dragging)
        {
            // Defensive: a genuine simultaneous left-drag + right-click
            // isn't a designed interaction.
            return;
        }

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);

        // Taskbar sits visually on top of the desktop (it's drawn after
        // it and its bounds are a strip within the desktop's own), so a
        // click there must resolve against the taskbar first.
        balcony::ui::Component* hit = _taskbar.FindHit(fx, fy);
        if (!hit)
        {
            hit = _desktop.FindHit(fx, fy);
        }

        balcony::ui::Tooltip* tooltip = hit ? hit->GetTooltip() : nullptr;
        if (!tooltip)
        {
            return;
        }

        tooltip->Show(fx, fy);
        _activeMenu = tooltip;
    }

    void DesktopEnvironment::HandleLeftClick(int x, int y)
    {
        // Snapshot and clear drag state up front, whether or not a drag
        // was actually happening: this is also where WM_LBUTTONUP's
        // ordinary (non-dragging) case must leave _drag clean.
        const bool wasDragging = _drag.dragging;
        balcony::ui::Component* dragTarget = _drag.target;
        _drag = DragState{};

        if (wasDragging)
        {
            // A real drag just ended -- persist via the component's own
            // callback and suppress the ordinary click dispatch below.
            // Dragging an icon must not also launch it.
            if (dragTarget)
            {
                dragTarget->DragEnd();
            }
            return;
        }

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);

        if (_activeMenu && _activeMenu->IsVisible())
        {
            if (balcony::ui::Component* hit = _activeMenu->FindHit(fx, fy))
            {
                hit->Click();
            }
            _activeMenu->Hide();
            _activeMenu = nullptr;
            return;
        }

        // No menu open -- an ordinary click against the desktop/taskbar
        // itself (e.g. a desktop icon). Same taskbar-first precedence as
        // the right-click path above.
        balcony::ui::Component* hit = _taskbar.FindHit(fx, fy);
        if (!hit)
        {
            hit = _desktop.FindHit(fx, fy);
        }
        if (hit)
        {
            hit->Click();
        }
    }

    void DesktopEnvironment::HandleLeftButtonDown(int x, int y)
    {
        if (_activeMenu && _activeMenu->IsVisible())
        {
            // Never start a drag from inside an open context menu.
            return;
        }

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);

        balcony::ui::Component* hit = _taskbar.FindDraggable(fx, fy);
        if (!hit)
        {
            hit = _desktop.FindDraggable(fx, fy);
        }
        if (!hit)
        {
            return;
        }

        _drag = DragState{};
        _drag.target = hit;
        _drag.startX = _drag.lastX = fx;
        _drag.startY = _drag.lastY = fy;
    }

    void DesktopEnvironment::HandleMouseMove(int x, int y)
    {
        if (!_drag.target)
        {
            return;
        }

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);

        if (!_drag.dragging)
        {
            // Windows' own click-vs-drag threshold, not a made-up
            // constant -- keeps this consistent with every other
            // Windows app.
            if (std::abs(fx - _drag.startX) < static_cast<float>(GetSystemMetrics(SM_CXDRAG)) &&
                std::abs(fy - _drag.startY) < static_cast<float>(GetSystemMetrics(SM_CYDRAG)))
            {
                return;
            }
            _drag.dragging = true;
        }

        _drag.target->Translate(fx - _drag.lastX, fy - _drag.lastY);
        _drag.lastX = fx;
        _drag.lastY = fy;
    }

    void DesktopEnvironment::HandleCaptureLost()
    {
        // Capture was stolen mid-drag before a WM_LBUTTONUP arrived
        // (e.g. alt-tab) -- finalize at the current, already-translated
        // position rather than silently discarding the move. A no-op in
        // the ordinary case, where HandleLeftClick already cleared
        // _drag just before releasing capture itself.
        if (_drag.dragging && _drag.target)
        {
            _drag.target->DragEnd();
        }
        _drag = DragState{};
    }
}
