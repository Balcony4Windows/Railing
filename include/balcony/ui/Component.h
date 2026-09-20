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

        // Returns the nearest DRAGGABLE component at (x, y), or nullptr
        // -- deliberately separate from FindHit above. Click-hit-testing
        // must keep resolving to the deepest leaf (e.g. a desktop icon's
        // Image/Text, each with its own independent OnClick/Tooltip),
        // while drag-hit-testing needs to resolve to the nearest
        // draggable ANCESTOR (e.g. a Container wrapping that icon+label
        // pair, since dragging is a whole-group gesture). Keeping these
        // as two independent virtuals means neither needs to know about
        // the other, and no parent back-pointers are needed anywhere.
        // The default just applies IsDraggable()+HitTest to this
        // component; Container overrides it to search children first,
        // exactly mirroring FindHit.
        virtual Component* FindDraggable(float x, float y) { return (_draggable && HitTest(x, y)) ? this : nullptr; }

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

        void SetDraggable(bool draggable) { _draggable = draggable; }
        bool IsDraggable() const { return _draggable; }

        // Fired once when a drag that actually moved past the click
        // threshold ends (see DesktopEnvironment's drag state machine);
        // never fired for an ordinary click. No coordinates are passed
        // -- the composer's own closure already knows which object this
        // is (it's the one that called SetOnDragEnd) and can read its
        // current Bounds() itself.
        void SetOnDragEnd(std::function<void()> handler) { _onDragEnd = std::move(handler); }
        void DragEnd() const { if (_onDragEnd) _onDragEnd(); }

    private:
        Tooltip* _tooltip = nullptr;
        std::function<void()> _onClick;
        bool _draggable = false;
        std::function<void()> _onDragEnd;
    };
}
