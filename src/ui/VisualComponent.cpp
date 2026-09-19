#include "balcony/ui/VisualComponent.h"

namespace balcony::ui
{
    void VisualComponent::Initialize(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                                      balcony::renderer::PrimitiveRenderer& renderer)
    {
        if (IsInitialized())
        {
            return;
        }

        const uint8_t white[4] = {255, 255, 255, 255};
        _fillTexture.CreateFromPixels(device, queue, renderer, 1, 1, white);
    }

    void VisualComponent::Draw(balcony::renderer::PrimitiveRenderer& renderer) const
    {
        if (!_fillTexture.IsValid())
        {
            return;
        }

        if (_borderWidth > 0.0f)
        {
            renderer.DrawQuad(_bounds, _fillTexture.SrvIndex(), {0.0f, 0.0f, 1.0f, 1.0f}, _borderColor);
        }

        if (_hasBackground)
        {
            balcony::renderer::RectF fillBounds = _bounds;
            if (_borderWidth > 0.0f)
            {
                fillBounds.x += _borderWidth;
                fillBounds.y += _borderWidth;
                fillBounds.width -= _borderWidth * 2.0f;
                fillBounds.height -= _borderWidth * 2.0f;
            }

            if (fillBounds.width > 0.0f && fillBounds.height > 0.0f)
            {
                renderer.DrawQuad(fillBounds, _fillTexture.SrvIndex(), {0.0f, 0.0f, 1.0f, 1.0f}, _backgroundColor);
            }
        }
    }
}
