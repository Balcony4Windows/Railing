#pragma once

#include "balcony/renderer/PrimitiveRenderer.h"

#include <functional>

namespace balcony::ui
{
    class Tooltip;
}

// Common interface every UI primitive satisfies (Text, Image, Animation,
// Spacer, Audio, Container, ...) so a Container can hold and drive a
// heterogeneous set of them without knowing their concrete types. Both
// Update and Draw default to doing nothing: most primitives only need
// one of them, and some (Audio) need neither. See CLAUDE.md section 5.
//
// Deliberately NOT part of this interface: bounds, position, color,
// texture, or any other per-primitive property. Placement and styling
// stay on the concrete component, set directly by whoever composes it --
// there is no shared layout/style surface to preset here.
//
// Two exceptions, both non-owning attach points that do nothing until
// the composer wires them up:
//  - Tooltip: any component can have a right-click popup attached.
//  - OnClick: any component can have a click handler attached (e.g. a
//    Text acting as a menu item inside a Tooltip).
// HitTest/FindHit exist so something driving mouse input can ask "what,
// if anything, is at this point" without knowing concrete types. The
// default HitTest is false (most primitives don't occupy a clickable
// area); anything with real bounds (Image, Text, Animation, Spacer,
// Container) overrides it to check them.
namespace balcony::ui
{
    class Component
    {
    public:
        virtual ~Component() = default;

        virtual void Update(float /*deltaSeconds*/) {}
        virtual void Draw(balcony::renderer::PrimitiveRenderer& /*renderer*/) const {}

        virtual bool HitTest(float /*x*/, float /*y*/) const { return false; }

        // Returns the deepest component at (x, y), or nullptr. The
        // default just applies HitTest to this component; Container
        // overrides it to search children first.
        virtual Component* FindHit(float x, float y) { return HitTest(x, y) ? this : nullptr; }

        // Shifts this component by (dx, dy). Default is a no-op (most
        // components are positioned once and never moved again).
        // VisualComponent shifts its own bounds; Container additionally
        // shifts every child by the same amount, so moving a populated
        // Container (e.g. a Tooltip whose content was laid out relative
        // to wherever it was first created) keeps that content aligned
        // with it. See Tooltip::Show, the reason this exists.
        virtual void Translate(float /*dx*/, float /*dy*/) {}

        void SetTooltip(Tooltip* tooltip) { _tooltip = tooltip; }
        Tooltip* GetTooltip() const { return _tooltip; }

        void SetOnClick(std::function<void()> handler) { _onClick = std::move(handler); }
        void Click() const { if (_onClick) _onClick(); }

    private:
        Tooltip* _tooltip = nullptr;
        std::function<void()> _onClick;
    };
}
