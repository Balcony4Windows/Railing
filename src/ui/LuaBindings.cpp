#include "balcony/ui/LuaBindings.h"

#include "balcony/core/IconExtractor.h"
#include "balcony/renderer/Renderer.h"
#include "balcony/ui/Animation.h"
#include "balcony/ui/Audio.h"
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
            "SetOnClick", &Component::SetOnClick,
            "SetDraggable", &Component::SetDraggable,
            "IsDraggable", &Component::IsDraggable,
            "SetOnDragEnd", &Component::SetOnDragEnd);

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

        lua.new_usertype<Tooltip>("Tooltip",
            sol::constructors<Tooltip()>(),
            sol::base_classes, sol::bases<Container, VisualComponent, Component>(),
            "Show", &Tooltip::Show,
            "Hide", &Tooltip::Hide,
            "IsVisible", &Tooltip::IsVisible,
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
