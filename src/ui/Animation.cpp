#include "balcony/ui/Animation.h"

#include "balcony/core/Invalidation.h"

namespace balcony::ui
{
    bool Animation::AddFrameFromFile(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                                      balcony::renderer::PrimitiveRenderer& renderer, std::wstring_view path)
    {
        balcony::renderer::Texture frame;
        if (!frame.CreateFromFile(device, queue, renderer, path))
        {
            return false;
        }

        _frames.push_back(frame);
        return true;
    }

    void Animation::Update(float deltaSeconds)
    {
        if (_frames.size() < 2)
            return;

        const size_t startingFrame = _currentFrame;

        _elapsed += deltaSeconds;
        while (_elapsed >= _frameDuration)
        {
            _elapsed -= _frameDuration;
            const size_t next = _currentFrame + 1;
            if (next < _frames.size())
                _currentFrame = next;
            else if (_looping)
                _currentFrame = 0;
        }

        // Only the C++ side knows a frame actually advanced here (this
        // bypasses every Lua-visible setter) -- request the redraw
        // that'd otherwise never happen. See Invalidation.h.
        if (_currentFrame != startingFrame)
        {
            balcony::core::RequestRedraw();
        }
    }

    void Animation::Draw(balcony::renderer::PrimitiveRenderer& renderer) const
    {
        VisualComponent::Draw(renderer);

        if (_frames.empty())
        {
            return;
        }

        const auto& frame = _frames[_currentFrame];
        renderer.DrawQuad(Bounds(), frame.SrvIndex());
    }
}
