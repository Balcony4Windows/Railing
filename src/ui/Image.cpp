#include "balcony/ui/Image.h"

namespace balcony::ui
{
    bool Image::SetSource(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                           balcony::renderer::PrimitiveRenderer& renderer, std::wstring_view path)
    {
        balcony::renderer::Texture texture;
        if (!texture.CreateFromFile(device, queue, renderer, path))
        {
            return false;
        }

        SetTexture(texture);
        return true;
    }

    void Image::SetTexture(const balcony::renderer::Texture& texture)
    {
        _texture = texture;
        if (Bounds().width == 0.0f && Bounds().height == 0.0f)
        {
            SetSize(static_cast<float>(texture.Width()), static_cast<float>(texture.Height()));
        }
    }

    void Image::SetPixels(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                           balcony::renderer::PrimitiveRenderer& renderer,
                           uint32_t width, uint32_t height, const uint8_t* rgba8)
    {
        _texture.CreateFromPixels(device, queue, renderer, width, height, rgba8);
        if (Bounds().width == 0.0f && Bounds().height == 0.0f)
        {
            SetSize(static_cast<float>(width), static_cast<float>(height));
        }
    }

    void Image::Draw(balcony::renderer::PrimitiveRenderer& renderer) const
    {
        VisualComponent::Draw(renderer);

        if (!_texture.IsValid())
        {
            return;
        }

        renderer.DrawQuad(Bounds(), _texture.SrvIndex(), {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, _opacity});
    }
}
