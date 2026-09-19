#pragma once

#include "balcony/renderer/Texture.h"
#include "balcony/ui/VisualComponent.h"

#include <string_view>

namespace balcony::renderer
{
    class GraphicsDevice;
    class CommandQueue;
}

// Draws a texture inside its bounds. See CLAUDE.md section 5.
namespace balcony::ui
{
    class Image : public VisualComponent
    {
    public:
        // Decodes a common image format (PNG/JPEG/BMP/ICO/...) via WIC
        // and uses it as this Image's content. Bounds default to the
        // decoded image's own size.
        bool SetSource(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                        balcony::renderer::PrimitiveRenderer& renderer, std::wstring_view path);

        // For textures built programmatically (solid colors, render
        // targets, ...) rather than loaded from a file.
        void SetTexture(const balcony::renderer::Texture& texture);

        void SetOpacity(float opacity) { _opacity = opacity; }

        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

    private:
        balcony::renderer::Texture _texture;
        float _opacity = 1.0f;
    };
}
