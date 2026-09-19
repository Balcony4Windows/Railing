#pragma once

#include "balcony/ui/VisualComponent.h"

#include <vector>

// Composes other components. Bounds/background/border are inherited from
// VisualComponent -- off by default, so a Container draws nothing of its
// own unless the composer explicitly sets one, same as everywhere else.
// There is still no automatic layout: bounds are informational (e.g.
// "this is the screen region the taskbar occupies") and Container never
// applies them to children. Placement and styling of each child stays
// on that child, set directly by whoever composes it. See CLAUDE.md
// sections 5 and 15 on composition over configuration -- this is the
// CONTAINER primitive, not a layout engine.
//
// Non-owning: Container holds raw pointers and never deletes them.
// Lifetime belongs to whoever constructs the components (a Lua-driven
// composition layer will eventually decide this more thoroughly; that
// is an open question this deliberately doesn't preempt).
namespace balcony::ui
{
    class Container : public VisualComponent
    {
    public:
        void Add(Component* component);
        void Remove(Component* component);

        void Update(float deltaSeconds) override;
        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

        // Shifts this Container's own bounds (inherited from
        // VisualComponent) and every child by the same (dx, dy) -- so
        // moving a populated Container keeps its content aligned with
        // it, recursing into any child Container of its own.
        void Translate(float dx, float dy) override;

        // Searches children back-to-front (last added = topmost) before
        // falling back to this Container's own bounds, so an overlapping
        // child wins over its parent.
        Component* FindHit(float x, float y) override;

    private:
        std::vector<Component*> _children;
    };
}
