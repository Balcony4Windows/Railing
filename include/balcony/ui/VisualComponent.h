#pragma once

#include "balcony/core/Invalidation.h"
#include "balcony/renderer/Texture.h"
#include "balcony/ui/Component.h"

namespace balcony::renderer
{
    class GraphicsDevice;
    class CommandQueue;
}

// Base for every component that occupies a rectangular area on screen
// (Image, Text, Animation, Spacer, Container, ...) -- everything except
// Audio, which has no screen footprint and stays a plain Component.
// Owns the properties genuinely common to all of them: bounds (and the
// hit-testing that falls out of it for free) plus an optional
// background/border. This is not a styling/CSS system -- just the two
// things components kept independently reinventing. Both background and
// border are off until the composer sets them explicitly; nothing is
// preset. See CLAUDE.md section 5.
namespace balcony::ui
{
    class VisualComponent : public Component
    {
    public:
        // Every one of these requests a redraw -- see Invalidation.h.
        // Cheap (a bool write) and correct by construction: whatever
        // changes a component's visual state funnels through one of
        // these, so a composer/script never needs to remember to ask
        // for a repaint itself.
        void SetBounds(const balcony::renderer::RectF& bounds) { _bounds = bounds; balcony::core::RequestRedraw(); }
        const balcony::renderer::RectF& Bounds() const { return _bounds; }

        void SetPosition(float x, float y) { _bounds.x = x; _bounds.y = y; balcony::core::RequestRedraw(); }
        void SetSize(float width, float height) { _bounds.width = width; _bounds.height = height; balcony::core::RequestRedraw(); }

        void Translate(float dx, float dy) override { _bounds.x += dx; _bounds.y += dy; balcony::core::RequestRedraw(); }

        bool HitTest(float x, float y) const override { return _bounds.Contains(x, y); }

        // Background/border need a texture to draw with. Only required
        // if you actually use one of them below -- skip it entirely for
        // a component that doesn't. Idempotent: a second call is a
        // cheap no-op, so callers (including Lua wrappers that can't
        // know if this already ran) don't need to track it themselves.
        void Initialize(balcony::renderer::GraphicsDevice& device, balcony::renderer::CommandQueue& queue,
                         balcony::renderer::PrimitiveRenderer& renderer);
        bool IsInitialized() const { return _fillTexture.IsValid(); }

        void SetBackgroundColor(const balcony::renderer::ColorRGBA& color)
        {
            _backgroundColor = color;
            _hasBackground = true;
            balcony::core::RequestRedraw();
        }

        void SetBorderColor(const balcony::renderer::ColorRGBA& color) { _borderColor = color; balcony::core::RequestRedraw(); }
        void SetBorderWidth(float width) { _borderWidth = width; balcony::core::RequestRedraw(); }

        // Draws the background/border (if any). Subclasses with their
        // own content call this explicitly from their own Draw();
        // components with no content of their own (Spacer, Container)
        // just inherit this as their whole Draw().
        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

    protected:
        // For a subclass (Canvas) that draws solid-colored shapes of its
        // own, on top of the same shared, batched rendering path
        // background/border already use -- reuses the SAME 1x1 white
        // texture instead of every drawing call needing its own. Only
        // meaningful once Initialize() has actually run.
        uint32_t FillTextureSrvIndex() const { return _fillTexture.SrvIndex(); }

    private:
        balcony::renderer::RectF _bounds{};

        balcony::renderer::Texture _fillTexture;
        balcony::renderer::ColorRGBA _backgroundColor{};
        balcony::renderer::ColorRGBA _borderColor{};
        float _borderWidth = 0.0f;
        bool _hasBackground = false;
    };
}
