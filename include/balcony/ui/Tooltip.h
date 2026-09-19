#pragma once

#include "balcony/ui/Container.h"

namespace balcony::renderer
{
    class GraphicsDevice;
    class CommandQueue;
}

// A right-click popup: a Container (so it can hold whatever content a
// context menu/tooltip needs -- Text, Image, more Containers, ...),
// using the same background/border every VisualComponent has, plus
// show/hide/position state. Unlike most components' background (off
// until set), Initialize() here also picks a sensible default so a
// Tooltip looks like a tooltip out of the box; call SetBackgroundColor
// afterward to override it. Hidden and empty until the composer adds
// children and calls Show(); nothing about size or content is preset.
// See CLAUDE.md section 5.
//
// This class does not detect right clicks itself -- that is a future
// input-routing concern. It only provides the popup mechanics that
// component's handler would call into.
namespace balcony::ui
{
    class Tooltip : public Container
    {
    public:
        void Initialize(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                         balcony::renderer::PrimitiveRenderer& renderer);

        // Moves to (x, y), keeping whatever size was set via SetBounds,
        // and becomes visible.
        void Show(float x, float y);
        void Hide() { _visible = false; }
        bool IsVisible() const { return _visible; }

        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

        // A hidden tooltip must never intercept a click meant for
        // whatever is underneath it.
        Component* FindHit(float x, float y) override { return _visible ? Container::FindHit(x, y) : nullptr; }

    private:
        bool _visible = false;
    };
}
