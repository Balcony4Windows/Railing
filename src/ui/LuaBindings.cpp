#include "balcony/ui/LuaBindings.h"

#include "balcony/core/IconExtractor.h"
#include "balcony/renderer/Renderer.h"
#include "balcony/ui/Animation.h"
#include "balcony/ui/Audio.h"
#include "balcony/ui/Canvas.h"
#include "balcony/ui/Component.h"
#include "balcony/ui/Container.h"
#include "balcony/ui/Image.h"
#include "balcony/ui/Spacer.h"
#include "balcony/ui/Text.h"
#include "balcony/ui/Tooltip.h"
#include "balcony/ui/VisualComponent.h"

#include <sol/sol.hpp>

#include <Windows.h>

#include <cstdint>
#include <vector>

namespace balcony::ui
{
    namespace
    {
        std::wstring Utf8ToWide(const std::string& text)
        {
            if (text.empty())
            {
                return {};
            }

            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }

        void LogLuaCallbackError(const sol::protected_function_result& result)
        {
            const sol::error err = result;
            OutputDebugStringA(err.what());
            OutputDebugStringA("\n");
        }

        // Wraps a Lua function as a protected C++ callback: a Lua-side
        // error inside it (a typo'd method name, a nil access, ...) is
        // caught and logged instead of propagating as an unprotected
        // Lua error through the C++ call stack that invoked it. That
        // matters here specifically because these callbacks are invoked
        // from deep inside C++ (Window's WndProc, the render loop's
        // Draw() tree) with no lua_pcall boundary anywhere above them --
        // unlike a top-level script run (RunFile/RunString), which
        // LuaRuntime already wraps in one. Without this, a Lua error
        // here unwinds via a raw Lua longjmp straight through those C++
        // frames, which is undefined behavior and crashes the whole
        // process (this is exactly what a missing/misspelled method
        // call did before this wrapper existed). See CLAUDE.md section
        // 17: a script bug -- or someday a genuinely untrusted plugin --
        // must not be able to take the whole DE down with it.
        std::function<void()> ProtectedCallback(sol::protected_function fn)
        {
            return [fn = std::move(fn)]()
            {
                if (!fn.valid())
                {
                    return;
                }
                const sol::protected_function_result result = fn();
                if (!result.valid())
                {
                    LogLuaCallbackError(result);
                }
            };
        }

        std::function<void(float, float)> ProtectedDragCallback(sol::protected_function fn)
        {
            return [fn = std::move(fn)](float x, float y)
            {
                if (!fn.valid())
                {
                    return;
                }
                const sol::protected_function_result result = fn(x, y);
                if (!result.valid())
                {
                    LogLuaCallbackError(result);
                }
            };
        }
    }

    void RegisterLuaBindings(sol::state& lua, balcony::renderer::Renderer& renderer)
    {
        lua.new_usertype<balcony::renderer::RectF>("RectF",
            sol::constructors<balcony::renderer::RectF(), balcony::renderer::RectF(float, float, float, float)>(),
            "x", &balcony::renderer::RectF::x,
            "y", &balcony::renderer::RectF::y,
            "width", &balcony::renderer::RectF::width,
            "height", &balcony::renderer::RectF::height);

        lua.new_usertype<balcony::renderer::ColorRGBA>("Color",
            sol::constructors<balcony::renderer::ColorRGBA(), balcony::renderer::ColorRGBA(float, float, float, float)>(),
            "r", &balcony::renderer::ColorRGBA::r,
            "g", &balcony::renderer::ColorRGBA::g,
            "b", &balcony::renderer::ColorRGBA::b,
            "a", &balcony::renderer::ColorRGBA::a);

        // Base classes are registered without constructors -- Lua never
        // builds a bare Component/VisualComponent itself, only receives
        // instances of concrete types (or via GetTooltip()).
        lua.new_usertype<Component>("Component",
            "SetTooltip", &Component::SetTooltip,
            "GetTooltip", &Component::GetTooltip,
            "SetOnClick", [](Component& self, sol::protected_function fn) { self.SetOnClick(ProtectedCallback(std::move(fn))); },
            "SetOnDoubleClick", [](Component& self, sol::protected_function fn) { self.SetOnDoubleClick(ProtectedCallback(std::move(fn))); },
            "SetDraggable", &Component::SetDraggable,
            "IsDraggable", &Component::IsDraggable,
            "SetOnDragEnd", [](Component& self, sol::protected_function fn) { self.SetOnDragEnd(ProtectedCallback(std::move(fn))); },
            "SetOnDrag", [](Component& self, sol::protected_function fn) { self.SetOnDrag(ProtectedDragCallback(std::move(fn))); },
            // Previously only ever called from C++ (the drag state
            // machine, Tooltip::Show internally) -- the volume/network
            // flyouts are the first callers that need to invoke it
            // directly from Lua (undoing a previous Show() translation
            // before repositioning -- see volume_flyout.lua).
            "Translate", &Component::Translate);

        lua.new_usertype<VisualComponent>("VisualComponent",
            sol::base_classes, sol::bases<Component>(),
            "SetBounds", &VisualComponent::SetBounds,
            "Bounds", &VisualComponent::Bounds,
            "SetPosition", &VisualComponent::SetPosition,
            "SetSize", &VisualComponent::SetSize,
            "SetBackgroundColor", [&renderer](VisualComponent& self, const balcony::renderer::ColorRGBA& color)
            {
                self.Initialize(renderer.Device(), renderer.Queue(), renderer.Primitives());
                self.SetBackgroundColor(color);
            },
            "SetBorderColor", [&renderer](VisualComponent& self, const balcony::renderer::ColorRGBA& color)
            {
                self.Initialize(renderer.Device(), renderer.Queue(), renderer.Primitives());
                self.SetBorderColor(color);
            },
            "SetBorderWidth", [&renderer](VisualComponent& self, float width)
            {
                self.Initialize(renderer.Device(), renderer.Queue(), renderer.Primitives());
                self.SetBorderWidth(width);
            });

        lua.new_usertype<Text>("Text",
            sol::constructors<Text()>(),
            sol::base_classes, sol::bases<VisualComponent, Component>(),
            "SetFont", [](Text& self, const std::string& fontFamily, int fontHeightPx)
            {
                self.SetFont(Utf8ToWide(fontFamily), fontHeightPx);
            },
            "SetColor", &Text::SetColor,
            "SetText", [&renderer](Text& self, const std::string& text)
            {
                return self.SetText(renderer.Device(), renderer.Queue(), renderer.Primitives(), Utf8ToWide(text));
            });

        lua.new_usertype<Image>("Image",
            sol::constructors<Image()>(),
            sol::base_classes, sol::bases<VisualComponent, Component>(),
            "SetOpacity", &Image::SetOpacity,
            "SetSource", [&renderer](Image& self, const std::string& path)
            {
                return self.SetSource(renderer.Device(), renderer.Queue(), renderer.Primitives(), Utf8ToWide(path));
            },
            "SetSystemIcon", [&renderer](Image& self, const std::string& path)
            {
                std::vector<uint8_t> pixels;
                uint32_t width = 0;
                uint32_t height = 0;
                if (!balcony::core::ExtractIconPixels(Utf8ToWide(path), pixels, width, height))
                {
                    return false;
                }

                // SetPixels (not SetTexture(freshTexture)) so a re-icon on
                // the same Image reuses its existing SRV slot instead of
                // leaking a new one -- see Image::SetPixels.
                self.SetPixels(renderer.Device(), renderer.Queue(), renderer.Primitives(), width, height, pixels.data());
                return true;
            },
            "SetWindowIcon", [&renderer](Image& self, uint64_t windowId)
            {
                std::vector<uint8_t> pixels;
                uint32_t width = 0;
                uint32_t height = 0;
                if (!balcony::core::ExtractWindowIconPixels(windowId, pixels, width, height))
                {
                    return false;
                }

                self.SetPixels(renderer.Device(), renderer.Queue(), renderer.Primitives(), width, height, pixels.data());
                return true;
            });

        lua.new_usertype<Animation>("Animation",
            sol::constructors<Animation()>(),
            sol::base_classes, sol::bases<VisualComponent, Component>(),
            "SetFrameDuration", &Animation::SetFrameDuration,
            "SetLooping", &Animation::SetLooping,
            "AddFrameFromFile", [&renderer](Animation& self, const std::string& path)
            {
                return self.AddFrameFromFile(renderer.Device(), renderer.Queue(), renderer.Primitives(), Utf8ToWide(path));
            });

        lua.new_usertype<Spacer>("Spacer",
            sol::constructors<Spacer()>(),
            sol::base_classes, sol::bases<VisualComponent, Component>());

        lua.new_usertype<Container>("Container",
            sol::constructors<Container()>(),
            sol::base_classes, sol::bases<VisualComponent, Component>(),
            "Add", &Container::Add,
            "Remove", &Container::Remove);

        lua.new_usertype<Canvas>("Canvas",
            sol::constructors<Canvas()>(),
            sol::base_classes, sol::bases<VisualComponent, Component>(),
            // Initializes on first use (same pattern as
            // SetBackgroundColor/SetBorderColor/SetBorderWidth above) --
            // a Canvas always needs its fill texture the moment it has
            // something to draw, unlike background/border which stay
            // off by default.
            "SetOnDraw", [&renderer](Canvas& self, sol::protected_function callback)
            {
                self.Initialize(renderer.Device(), renderer.Queue(), renderer.Primitives());
                self.SetOnDraw(ProtectedCallback(std::move(callback)));
            },
            "SetAnimated", &Canvas::SetAnimated,
            "DrawRect", &Canvas::DrawRect);

        lua.new_usertype<Tooltip>("Tooltip",
            sol::constructors<Tooltip()>(),
            sol::base_classes, sol::bases<Container, VisualComponent, Component>(),
            "Show", &Tooltip::Show,
            "Hide", &Tooltip::Hide,
            "IsVisible", &Tooltip::IsVisible,
            "SetCloseOnClick", &Tooltip::SetCloseOnClick,
            "CloseOnClick", &Tooltip::CloseOnClick,
            "Initialize", [&renderer](Tooltip& self)
            {
                self.Initialize(renderer.Device(), renderer.Queue(), renderer.Primitives());
            });

        lua.new_usertype<Audio>("Audio",
            sol::constructors<Audio()>(),
            "SetSource", [](Audio& self, const std::string& path)
            {
                return self.SetSource(Utf8ToWide(path));
            },
            "Play", &Audio::Play,
            "Stop", &Audio::Stop);
    }
}
