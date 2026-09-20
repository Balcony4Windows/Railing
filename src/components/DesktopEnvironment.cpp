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
        _audioBackend.EnsureInitialized();
        balcony::core::RegisterLuaBindings(_lua.State(), _systemMonitors, _audioBackend, _networkBackend, reinterpret_cast<uint64_t>(window.Handle()));

        // Must happen before any script runs below: desktop.lua reads
        // persisted icon positions synchronously while building icons.
        _stateStore.Load();
        balcony::persistence::RegisterLuaBindings(_lua.State(), _stateStore);

        // Balcony.ShowFlyout needs `this` (to reach _activeMenu), which
        // the standalone RegisterLuaBindings free functions above don't
        // have -- registered directly here instead. See its own
        // declaration in DesktopEnvironment.h for why it exists.
        sol::table balconyTable = _lua.State()["Balcony"];
        balconyTable.set_function("ShowFlyout", [this](balcony::ui::Tooltip& flyout, float x, float y)
        {
            ShowFlyout(&flyout, x, y);
        });

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

        ShowFlyout(tooltip, fx, fy);
    }

    void DesktopEnvironment::ShowFlyout(balcony::ui::Tooltip* flyout, float x, float y)
    {
        if (!flyout)
        {
            return;
        }
        // Opening a second menu while a different one is still open must
        // actually close the first -- not just stop drawing it (Draw()/
        // Update() only ever reach _activeMenu, so the old one would
        // already vanish visually), but properly clear its own _visible
        // flag too, since scripts read IsVisible() themselves (e.g. both
        // flyouts gate their periodic re-poll on it). A no-op when
        // switching within the same menu (e.g. RebuildDeviceList calling
        // back into an already-open flyout) or when nothing was open.
        if (_activeMenu && _activeMenu != flyout)
        {
            _activeMenu->Hide();
        }

        // Clamp so the menu always opens fully on-screen. A right-click
        // menu shows AT the cursor -- fine for a desktop icon (usually
        // nowhere near an edge), but a taskbar button's menu shows at a
        // cursor that's already sitting right against the bottom edge,
        // so opening downward from there put almost the entire menu
        // below the visible screen (barely a sliver of border visible,
        // no items reachable at all -- this is what a taskbar app's
        // "missing" right-click menu actually was). Left-click flyouts
        // (volume/network) already position themselves above their icon
        // before calling this, so for them this is normally a no-op.
        const balcony::renderer::RectF& screen = _desktop.Bounds();
        const balcony::renderer::RectF& size = flyout->Bounds();
        float clampedX = std::min(x, screen.x + screen.width - size.width);
        float clampedY = std::min(y, screen.y + screen.height - size.height);
        clampedX = std::max(clampedX, screen.x);
        clampedY = std::max(clampedY, screen.y);

        flyout->Show(clampedX, clampedY);
        _activeMenu = flyout;
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
            // Never touch `menu` again once its own Hide()/Click() path
            // below has run: a handler like "Pin"/"Unpin" (see
            // taskbar_apps.lua) rebuilds the very button this menu
            // belongs to, which drops every Lua reference to this
            // Tooltip/Component -- touching them afterward would be a
            // use-after-free the moment Lua's GC reclaims them (which
            // can happen as soon as the next allocation, e.g. inside
            // that same rebuild).
            balcony::ui::Tooltip* menu = _activeMenu;
            balcony::ui::Component* hit = menu->FindHit(fx, fy);

            if (hit)
            {
                // Landed on something inside the menu itself. A
                // right-click context menu (CloseOnClick() true, the
                // default) closes the instant its one action fires, same
                // as before. A left-click quick-settings flyout
                // (CloseOnClick() false -- volume/network) stays open:
                // its whole point is surviving several interactions
                // (drag the slider, switch devices, pick a network) and
                // closing only on a click elsewhere, handled below.
                if (menu->CloseOnClick())
                {
                    _activeMenu = nullptr;
                    menu->Hide();
                }
                hit->Click();
                return;
            }

            // Landed outside the menu -- close it, then let the click
            // fall through to whatever's actually there (the taskbar or
            // desktop behind it), same as if no menu had been open at
            // all. This is what makes switching flyouts (click a
            // different tray icon while one is already open) a single
            // click instead of "one click to close, a second to open
            // the new one": that second icon's own SetOnClick handler
            // -- which calls Balcony.ShowFlyout -- runs in THIS same
            // click, right after Hide() below.
            _activeMenu = nullptr;
            menu->Hide();
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

    void DesktopEnvironment::HandleLeftDoubleClick(int x, int y)
    {
        if (_drag.dragging || (_activeMenu && _activeMenu->IsVisible()))
        {
            return;
        }

        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);

        balcony::ui::Component* hit = _taskbar.FindHit(fx, fy);
        if (!hit)
        {
            hit = _desktop.FindHit(fx, fy);
        }
        if (hit)
        {
            hit->DoubleClick();
        }
    }

    void DesktopEnvironment::HandleLeftButtonDown(int x, int y)
    {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);

        balcony::ui::Component* hit = nullptr;
        if (_activeMenu && _activeMenu->IsVisible())
        {
            // Only look for something draggable WITHIN the open menu
            // itself -- e.g. the volume flyout's slider, or the network
            // flyout's scrollable list, both opened via
            // Balcony.ShowFlyout and genuinely meant to be interacted
            // with while "open". Deliberately never falls through to
            // the taskbar/desktop below: dragging something BEHIND an
            // open menu (a desktop icon, say) would be a confusing
            // interaction while a menu has focus.
            hit = _activeMenu->FindDraggable(fx, fy);
        }
        else
        {
            hit = _taskbar.FindDraggable(fx, fy);
            if (!hit)
            {
                hit = _desktop.FindDraggable(fx, fy);
            }
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

        // Mutually exclusive by which callback the composer wired up: a
        // component with SetOnDrag set (a slider, a scrollbar thumb)
        // gets raw cursor coordinates to interpret however it wants;
        // one without it (a desktop icon) gets repositioned by the
        // move's delta, same as before. See Component::SetOnDrag.
        if (_drag.target->HasOnDrag())
        {
            _drag.target->Drag(fx, fy);
        }
        else
        {
            _drag.target->Translate(fx - _drag.lastX, fy - _drag.lastY);
        }
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
