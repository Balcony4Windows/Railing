#pragma once

#include "balcony/core/Invalidation.h"
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

        // Uploads raw RGBA8 pixels directly into this Image's own
        // texture, reusing its existing GPU descriptor slot if it already
        // has one -- unlike building a separate Texture and handing it to
        // SetTexture(), which always leaves the old slot (if any) behind
        // unreclaimed. What Image::SetSystemIcon/SetWindowIcon use.
        // Bounds default to the pixel buffer's own size the first time
        // this is called.
        void SetPixels(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                        balcony::renderer::PrimitiveRenderer& renderer,
                        uint32_t width, uint32_t height, const uint8_t* rgba8);

        void SetOpacity(float opacity) { _opacity = opacity; balcony::core::RequestRedraw(); }

        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

    private:
        balcony::renderer::Texture _texture;
        float _opacity = 1.0f;
    };
}
