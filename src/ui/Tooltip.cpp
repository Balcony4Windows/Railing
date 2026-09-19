#include "balcony/ui/Tooltip.h"

namespace balcony::ui
{
    void Tooltip::Initialize(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                              balcony::renderer::PrimitiveRenderer& renderer)
    {
        const bool firstTime = !IsInitialized();
        VisualComponent::Initialize(device, queue, renderer);
        if (firstTime)
        {
            // Only on first init -- a repeat call (idempotent, e.g. from
            // a Lua wrapper) must not clobber a color set in between.
            SetBackgroundColor({0.12f, 0.12f, 0.12f, 0.95f});
        }
    }

    void Tooltip::Show(float x, float y)
    {
        // Translate rather than SetBounds directly: every child was
        // positioned relative to wherever this Tooltip was originally
        // laid out, so moving just the Tooltip's own bounds would leave
        // its content behind. Translate() shifts both by the same
        // delta, keeping content aligned no matter where Show() is
        // called from -- the composer never needs to reposition a
        // Tooltip's children by hand just because it moved.
        Translate(x - Bounds().x, y - Bounds().y);
        _visible = true;
    }

    void Tooltip::Draw(balcony::renderer::PrimitiveRenderer& renderer) const
    {
        if (!_visible)
        {
            return;
        }

        Container::Draw(renderer);
    }
}
