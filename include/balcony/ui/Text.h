#pragma once

#include "balcony/core/Invalidation.h"
#include "balcony/renderer/Texture.h"
#include "balcony/ui/VisualComponent.h"

#include <string>
#include <string_view>

namespace balcony::renderer
{
    class GraphicsDevice;
    class CommandQueue;
}

// Rasterizes a string once (via GDI) into a texture, then draws that
// texture through the same PrimitiveRenderer path Image uses -- text is
// not a separate rendering pipeline, only the pixel source differs.
// See CLAUDE.md section 5.
namespace balcony::ui
{
    class Text : public VisualComponent
    {
    public:
        // Sets the font used by future SetText() calls. Doesn't touch
        // the GPU by itself -- call SetText again to re-rasterize with
        // it, same as changing anything else here.
        void SetFont(std::wstring_view fontFamily, int fontHeightPx)
        {
            _fontFamily = fontFamily;
            _fontHeightPx = fontHeightPx;
        }

        bool SetText(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                     balcony::renderer::PrimitiveRenderer& renderer, std::wstring_view text);

        void SetColor(const balcony::renderer::ColorRGBA& color) { _color = color; balcony::core::RequestRedraw(); }

        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

    private:
        balcony::renderer::Texture _texture;
        balcony::renderer::ColorRGBA _color{};
        std::wstring _fontFamily = L"Segoe UI";
        int _fontHeightPx = 24;
    };
}
