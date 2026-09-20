#pragma once

#include "balcony/ui/VisualComponent.h"

#include <functional>

// A blank, general-purpose drawing surface: re-invokes a composer-
// supplied draw callback every frame, letting a script issue its own
// solid-color rectangle draws through the same shared, batched
// rendering path every other primitive already draws through
// (PrimitiveRenderer -- see CLAUDE.md sections 4 and 5).
//
// This exists so "I want a custom visual" (an audio visualizer, a
// graph, a custom clock face, a progress ring approximated from
// rectangles, ...) doesn't need a new C++ class every time -- Canvas is
// the one new primitive; everything built with it stays in Lua. See
// TODO.md's extensibility question.
//
// Deliberately minimal: one drawing primitive (a solid-colored,
// axis-aligned rectangle), not a general 2D graphics API. Composing
// many small rectangles already covers a wide range of shapes at UI
// scale; a composer wanting curves/rotation/textured draws is a signal
// for a future, deliberately-designed addition here, not a reason to
// grow this into a full vector-graphics surface up front.
namespace balcony::ui
{
    class Canvas : public VisualComponent
    {
    public:
        void SetOnDraw(std::function<void()> callback) { _onDraw = std::move(callback); }

        // Off by default: a Canvas only redraws when something actually
        // requests it (Balcony.RequestRedraw() from Lua, or any of the
        // mutating calls elsewhere in this codebase already do) -- the
        // same "only touch the screen when something really changed"
        // discipline every other primitive already follows (see the
        // clock in scripts/desktop.lua, which only re-rasterizes when
        // the displayed second actually changes). Turn this on for a
        // Canvas that's a genuine continuous animation -- an audio
        // visualizer, a spinner -- where the content changes
        // essentially every frame regardless; it then keeps requesting
        // a redraw for as long as it stays on, same as Animation does
        // automatically while it has 2+ frames.
        void SetAnimated(bool animated) { _animated = animated; }

        // Draws a solid-colored, axis-aligned rectangle in the same
        // absolute-pixel coordinate space as every other component's
        // bounds (not relative to this Canvas's own bounds -- nothing
        // in this codebase auto-applies a parent's bounds to what it
        // draws, see Container's own comment on the same point). Only
        // meaningful from inside the OnDraw callback, since it needs
        // the current frame's renderer (stashed by Draw() below); a
        // no-op otherwise, same "does nothing if not wired up"
        // contract as Component::Click()/DragEnd() with no handler set.
        void DrawRect(const balcony::renderer::RectF& rect, const balcony::renderer::ColorRGBA& color) const;

        void Draw(balcony::renderer::PrimitiveRenderer& renderer) const override;

    private:
        std::function<void()> _onDraw;
        bool _animated = false;

        // Valid only for the duration of one Draw() call -- lets
        // DrawRect (invoked synchronously from within the OnDraw
        // callback Draw() calls) reach the current frame's renderer
        // without threading it through every call. Mutable because
        // Draw() is const, like every other Component::Draw override.
        mutable balcony::renderer::PrimitiveRenderer* _currentRenderer = nullptr;
    };
}
