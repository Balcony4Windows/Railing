#pragma once

#include "balcony/renderer/Texture.h"
#include "balcony/ui/VisualComponent.h"

#include <string_view>
#include <vector>

namespace balcony::renderer
{
    class GraphicsDevice;
    class CommandQueue;
}

// Cycles through pre-built texture frames. An animation is just an image
// whose texture changes over time -- it draws through the exact same
// PrimitiveRenderer path Image uses, not a dedicated pipeline.
// See CLAUDE.md section 5.
namespace balcony::ui
{
    class Animation : public VisualComponent
    {
    public:
        // Decodes a single frame from an image file (PNG/JPEG/BMP/...)
        // via WIC and appends it. Call once per frame, in order.
        bool AddFrameFromFile(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                               balcony::renderer::PrimitiveRenderer& renderer, std::wstring_view path);

        // For frames built programmatically.
        void AddFrame(const balcony::renderer::Texture& frame) { _frames.push_back(frame); }

        void SetFrameDuration(float seconds) { _frameDuration = seconds; }
        void SetLooping(bool looping) { _looping = looping; }

        void Update(float deltaSeconds) override;
        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

    private:
        std::vector<balcony::renderer::Texture> _frames;
        float _frameDuration = 0.1f;
        float _elapsed = 0.0f;
        size_t _currentFrame = 0;
        bool _looping = true;
    };
}
